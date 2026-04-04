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

void app_init(void)
{
    /* 1. Create RX queues BEFORE initializing UART (which enables interrupts) */
    g_ipad_rx_queue = xQueueCreate(UART_RX_QUEUE_LEN, sizeof(uint8_t));
    g_screen_rx_queue = xQueueCreate(UART_RX_QUEUE_LEN, sizeof(uint8_t));

    /* 2. Initialize hardware drivers */
    uart_driver_init();
    adc_driver_init();
    pwm_driver_init();
    relay_driver_init();

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
