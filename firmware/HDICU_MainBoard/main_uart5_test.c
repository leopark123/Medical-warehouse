/**
 * @file    main_uart5_test.c
 * @brief   UART5 (PC12/PD2) continuous output test
 *          Sends "UART5 OK\r\n" every 500ms on UART5 (38400).
 *          Also sends same on UART4 (CN16, 9600) for USB-TTL monitoring.
 *          Probe CN1 pins with logic analyzer at 38400 to find UART5 TX.
 */
#include "stm32f1xx_hal.h"
#include <string.h>

void SysTick_Handler(void) { HAL_IncTick(); }

static UART_HandleTypeDef huart4, huart5;

int main(void)
{
    HAL_Init();

    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    /* GPIO clocks */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_UART5_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* UART4 TX = PC10 (CN16, for USB-TTL monitoring) */
    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* UART5 TX = PC12 */
    gpio.Pin = GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* UART5 RX = PD2 (input) */
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOD, &gpio);

    /* Init UART4 at 9600 (CN16 for monitoring) */
    huart4.Instance = UART4;
    huart4.Init.BaudRate = 9600;
    huart4.Init.WordLength = UART_WORDLENGTH_8B;
    huart4.Init.StopBits = UART_STOPBITS_1;
    huart4.Init.Parity = UART_PARITY_NONE;
    huart4.Init.Mode = UART_MODE_TX;
    huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart4);

    /* Init UART5 at 38400 (JFC103 port) */
    huart5.Instance = UART5;
    huart5.Init.BaudRate = 38400;
    huart5.Init.WordLength = UART_WORDLENGTH_8B;
    huart5.Init.StopBits = UART_STOPBITS_1;
    huart5.Init.Parity = UART_PARITY_NONE;
    huart5.Init.Mode = UART_MODE_TX_RX;
    huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart5);

    const char *msg4 = "[UART4/CN16] UART5 test running...\r\n";
    const char *msg5 = "[UART5] HELLO FROM PC12\r\n";
    const char *start_cmd = "\x8A";

    /* Send 0x8A start command on UART5 */
    HAL_UART_Transmit(&huart5, (uint8_t *)start_cmd, 1, 100);

    HAL_UART_Transmit(&huart4, (uint8_t *)msg4, strlen(msg4), 200);

    uint32_t cnt = 0;
    while (1) {
        /* Send on UART5 (38400) - probe CN1 pins to find this */
        char buf5[40];
        int len5 = 0;
        const char *p = "[U5] ";
        while (*p) buf5[len5++] = *p++;
        /* simple counter */
        uint32_t v = cnt;
        char tmp[10]; int tl = 0;
        if (v == 0) tmp[tl++] = '0';
        else while (v > 0) { tmp[tl++] = '0' + (v % 10); v /= 10; }
        while (tl > 0) buf5[len5++] = tmp[--tl];
        buf5[len5++] = '\r'; buf5[len5++] = '\n';

        HAL_UART_Transmit(&huart5, (uint8_t *)buf5, len5, 200);

        /* Also send status on UART4 (9600) for USB-TTL */
        HAL_UART_Transmit(&huart4, (uint8_t *)buf5, len5, 200);

        cnt++;
        HAL_Delay(500);
    }
}
