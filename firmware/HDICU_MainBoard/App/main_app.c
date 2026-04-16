/**
 * @file    main_app.c
 * @brief   Application entry point — called from main() after HAL_Init
 *
 *          UART RX architecture:
 *          - Sensor bytes (CO2/O2/JFC103): parsed in ISR (write to private static buffers only)
 *          - Protocol bytes (iPad/Screen): buffered in ISR, dispatched in task context via queues
 *          This avoids writing shared AppData_t from ISR context.
 */

#include "bsp_config.h"
#include "app_data.h"
#include "task_defs.h"

/* Hardware drivers */
#include "uart_driver.h"
#include "adc_driver.h"
#include "pwm_driver.h"
#include "relay_driver.h"

/* Sensor drivers */
#include "co2_sensor.h"
#include "o2_sensor.h"
#include "jfc103_sensor.h"

/* Protocol handlers */
#include "ipad_protocol.h"
#include "screen_protocol.h"

/* Storage */
#include "flash_storage.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ========================================================================= */
/*  UART RX Queues — decouple ISR from protocol dispatch                     */
/* ========================================================================= */
#define UART_RX_QUEUE_LEN   128

QueueHandle_t g_ipad_rx_queue;
QueueHandle_t g_screen_rx_queue;

/**
 * @brief Fatal init error — hang with buzzer on.
 *        Called when RTOS resource creation fails (queues, tasks).
 */
void fatal_init_error(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_3;  /* PB3 buzzer */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
    for (;;) {}  /* Hang with buzzer on */
}

void app_init(void)
{
    /* 1. Create RX queues BEFORE initializing UART (which enables interrupts) */
    g_ipad_rx_queue = xQueueCreate(UART_RX_QUEUE_LEN, sizeof(uint8_t));
    g_screen_rx_queue = xQueueCreate(UART_RX_QUEUE_LEN, sizeof(uint8_t));
    if (!g_ipad_rx_queue || !g_screen_rx_queue) fatal_init_error();

    /* 2. Initialize hardware drivers */
    uart_driver_init();
    adc_driver_init();
    pwm_driver_init();
    relay_driver_init();

    /* 2b. Initialize buzzer + nursing LED GPIOs (not in any driver module) */
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        GPIO_InitTypeDef gpio = {0};
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;

        /* Buzzer: PB3 */
        gpio.Pin = BSP_BUZZER_PIN;
        HAL_GPIO_Init(BSP_BUZZER_PORT, &gpio);
        HAL_GPIO_WritePin(BSP_BUZZER_PORT, BSP_BUZZER_PIN, GPIO_PIN_RESET);

        /* Nursing LEDs: PB1, PB0 (GPIOB) + PC5 (GPIOC) */
        gpio.Pin = BSP_LED_HULI1_PIN | BSP_LED_HULI2_PIN;
        HAL_GPIO_Init(BSP_LED_HULI1_PORT, &gpio);  /* GPIOB */
        gpio.Pin = BSP_LED_HULI3_PIN;
        HAL_GPIO_Init(BSP_LED_HULI3_PORT, &gpio);  /* GPIOC */
        HAL_GPIO_WritePin(BSP_LED_HULI1_PORT, BSP_LED_HULI1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BSP_LED_HULI2_PORT, BSP_LED_HULI2_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BSP_LED_HULI3_PORT, BSP_LED_HULI3_PIN, GPIO_PIN_RESET);

        /* Liquid/Urine detect inputs: PB14, PB15 */
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_PULLUP;
        gpio.Pin = BSP_LIQUID_DETECT_PIN | BSP_URINE_DETECT_PIN;
        HAL_GPIO_Init(BSP_LIQUID_DETECT_PORT, &gpio);

        /* 2c. Additional GPIO outputs (硬件工程师确认 2026-04-16) */
        __HAL_RCC_GPIOE_CLK_ENABLE();  /* Explicit, even though relay_driver already enables */

        gpio.Mode = GPIO_MODE_OUTPUT_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;

        /* PE10-PE13: U32照明灯4色 (高电平亮) + PE7: 推拉电磁铁(内/外循环) */
        gpio.Pin = BSP_LIGHT_LED1_PIN | BSP_LIGHT_LED2_PIN |
                   BSP_LIGHT_LED3_PIN | BSP_LIGHT_LED4_PIN |
                   BSP_MAGNET_PIN;
        HAL_GPIO_Init(GPIOE, &gpio);
        HAL_GPIO_WritePin(GPIOE,
                          BSP_LIGHT_LED1_PIN | BSP_LIGHT_LED2_PIN |
                          BSP_LIGHT_LED3_PIN | BSP_LIGHT_LED4_PIN |
                          BSP_MAGNET_PIN,
                          GPIO_PIN_RESET);

        /* PB5: GY-IO制氧机信号 + PB6: ZY-IO(空置,输出低防漂浮) + PB12: 压缩机指示灯 */
        gpio.Pin = BSP_GY_PIN | GPIO_PIN_6 | BSP_COMPRESSOR_LED_PIN;
        HAL_GPIO_Init(GPIOB, &gpio);
        HAL_GPIO_WritePin(GPIOB, BSP_GY_PIN | GPIO_PIN_6 | BSP_COMPRESSOR_LED_PIN, GPIO_PIN_RESET);
    }

    /* 3. Initialize central data hub with power-on defaults */
    app_data_init();

    /* 4. Load persistent data from flash */
    if (flash_storage_init()) {
        app_data_get()->system.total_runtime_min = flash_storage_get_runtime();
    }

    /* 5. Initialize sensor drivers */
    co2_sensor_init();
    o2_sensor_init();
    jfc103_sensor_init();

    /* 6. Initialize protocol handlers */
    ipad_protocol_init();
    screen_protocol_init();

    /* 7. Create all FreeRTOS tasks */
    tasks_create_all();

    /* 8. Start scheduler — does not return */
    vTaskStartScheduler();

    for (;;) {}
}

/* ========================================================================= */
/*  UART RX Callback — ISR context                                           */
/*  Sensor bytes: parse directly (private static buffers, ISR-safe)          */
/*  Protocol bytes: enqueue for task-context dispatch (writes shared data)   */
/* ========================================================================= */
void uart_rx_callback(UartChannel_t ch, uint8_t byte)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    switch (ch) {
    case UART_CH_SCREEN:
        /* Enqueue for CommScreenTask — writes to shared AppData */
        xQueueSendFromISR(g_screen_rx_queue, &byte, &xHigherPriorityTaskWoken);
        break;
    case UART_CH_IPAD:
        /* Enqueue for CommIPadTask — writes to shared AppData */
        xQueueSendFromISR(g_ipad_rx_queue, &byte, &xHigherPriorityTaskWoken);
        break;
    case UART_CH_CO2:
        /* ISR-safe: writes to co2_sensor static buffer only */
        co2_sensor_rx_byte(byte);
        break;
    case UART_CH_O2:
        /* ISR-safe: writes to o2_sensor static buffer only */
        o2_sensor_rx_byte(byte);
        break;
    case UART_CH_JFC103:
        /* ISR-safe: writes to jfc103_sensor static buffer only */
        jfc103_sensor_rx_byte(byte);
        break;
    default:
        break;
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
