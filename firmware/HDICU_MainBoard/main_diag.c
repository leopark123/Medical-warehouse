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

static void test_clock_hse(void)
{
    uart_send("[DIAG] HSE/PLL test...\r\n");

    /* Try starting HSE */
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;

    HAL_StatusTypeDef status = HAL_RCC_OscConfig(&osc);

    if (status == HAL_OK) {
        uart_send("  HSE+PLL: OK\r\n");

        /* Try switching SYSCLK to PLL */
        RCC_ClkInitTypeDef clk = {0};
        clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                        RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
        clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
        clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
        clk.APB1CLKDivider = RCC_HCLK_DIV2;
        clk.APB2CLKDivider = RCC_HCLK_DIV1;

        if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) == HAL_OK) {
            uart_send("  SYSCLK->PLL: OK\r\n");
            /* Re-init UART1 with new clock */
            UART1_Init();
        } else {
            uart_send("  SYSCLK switch: FAIL\r\n");
        }
    } else {
        uart_send("  HSE: FAIL (no crystal?)\r\n");
    }

    /* Print clock registers */
    char buf[60]; char *p;
    uint32_t rcc_cr = RCC->CR;
    uint32_t rcc_cfgr = RCC->CFGR;
    uint32_t sysclk = HAL_RCC_GetSysClockFreq();
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();

    p = buf;
    const char *s1 = "  HSEON="; while(*s1) *p++=*s1++;
    *p++ = '0' + ((rcc_cr >> 16) & 1);
    const char *s2 = " HSERDY="; while(*s2) *p++=*s2++;
    *p++ = '0' + ((rcc_cr >> 17) & 1);
    const char *s3 = " PLLON="; while(*s3) *p++=*s3++;
    *p++ = '0' + ((rcc_cr >> 24) & 1);
    const char *s4 = " PLLRDY="; while(*s4) *p++=*s4++;
    *p++ = '0' + ((rcc_cr >> 25) & 1);
    *p++ = '\r'; *p++ = '\n';
    HAL_UART_Transmit(&huart1, (uint8_t*)buf, p-buf, 100);

    p = buf;
    const char *s5 = "  SWS="; while(*s5) *p++=*s5++;
    uint8_t sws = (rcc_cfgr >> 2) & 3;
    *p++ = '0' + sws;
    const char *swn[] = {" (HSI)", " (HSE)", " (PLL)", " (?)"};
    const char *ss = swn[sws]; while(*ss) *p++=*ss++;
    *p++ = '\r'; *p++ = '\n';
    HAL_UART_Transmit(&huart1, (uint8_t*)buf, p-buf, 100);

    p = buf;
    const char *s6 = "  SYSCLK="; while(*s6) *p++=*s6++;
    { char tmp[12]; int tl=0; uint32_t v=sysclk/1000000;
      if(v==0) tmp[tl++]='0'; else while(v>0){tmp[tl++]='0'+(v%10);v/=10;}
      while(tl>0) *p++=tmp[--tl]; }
    const char *s7 = "MHz PCLK1="; while(*s7) *p++=*s7++;
    { char tmp[12]; int tl=0; uint32_t v=pclk1/1000000;
      if(v==0) tmp[tl++]='0'; else while(v>0){tmp[tl++]='0'+(v%10);v/=10;}
      while(tl>0) *p++=tmp[--tl]; }
    const char *s8 = "MHz\r\n"; while(*s8) *p++=*s8++;
    HAL_UART_Transmit(&huart1, (uint8_t*)buf, p-buf, 100);
}

int main(void)
{
    HAL_Init();
    /* Start on HSI 8MHz, then optionally try HSE in test_clock_hse() */
    UART1_Init();

    uart_send("\r\n\r\n");
    uart_send("================================\r\n");
    uart_send("[DIAG] HDICU Diagnostic Start\r\n");
    uart_send("[DIAG] UART1: 115200 on PA9\r\n");
    uart_send("================================\r\n");

    /* Test clock first */
    test_clock_hse();

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
