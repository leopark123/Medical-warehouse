/**
 * @file    main_pcb_test.c
 * @brief   PCB Hardware Test Firmware — tests GPIO/Relay/PWM/LED/Buzzer/ADC
 *          No FreeRTOS. Outputs results via UART1 (PA9, 115200).
 *          Automatically cycles through all hardware tests.
 *
 *          Build: compile with same HAL/CMSIS as main firmware
 *          Flash: JFlash Lite → pcb_test.bin at 0x08000000
 *          Monitor: logic analyzer or USB-TTL on PA9 (UART1_TX)
 */
#include "stm32f1xx_hal.h"
#include <string.h>

void SysTick_Handler(void) { HAL_IncTick(); }

static UART_HandleTypeDef huart1;

/* ========== Helpers ========== */

static void uart_send(const char *s)
{
    /* Output on both CN1(UART1) and CN16(UART4) */
    uint16_t len = strlen(s);
    HAL_UART_Transmit(&huart1, (uint8_t *)s, len, 200);
    HAL_UART_Transmit(&huart4, (uint8_t *)s, len, 200);
}

static void uart_send_hex32(const char *label, uint32_t val)
{
    char buf[50]; char *p = buf;
    while (*label) *p++ = *label++;
    const char hex[] = "0123456789ABCDEF";
    *p++ = '0'; *p++ = 'x';
    for (int i = 7; i >= 0; i--) *p++ = hex[(val >> (i*4)) & 0xF];
    *p++ = '\r'; *p++ = '\n';
    uint16_t len = p - buf;
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 200);
    HAL_UART_Transmit(&huart4, (uint8_t *)buf, len, 200);
}

static void uart_send_int(const char *label, int32_t val)
{
    char buf[50]; char *p = buf;
    while (*label) *p++ = *label++;
    if (val < 0) { *p++ = '-'; val = -val; }
    char tmp[12]; int tl = 0;
    if (val == 0) tmp[tl++] = '0';
    else while (val > 0) { tmp[tl++] = '0' + (val % 10); val /= 10; }
    while (tl > 0) *p++ = tmp[--tl];
    *p++ = '\r'; *p++ = '\n';
    uint16_t len2 = p - buf;
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, len2, 200);
    HAL_UART_Transmit(&huart4, (uint8_t *)buf, len2, 200);
}

/* ========== Init ========== */

static UART_HandleTypeDef huart4;

static void UART1_Init(void)
{
    /* Output on BOTH UART1(CN1/PA9) and UART4(CN16/PC10) simultaneously.
     * CN16 confirmed working via USB-TTL. CN1 for screen board verification. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* UART1 TX = PA9 (CN1) */
    gpio.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* UART4 TX = PC10 (CN16) */
    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* Init UART1 (CN1) */
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart1);

    /* Init UART4 (CN16) */
    huart4.Instance = UART4;
    huart4.Init.BaudRate = 115200;
    huart4.Init.WordLength = UART_WORDLENGTH_8B;
    huart4.Init.StopBits = UART_STOPBITS_1;
    huart4.Init.Parity = UART_PARITY_NONE;
    huart4.Init.Mode = UART_MODE_TX;
    huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart4);
}

/* ========== Tests ========== */

/* --- Relay GPIO Test --- */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    const char *name;
} RelayTestEntry;

static const RelayTestEntry relay_tests[] = {
    { GPIOE, GPIO_PIN_1,  "Relay0 PTC(PE1)" },
    { GPIOE, GPIO_PIN_0,  "Relay1 JIARE(PE0)" },
    { GPIOB, GPIO_PIN_9,  "Relay2 RED(PB9)" },
    { GPIOB, GPIO_PIN_8,  "Relay3 ZIY(PB8)" },
    { GPIOB, GPIO_PIN_7,  "Relay4 O2(PB7)" },
    { GPIOE, GPIO_PIN_4,  "Relay5 JIASHI(PE4)" },
    { GPIOE, GPIO_PIN_3,  "Relay6 FENGJI(PE3)" },
    { GPIOE, GPIO_PIN_2,  "Relay7 YASUO(PE2)" },
    { GPIOB, GPIO_PIN_4,  "Relay8 WH(PB4)" },
};

