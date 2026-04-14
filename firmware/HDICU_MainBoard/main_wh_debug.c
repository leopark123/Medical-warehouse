/**
 * @file    main_wh_debug.c
 * @brief   WH(PB4) relay debug — is it software or hardware?
 *
 * Test sequence:
 * 1. Hold PB4 HIGH for 10s → measure PB4 with multimeter (expect 3.3V)
 * 2. Read GPIOB->ODR and print → confirm bit4 is set
 * 3. Turn OFF PB4, turn ON PB7 (O2 relay, confirmed working) → U16 should click
 * 4. Turn OFF PB7, turn ON PB4 again → does anything respond?
 * 5. Swap test: drive PB4 HIGH while measuring U33 input pin
 *
 * Output via UART4 (CN16) at 115200.
 */
#include "stm32f1xx_hal.h"
#include <string.h>

void SysTick_Handler(void) { HAL_IncTick(); }

static UART_HandleTypeDef huart4;

static void uart_send(const char *s)
{
    HAL_UART_Transmit(&huart4, (uint8_t *)s, strlen(s), 200);
}

static void uart_hex(const char *label, uint32_t val)
{
    char buf[60]; char *p = buf;
    while (*label) *p++ = *label++;
    const char hex[] = "0123456789ABCDEF";
    *p++ = '0'; *p++ = 'x';
    for (int i = 7; i >= 0; i--) *p++ = hex[(val >> (i*4)) & 0xF];
    *p++ = '\r'; *p++ = '\n'; *p = 0;
    uart_send(buf);
}

int main(void)
{
    HAL_Init();

    /* Release PB3/PB4/PA15 from JTAG to GPIO, keep SWD */
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    /* UART4 on PC10 */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);

    huart4.Instance = UART4;
    huart4.Init.BaudRate = 115200;
    huart4.Init.WordLength = UART_WORDLENGTH_8B;
    huart4.Init.StopBits = UART_STOPBITS_1;
    huart4.Init.Parity = UART_PARITY_NONE;
    huart4.Init.Mode = UART_MODE_TX;
    huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart4);

    /* Init PB4 (WH) and PB7 (O2, known good) as outputs */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);

    uart_send("\r\n=== WH(PB4) Debug ===\r\n\r\n");

    /* ---- TEST 1: PB4 HIGH, measure voltage ---- */
    uart_send("[TEST1] PB4 = HIGH now. Hold 10 seconds.\r\n");
    uart_send("        Measure PB4 with multimeter. Expect 3.3V.\r\n");
    uart_send("        If relay near U30 clicks -> hardware OK.\r\n");
    uart_send("        If no click -> measure U33 input pin.\r\n");
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_Delay(1000);

    /* Read and print ODR */
    uint32_t odr = GPIOB->ODR;
    uart_hex("[TEST1] GPIOB->ODR = ", odr);
    if (odr & GPIO_PIN_4) {
        uart_send("[TEST1] ODR bit4 = SET -> software output is HIGH\r\n");
    } else {
        uart_send("[TEST1] ODR bit4 = CLEAR -> SOFTWARE BUG!\r\n");
    }

    /* Also read IDR to see actual pin level */
    uint32_t idr = GPIOB->IDR;
    uart_hex("[TEST1] GPIOB->IDR = ", idr);
    if (idr & GPIO_PIN_4) {
        uart_send("[TEST1] IDR bit4 = HIGH -> pin physically HIGH\r\n");
    } else {
        uart_send("[TEST1] IDR bit4 = LOW -> pin NOT reaching HIGH! (short/load?)\r\n");
    }

    HAL_Delay(9000);  /* Total 10s hold */

    /* ---- TEST 2: PB4 OFF, PB7 ON (O2 relay, known good) ---- */
    uart_send("\r\n[TEST2] PB4 OFF, PB7 (O2) ON. U16 should click.\r\n");
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    uart_send("        PB7 = HIGH. Listen for U16 click...\r\n");
    HAL_Delay(3000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    uart_send("        PB7 = OFF.\r\n");

    /* ---- TEST 3: PB4 ON again ---- */
    uart_send("\r\n[TEST3] PB4 ON again. Does ANYTHING respond?\r\n");
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
    uart_send("        PB4 = HIGH. Holding 5s...\r\n");

    odr = GPIOB->ODR;
    idr = GPIOB->IDR;
    uart_hex("        GPIOB->ODR = ", odr);
    uart_hex("        GPIOB->IDR = ", idr);

    HAL_Delay(5000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
    uart_send("        PB4 = OFF.\r\n");

    /* ---- TEST 4: Toggle PB4 rapidly 10 times ---- */
    uart_send("\r\n[TEST4] PB4 rapid toggle x10 (listen carefully)\r\n");
    for (int i = 0; i < 10; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
        HAL_Delay(500);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
        HAL_Delay(500);
    }

    uart_send("\r\n=== WH DEBUG COMPLETE ===\r\n");
    uart_send("If TEST1 shows ODR+IDR bit4=HIGH but no click:\r\n");
    uart_send("  -> HARDWARE issue: PB4 trace to U33 or U33 to relay\r\n");
    uart_send("If TEST1 shows ODR bit4=HIGH but IDR bit4=LOW:\r\n");
    uart_send("  -> PB4 pin is shorted or overloaded\r\n");
    uart_send("If TEST2 U16 clicked:\r\n");
    uart_send("  -> GPIOB port is working, PB4 path is the problem\r\n");

    while (1) { HAL_Delay(1000); }
}
