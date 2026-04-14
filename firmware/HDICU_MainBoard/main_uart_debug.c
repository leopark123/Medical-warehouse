/**
 * @file    main_uart_debug.c
 * @brief   UART + Clock diagnostic — debug CN3(UART1)/HSE issues
 *
 * Outputs on BOTH UART1(PA9) and UART4(PC10) simultaneously.
 * Also enables MCO on PA8 to output system clock for logic analyzer.
 *
 * Probe points:
 *   PA8  = MCO (system clock output, divided)
 *   PA9  = UART1 TX (MCU side, 3.3V, BEFORE CN3 level shifter)
 *   CN3 TX = UART1 TX (AFTER level shifter, should be 5V)
 *   CN16 TX = UART4 TX (AFTER level shifter, confirmed working)
 */
#include "stm32f1xx_hal.h"
#include <string.h>

void SysTick_Handler(void) { HAL_IncTick(); }

static UART_HandleTypeDef huart1, huart4;

static void send_both(const char *s)
{
    uint16_t len = strlen(s);
    HAL_UART_Transmit(&huart1, (uint8_t *)s, len, 200);
    HAL_UART_Transmit(&huart4, (uint8_t *)s, len, 200);
}

static void send_hex(const char *label, uint32_t val)
{
    char buf[50]; char *p = buf;
    while (*label) *p++ = *label++;
    const char hex[] = "0123456789ABCDEF";
    *p++ = '0'; *p++ = 'x';
    for (int i = 7; i >= 0; i--) *p++ = hex[(val >> (i*4)) & 0xF];
    *p++ = '\r'; *p++ = '\n'; *p = 0;
    send_both(buf);
}

int main(void)
{
    HAL_Init();

    /* Release PB3/PB4/PA15 from JTAG to GPIO, keep SWD */
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    /* ---- Clock config: try HSE+PLL, fallback HSI ---- */
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* Try HSE first */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;

    uint8_t hse_ok = 0;
    if (HAL_RCC_OscConfig(&osc) == HAL_OK) {
        clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                        RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
        clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
        clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
        clk.APB1CLKDivider = RCC_HCLK_DIV2;
        clk.APB2CLKDivider = RCC_HCLK_DIV1;
        if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) == HAL_OK) {
            hse_ok = 1;
        }
    }

    if (!hse_ok) {
        /* Fallback: HSI 8MHz */
        HAL_RCC_DeInit();
        HAL_Init();
    }

    SystemCoreClockUpdate();

    /* ---- MCO output on PA8: system clock / 2 ---- */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* MCO = SYSCLK (72MHz or 8MHz). Logic analyzer at 24MHz can see 8MHz but not 72MHz.
     * If HSE+PLL works, MCO=72MHz → analyzer sees HIGH or aliased garbage.
     * If HSI only, MCO=8MHz → analyzer might see rough 8MHz toggling. */
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_SYSCLK, RCC_MCODIV_1);

    /* ---- UART1 (PA9) ---- */
    __HAL_RCC_USART1_CLK_ENABLE();
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

    /* ---- UART4 (PC10) ---- */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
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

    /* ---- Diagnostic output ---- */
    send_both("\r\n=== UART/Clock Diagnostic ===\r\n");

    if (hse_ok) {
        send_both("[DIAG] HSE+PLL = OK (72MHz)\r\n");
    } else {
        send_both("[DIAG] HSE FAILED -> HSI 8MHz fallback\r\n");
    }

    send_hex("[DIAG] SystemCoreClock=", SystemCoreClock);
    send_hex("[DIAG] RCC->CR=", RCC->CR);
    send_hex("[DIAG] RCC->CFGR=", RCC->CFGR);

    /* Check HSE ready bit */
    if (RCC->CR & RCC_CR_HSERDY) {
        send_both("[DIAG] HSE RDY bit = SET (crystal oscillating)\r\n");
    } else {
        send_both("[DIAG] HSE RDY bit = CLEAR (crystal NOT oscillating)\r\n");
    }

    /* Check PLL ready bit */
    if (RCC->CR & RCC_CR_PLLRDY) {
        send_both("[DIAG] PLL RDY bit = SET\r\n");
    } else {
        send_both("[DIAG] PLL RDY bit = CLEAR\r\n");
    }

    send_both("[DIAG] PA8=MCO output (probe with logic analyzer)\r\n");
    send_both("[DIAG] PA9=UART1 TX (probe MCU side before level shifter)\r\n");
    send_both("[DIAG] Now sending 'U' every 500ms on both UART1+UART4...\r\n");

    /* Continuous output for probing */
    uint32_t cnt = 0;
    while (1) {
        char buf[40];
        char *p = buf;
        *p++ = '['; *p++ = 'U'; *p++ = ']'; *p++ = ' ';
        /* Simple counter */
        uint32_t v = cnt;
        char tmp[10]; int tl = 0;
        if (v == 0) tmp[tl++] = '0';
        else while (v > 0) { tmp[tl++] = '0' + (v % 10); v /= 10; }
        while (tl > 0) *p++ = tmp[--tl];
        *p++ = '\r'; *p++ = '\n'; *p = 0;

        send_both(buf);
        cnt++;
        HAL_Delay(500);
    }
}