static void test_relays(void)
{
    uart_send("\r\n[PCB] === Relay GPIO Test ===\r\n");
    uart_send("[PCB] Each relay ON 2s, OFF 1s. Listen for click.\r\n\r\n");

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    for (int i = 0; i < 9; i++) {
        gpio.Pin = relay_tests[i].pin;
        HAL_GPIO_Init(relay_tests[i].port, &gpio);
        HAL_GPIO_WritePin(relay_tests[i].port, relay_tests[i].pin, GPIO_PIN_RESET);
    }

    for (int i = 0; i < 9; i++) {
        uart_send("[PCB] ");
        uart_send(relay_tests[i].name);
        uart_send(" -> ON\r\n");
        HAL_GPIO_WritePin(relay_tests[i].port, relay_tests[i].pin, GPIO_PIN_SET);
        HAL_Delay(2000);

        HAL_GPIO_WritePin(relay_tests[i].port, relay_tests[i].pin, GPIO_PIN_RESET);
        uart_send("[PCB] ");
        uart_send(relay_tests[i].name);
        uart_send(" -> OFF\r\n");
        HAL_Delay(1000);
    }

    uart_send("[PCB] Relay test complete.\r\n");
}

/* --- Fan GPIO Test (ON/OFF) + PE9 PWM Test --- */
static void test_fan_gpio(void)
{
    uart_send("\r\n[PCB] === Fan GPIO Test ===\r\n");
    uart_send("[PCB] PE5(NEI)=ON/OFF, PE6(PTC_EN)=ON/OFF, PC13(NEI2)=ON/OFF, PE9(PTC_PWM)=PWM\r\n\r\n");

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;

    /* PE5 + PE6 + PE9 on GPIOE */
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOE, &gpio);

    /* PC13 on GPIOC (low speed) */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* Part 1: ON/OFF toggle test for PE5/PE6/PC13 (6 cycles, 1Hz) */
    uart_send("[PCB] --- ON/OFF test: PE5/PE6/PC13 toggle 1Hz x6 ---\r\n");
    for (int cycle = 0; cycle < 6; cycle++) {
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        uart_send("[PCB] PE5/PE6/PC13 HIGH\r\n");
        HAL_Delay(500);

        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        uart_send("[PCB] PE5/PE6/PC13 LOW\r\n");
        HAL_Delay(500);
    }

    /* Part 2: PE9 PWM test — software toggle at ~1kHz for 3s (scope visible) */
    uart_send("[PCB] --- PE9 PWM test: ~1kHz 50%% duty for 3s ---\r\n");
    uart_send("[PCB] Connect scope to PE9 now!\r\n");
    HAL_Delay(1000);  /* Give user time to connect scope */
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 3000) {
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);
        for (volatile int i = 0; i < 100; i++) {}  /* ~500us delay */
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);
        for (volatile int i = 0; i < 100; i++) {}  /* ~500us delay */
    }
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);

    /* Part 3: PE6+PE9 联动测试 — PE6=HIGH + PE9=PWM 3s */
    uart_send("[PCB] --- PE6+PE9 combo: PE6=ON + PE9=PWM for 3s ---\r\n");
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_SET);
    start = HAL_GetTick();
    while (HAL_GetTick() - start < 3000) {
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);
        for (volatile int i = 0; i < 100; i++) {}
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);
        for (volatile int i = 0; i < 100; i++) {}
    }
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);

    uart_send("[PCB] Fan GPIO test complete.\r\n");
}

