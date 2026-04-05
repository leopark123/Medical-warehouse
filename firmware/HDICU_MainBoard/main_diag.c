/**
 * @file    main_diag.c
 * @brief   Diagnostic firmware — no FreeRTOS, tests each peripheral one by one
 *          Output via UART1 (PA9 = P5 TX1) at 115200
 *          Runs on HSI 8MHz (no PLL)
 */
#include "stm32f1xx_hal.h"
#include <string.h>

void SysTick_Handler(void) { HAL_IncTick(); }

static UART_HandleTypeDef huart1;

static void uart_send(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 100);
}

static void UART1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart1);
}

static void test_adc(void)
{
    uart_send("[DIAG] ADC init...");
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOA, &gpio);

    ADC_HandleTypeDef hadc = {0};
    hadc.Instance = ADC1;
    hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.NbrOfConversion = 1;

    if (HAL_ADC_Init(&hadc) == HAL_OK) {
        uart_send("OK\r\n");
    } else {
        uart_send("FAIL\r\n");
    }
}

static void test_timer(void)
{
    uart_send("[DIAG] TIM6 init...");
    __HAL_RCC_TIM6_CLK_ENABLE();

    TIM_HandleTypeDef htim = {0};
    htim.Instance = TIM6;
    htim.Init.Prescaler = 71;
    htim.Init.Period = 99;
    htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(&htim) == HAL_OK) {
        uart_send("OK\r\n");
    } else {
        uart_send("FAIL\r\n");
    }
    /* Don't start interrupt - just test init */
}

static void test_gpio(void)
{
    uart_send("[DIAG] GPIO PE2/PE3/PE4...");
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &gpio);
    uart_send("OK\r\n");
}

int main(void)
{
    HAL_Init();
    /* No clock config — stay on HSI 8MHz */
    UART1_Init();

    uart_send("\r\n\r\n");
    uart_send("================================\r\n");
    uart_send("[DIAG] HDICU Diagnostic Start\r\n");
    uart_send("[DIAG] Clock: HSI 8MHz\r\n");
    uart_send("[DIAG] UART1: 115200 on PA9\r\n");
    uart_send("================================\r\n");

    /* Test each peripheral */
    test_gpio();
    test_adc();
    test_timer();

    uart_send("[DIAG] All peripheral tests done\r\n");
    uart_send("[DIAG] Starting heartbeat...\r\n\r\n");

    uint32_t count = 0;
    while (1) {
        char buf[50];
        char *p = buf;
        const char *hdr = "[DIAG] alive cnt=";
        while (*hdr) *p++ = *hdr++;

        char tmp[12]; int tlen = 0;
        uint32_t v = count;
        if (v == 0) tmp[tlen++] = '0';
        else while (v > 0) { tmp[tlen++] = '0' + (v % 10); v /= 10; }
        while (tlen > 0) *p++ = tmp[--tlen];
        *p++ = '\r'; *p++ = '\n';

        HAL_UART_Transmit(&huart1, (uint8_t *)buf, p - buf, 100);
        count++;
        HAL_Delay(1000);
    }
}
