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
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 200);
}

static void uart_send_hex32(const char *label, uint32_t val)
{
    char buf[50]; char *p = buf;
    while (*label) *p++ = *label++;
    const char hex[] = "0123456789ABCDEF";
    *p++ = '0'; *p++ = 'x';
    for (int i = 7; i >= 0; i--) *p++ = hex[(val >> (i*4)) & 0xF];
    *p++ = '\r'; *p++ = '\n';
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, p - buf, 200);
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
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, p - buf, 200);
}

/* ========== Init ========== */

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

/* ========== Tests ========== */

/* --- Relay GPIO Test --- */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    const char *name;
} RelayTestEntry;

static const RelayTestEntry relay_tests[] = {
    { GPIOD, GPIO_PIN_13, "Relay0 PTC(PD13)" },
    { GPIOD, GPIO_PIN_14, "Relay1 JIARE(PD14)" },
    { GPIOD, GPIO_PIN_15, "Relay2 RED(PD15)" },
    { GPIOE, GPIO_PIN_7,  "Relay3 ZIY(PE7)" },
    { GPIOE, GPIO_PIN_8,  "Relay4 O2(PE8)" },
    { GPIOE, GPIO_PIN_9,  "Relay5 JIASHI(PE9)" },
    { GPIOE, GPIO_PIN_10, "Relay6 FENGJI(PE10)" },
    { GPIOE, GPIO_PIN_11, "Relay7 YASUO(PE11)" },
    { GPIOE, GPIO_PIN_12, "Relay8 WH(PE12)" },
};

static void test_relays(void)
{
    uart_send("\r\n[PCB] === Relay GPIO Test ===\r\n");
    uart_send("[PCB] Each relay ON 2s, OFF 1s. Listen for click.\r\n\r\n");

    __HAL_RCC_GPIOD_CLK_ENABLE();
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

/* --- PWM GPIO Test --- */
static void test_pwm_gpio(void)
{
    uart_send("\r\n[PCB] === PWM GPIO Test (PE2/PE3/PE4) ===\r\n");
    uart_send("[PCB] Toggle each pin at ~1Hz for 6s. Check with analyzer.\r\n\r\n");

    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    uint16_t pins[] = { GPIO_PIN_2, GPIO_PIN_3, GPIO_PIN_4 };
    const char *names[] = { "PE2(NEI)", "PE3(PTC)", "PE4(NEI2)" };

    for (int i = 0; i < 3; i++) {
        gpio.Pin = pins[i];
        HAL_GPIO_Init(GPIOE, &gpio);
    }

    for (int cycle = 0; cycle < 6; cycle++) {
        for (int i = 0; i < 3; i++) {
            HAL_GPIO_WritePin(GPIOE, pins[i], GPIO_PIN_SET);
        }
        uart_send("[PCB] PE2/PE3/PE4 HIGH\r\n");
        HAL_Delay(500);

        for (int i = 0; i < 3; i++) {
            HAL_GPIO_WritePin(GPIOE, pins[i], GPIO_PIN_RESET);
        }
        uart_send("[PCB] PE2/PE3/PE4 LOW\r\n");
        HAL_Delay(500);
    }

    uart_send("[PCB] PWM GPIO test complete.\r\n");
}

/* --- LED Test --- */
static void test_leds(void)
{
    uart_send("\r\n[PCB] === LED Test (PD10/PD11/PD12) ===\r\n");

    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOD, &gpio);

    for (int i = 0; i < 3; i++) {
        uint16_t pin = (i == 0) ? GPIO_PIN_10 : (i == 1) ? GPIO_PIN_11 : GPIO_PIN_12;
        const char *name = (i == 0) ? "HULI1(PD10)" : (i == 1) ? "HULI2(PD11)" : "HULI3(PD12)";

        uart_send("[PCB] ");
        uart_send(name);
        uart_send(" ON\r\n");
        HAL_GPIO_WritePin(GPIOD, pin, GPIO_PIN_SET);
        HAL_Delay(1000);

        HAL_GPIO_WritePin(GPIOD, pin, GPIO_PIN_RESET);
        uart_send("[PCB] ");
        uart_send(name);
        uart_send(" OFF\r\n");
        HAL_Delay(500);
    }

    uart_send("[PCB] LED test complete.\r\n");
}

/* --- Buzzer Test --- */
static void test_buzzer(void)
{
    uart_send("\r\n[PCB] === Buzzer Test (PB0) ===\r\n");

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &gpio);

    uart_send("[PCB] Buzzer ON (1s)\r\n");
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_Delay(1000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
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
    test_pwm_gpio();
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