/* --- LED Test --- */
static void test_leds(void)
{
    uart_send("\r\n[PCB] === LED Test (PB1/PB0/PC5) ===\r\n");

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* HULI1=PB1, HULI2=PB0 on GPIOB */
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* HULI3=PC5 on GPIOC */
    gpio.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* Test HULI1 (PB1) */
    uart_send("[PCB] HULI1(PB1) ON\r\n");
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
    HAL_Delay(1000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
    uart_send("[PCB] HULI1(PB1) OFF\r\n");
    HAL_Delay(500);

    /* Test HULI2 (PB0) */
    uart_send("[PCB] HULI2(PB0) ON\r\n");
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_Delay(1000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    uart_send("[PCB] HULI2(PB0) OFF\r\n");
    HAL_Delay(500);

    /* Test HULI3 (PC5) */
    uart_send("[PCB] HULI3(PC5) ON\r\n");
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_SET);
    HAL_Delay(1000);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET);
    uart_send("[PCB] HULI3(PC5) OFF\r\n");
    HAL_Delay(500);

    uart_send("[PCB] LED test complete.\r\n");
}

/* --- Buzzer Test --- */
static void test_buzzer(void)
{
    uart_send("\r\n[PCB] === Buzzer Test (PB3) ===\r\n");

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOB, &gpio);

    uart_send("[PCB] Buzzer ON (1s)\r\n");
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
    HAL_Delay(1000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
    uart_send("[PCB] Buzzer OFF\r\n");
}

/* --- ADC Test --- */
static void test_adc(void)
{
    uart_send("\r\n[PCB] === ADC Test (PA0/PA1/PA4/PA5) ===\r\n");

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOA, &gpio);

    ADC_HandleTypeDef hadc = {0};
    hadc.Instance = ADC1;
    hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&hadc);
    HAL_ADCEx_Calibration_Start(&hadc);

    uint32_t channels[] = { ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_4, ADC_CHANNEL_5 };
    const char *names[] = { "PA0(CH0)", "PA1(CH1)", "PA4(CH4)", "PA5(CH5)" };

    for (int i = 0; i < 4; i++) {
        ADC_ChannelConfTypeDef cfg = {0};
        cfg.Channel = channels[i];
        cfg.Rank = ADC_REGULAR_RANK_1;
        cfg.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
        HAL_ADC_ConfigChannel(&hadc, &cfg);

        HAL_ADC_Start(&hadc);
        HAL_ADC_PollForConversion(&hadc, 10);
        uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc);
        HAL_ADC_Stop(&hadc);

        /* Calculate voltage: val/4095 * 3.3V */
        uint32_t mv = (uint32_t)val * 3300 / 4095;

        uart_send("[PCB] ");
        uart_send(names[i]);
        uart_send_int(": ADC=", val);
        uart_send_int("  mV=", mv);
    }

    uart_send("[PCB] ADC test complete.\r\n");
}

/* --- Input Test --- */
static void test_inputs(void)
{
    uart_send("\r\n[PCB] === Input Test (PB14/PB15) ===\r\n");

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);

    uint8_t pb14 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14);
    uint8_t pb15 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15);

    uart_send_int("[PCB] PB14(liquid)=", pb14);
    uart_send_int("[PCB] PB15(urine)=", pb15);
    uart_send("[PCB] Input test complete.\r\n");
}

/* ========== Main ========== */

int main(void)
{
    HAL_Init();

    /* Release PB3/PB4/PA15 from JTAG to GPIO, keep SWD */
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    UART1_Init();

    uart_send("\r\n\r\n");
    uart_send("============================================\r\n");
    uart_send("[PCB] HDICU-ZKB01A PCB Hardware Test\r\n");
    uart_send("[PCB] All results via UART1 PA9 115200\r\n");
    uart_send("============================================\r\n");

    /* Clock info */
    uart_send_int("[PCB] SystemCoreClock=", SystemCoreClock);

    /* Run all tests */
    test_leds();
    test_buzzer();
    test_adc();
    test_inputs();
    test_fan_gpio();
    test_relays();  /* Last because takes longest */

    uart_send("\r\n============================================\r\n");
    uart_send("[PCB] ALL TESTS COMPLETE\r\n");
    uart_send("[PCB] Check results above.\r\n");
    uart_send("[PCB] System entering idle heartbeat.\r\n");
    uart_send("============================================\r\n\r\n");

    uint32_t cnt = 0;
    while (1) {
        uart_send_int("[PCB] idle cnt=", cnt++);
        HAL_Delay(3000);
    }
}
