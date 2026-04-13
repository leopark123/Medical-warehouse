/**
 * @file    main_test.c
 * @brief   Minimal UART1 test — uses PA9 (TX1 on M100Z-M3 board)
 *          No FreeRTOS, no peripherals, just serial output on UART1
 */
#include "stm32f1xx_hal.h"
#include <string.h>

/* SysTick handler — required for HAL_Delay */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

static UART_HandleTypeDef huart1;

static void SystemClock_Config(void)
{
    /* Run on HSI 8MHz — no HSE/PLL needed for this test */
}

static void UART1_Init(void)
{
    /* Enable clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    /* PA9 = UART1_TX (AF push-pull) — this is TX1 on M100Z-M3 board */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* UART1: 115200, 8N1 */
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart1);
}

static void send_str(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 100);
}

int main(void)
{
    HAL_Init();

    /* Release PB3/PB4/PA15 from JTAG to GPIO, keep SWD */
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    SystemClock_Config();
    UART1_Init();

    uint32_t count = 0;
    while (1) {
        char buf[40];
        char *p = buf;

        const char *hdr = "[TEST] cnt=";
        while (*hdr) *p++ = *hdr++;

        char tmp[12];
        int tlen = 0;
        uint32_t v = count;
        if (v == 0) { tmp[tlen++] = '0'; }
        else { while (v > 0) { tmp[tlen++] = '0' + (v % 10); v /= 10; } }
        while (tlen > 0) *p++ = tmp[--tlen];

        *p++ = '\r';
        *p++ = '\n';

        HAL_UART_Transmit(&huart1, (uint8_t *)buf, p - buf, 100);
        count++;

        HAL_Delay(500);
    }
}
