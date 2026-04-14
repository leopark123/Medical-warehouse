/**
 * @file    uart_driver.c
 * @brief   UART driver — HAL-based init and byte-level RX interrupt routing
 */

#include "uart_driver.h"
#include "bsp_config.h"
#include "stm32f1xx_hal.h"

/* Per-channel HAL handles */
static UART_HandleTypeDef s_huart[UART_CH_COUNT];
static uint8_t s_rx_byte[UART_CH_COUNT];   /* Single-byte RX buffers for IT mode */

/* Diagnostic counters — accessible via uart_driver_get_diag() */
volatile uint32_t g_uart_rx_ok[UART_CH_COUNT];
volatile uint32_t g_uart_rx_err[UART_CH_COUNT];
volatile uint32_t g_uart_rx_rearm_fail[UART_CH_COUNT];
volatile uint32_t g_uart_last_errcode[UART_CH_COUNT];

/* Deferred recovery flag — ISR sets, task clears (B2 fix) */
volatile uint8_t g_uart_screen_recover_pending;

/* UART configuration table */
static const struct {
    USART_TypeDef   *instance;
    uint32_t        baudrate;
    GPIO_TypeDef    *tx_port;
    uint16_t        tx_pin;
    GPIO_TypeDef    *rx_port;
    uint16_t        rx_pin;
} s_uart_config[UART_CH_COUNT] = {
    [UART_CH_SCREEN] = { BSP_UART_SCREEN, BSP_UART_SCREEN_BAUD,
                         BSP_UART_SCREEN_TX_PORT, BSP_UART_SCREEN_TX_PIN,
                         BSP_UART_SCREEN_RX_PORT, BSP_UART_SCREEN_RX_PIN },
    [UART_CH_IPAD]   = { BSP_UART_IPAD, BSP_UART_IPAD_BAUD,
                         BSP_UART_IPAD_TX_PORT, BSP_UART_IPAD_TX_PIN,
                         BSP_UART_IPAD_RX_PORT, BSP_UART_IPAD_RX_PIN },
    [UART_CH_CO2]    = { BSP_UART_CO2, BSP_UART_CO2_BAUD,
                         BSP_UART_CO2_TX_PORT, BSP_UART_CO2_TX_PIN,
                         BSP_UART_CO2_RX_PORT, BSP_UART_CO2_RX_PIN },
    [UART_CH_O2]     = { BSP_UART_O2, BSP_UART_O2_BAUD,
                         BSP_UART_O2_TX_PORT, BSP_UART_O2_TX_PIN,
                         BSP_UART_O2_RX_PORT, BSP_UART_O2_RX_PIN },
    [UART_CH_JFC103] = { BSP_UART_JFC103, BSP_UART_JFC103_BAUD,
                         BSP_UART_JFC103_TX_PORT, BSP_UART_JFC103_TX_PIN,
                         BSP_UART_JFC103_RX_PORT, BSP_UART_JFC103_RX_PIN },
};

void uart_driver_init(void)
{
    /* Enable all needed GPIO and UART clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_UART5_CLK_ENABLE();

    for (int i = 0; i < UART_CH_COUNT; i++) {
        /* TX pin: AF push-pull */
        GPIO_InitTypeDef gpio = {0};
        gpio.Pin = s_uart_config[i].tx_pin;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(s_uart_config[i].tx_port, &gpio);

        /* RX pin: input floating */
        gpio.Pin = s_uart_config[i].rx_pin;
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(s_uart_config[i].rx_port, &gpio);

        /* UART init */
        s_huart[i].Instance = s_uart_config[i].instance;
        s_huart[i].Init.BaudRate = s_uart_config[i].baudrate;
        s_huart[i].Init.WordLength = UART_WORDLENGTH_8B;
        s_huart[i].Init.StopBits = UART_STOPBITS_1;
        s_huart[i].Init.Parity = UART_PARITY_NONE;
        s_huart[i].Init.Mode = UART_MODE_TX_RX;
        s_huart[i].Init.HwFlowCtl = UART_HWCONTROL_NONE;
        HAL_UART_Init(&s_huart[i]);

        /* DO NOT start RX interrupts here — scheduler not running yet.
         * Call uart_driver_start_rx() after vTaskStartScheduler(). */
    }

    /* Set NVIC priorities but DO NOT enable IRQs yet */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
    HAL_NVIC_SetPriority(UART4_IRQn, 5, 0);
    HAL_NVIC_SetPriority(UART5_IRQn, 5, 0);
}

/* Start UART RX interrupts — call AFTER FreeRTOS scheduler has started.
 * This prevents xQueueSendFromISR being called before scheduler is ready. */
void uart_driver_start_rx(void)
{
    for (int i = 0; i < UART_CH_COUNT; i++) {
        HAL_UART_Receive_IT(&s_huart[i], &s_rx_byte[i], 1);
    }
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
    HAL_NVIC_EnableIRQ(UART5_IRQn);
}

void uart_driver_send(UartChannel_t ch, const uint8_t *data, uint16_t len)
{
    if (ch >= UART_CH_COUNT) return;
    if (ch == UART_CH_SCREEN) {
        /* Bypass HAL for screen TX to avoid huart->Lock contention
         * with HAL_UART_Receive_IT in the RX callback (B1 fix). */
        USART_TypeDef *uart = s_uart_config[ch].instance;
        for (uint16_t i = 0; i < len; i++) {
            while (!(uart->SR & USART_SR_TXE)) {}
            uart->DR = data[i];
        }
        while (!(uart->SR & USART_SR_TC)) {}  /* Wait last byte complete */
        return;
    }
    HAL_UART_Transmit(&s_huart[ch], (uint8_t *)data, len, 100);
}

/* HAL RX complete callback — routes bytes to protocol/sensor parsers */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    for (int i = 0; i < UART_CH_COUNT; i++) {
        if (huart->Instance == s_huart[i].Instance) {
            g_uart_rx_ok[i]++;
            uart_rx_callback((UartChannel_t)i, s_rx_byte[i]);
            /* Re-arm single-byte receive */
            if (HAL_UART_Receive_IT(&s_huart[i], &s_rx_byte[i], 1) != HAL_OK) {
                g_uart_rx_rearm_fail[i]++;
                /* P1-2 fix: immediately flag recovery for screen channel */
                if (i == UART_CH_SCREEN) {
                    g_uart_screen_recover_pending = 1;
                }
            }
            return;
        }
    }
}

/* Error callback — recover from ORE/FE/NE/PE by clearing errors and re-arming RX */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    for (int i = 0; i < UART_CH_COUNT; i++) {
        if (huart->Instance == s_huart[i].Instance) {
            g_uart_rx_err[i]++;
            g_uart_last_errcode[i] = huart->ErrorCode;

            if (i == UART_CH_SCREEN) {
                /* Defer heavy recovery to task context (B2 fix).
                 * ISR only clears error flags and sets pending flag. */
                __HAL_UART_CLEAR_PEFLAG(&s_huart[i]);
                g_uart_screen_recover_pending = 1;
            } else {
                /* Light recovery for other channels */
                __HAL_UART_CLEAR_PEFLAG(&s_huart[i]);
                HAL_UART_Receive_IT(&s_huart[i], &s_rx_byte[i], 1);
            }
            return;
        }
    }
}

/* Default weak implementation — override in app layer */
__weak void uart_rx_callback(UartChannel_t ch, uint8_t byte)
{
    (void)ch;
    (void)byte;
}

/* Force-recover UART_CH_SCREEN — call from CommScreenTask (task context only).
 * Uses NVIC-level disable instead of __disable_irq to avoid blocking other UARTs (B3 fix). */
void uart_driver_recover_screen(void)
{
    int i = UART_CH_SCREEN;
    HAL_NVIC_DisableIRQ(USART1_IRQn);

    HAL_UART_AbortReceive_IT(&s_huart[i]);
    HAL_UART_DeInit(&s_huart[i]);

    s_huart[i].Instance = s_uart_config[i].instance;
    s_huart[i].Init.BaudRate = s_uart_config[i].baudrate;
    s_huart[i].Init.WordLength = UART_WORDLENGTH_8B;
    s_huart[i].Init.StopBits = UART_STOPBITS_1;
    s_huart[i].Init.Parity = UART_PARITY_NONE;
    s_huart[i].Init.Mode = UART_MODE_TX_RX;
    s_huart[i].Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&s_huart[i]);

    __HAL_UART_CLEAR_PEFLAG(&s_huart[i]);
    HAL_UART_Receive_IT(&s_huart[i], &s_rx_byte[i], 1);
    g_uart_screen_recover_pending = 0;
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/* ===== IRQ Handlers — forward to HAL ===== */
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&s_huart[UART_CH_SCREEN]); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&s_huart[UART_CH_IPAD]); }
void USART3_IRQHandler(void) { HAL_UART_IRQHandler(&s_huart[UART_CH_CO2]); }
void UART4_IRQHandler(void)  { HAL_UART_IRQHandler(&s_huart[UART_CH_O2]); }
void UART5_IRQHandler(void)  { HAL_UART_IRQHandler(&s_huart[UART_CH_JFC103]); }

/* ===== BSP transmit functions used by protocol/sensor modules ===== */
void bsp_uart_screen_send(const uint8_t *data, uint16_t len)
{
    uart_driver_send(UART_CH_SCREEN, data, len);
}

void bsp_uart_ipad_send(const uint8_t *data, uint16_t len)
{
    uart_driver_send(UART_CH_IPAD, data, len);
}

void bsp_uart_jfc103_send(const uint8_t *data, uint16_t len)
{
    uart_driver_send(UART_CH_JFC103, data, len);
}
