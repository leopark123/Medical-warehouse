/**
 * @file    main_relay_test.c
 * @brief   Relay-by-relay identification test
 *          Turns on ONE relay at a time with 5s hold.
 *          Output via UART4 (CN16) at 115200.
 */
#include "stm32f1xx_hal.h"
#include <string.h>

void SysTick_Handler(void) { HAL_IncTick(); }

static UART_HandleTypeDef huart4;

static void uart_send(const char *s)
{
    HAL_UART_Transmit(&huart4, (uint8_t *)s, strlen(s), 200);
}

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    const char *name;
} RelayDef;

static const RelayDef relays[] = {
    { GPIOE, GPIO_PIN_1,  "Relay0 PTC(PE1)    - PTC heater 220V    - U5 ch" },
    { GPIOE, GPIO_PIN_0,  "Relay1 JIARE(PE0)  - Bottom heat 220V   - U5 ch" },
    { GPIOB, GPIO_PIN_9,  "Relay2 RED(PB9)    - IR lamp 220V       - U5 ch" },
    { GPIOB, GPIO_PIN_8,  "Relay3 ZIY(PB8)    - UV lamp 220V       - U5 ch" },
    { GPIOB, GPIO_PIN_7,  "Relay4 O2(PB7)     - O2 valve 12V       - U5 ch" },
    { GPIOE, GPIO_PIN_4,  "Relay5 JIASHI(PE4) - Humidifier 220V    - U34 ch" },
    { GPIOE, GPIO_PIN_3,  "Relay6 FENGJI(PE3) - AC outer fan 220V  - U34 ch" },
    { GPIOE, GPIO_PIN_2,  "Relay7 YASUO(PE2)  - Compressor 220V    - U34 ch" },
    { GPIOB, GPIO_PIN_4,  "Relay8 WH(PB4)     - Nebulizer 12V      - U33 ch" },
};

#define RELAY_COUNT (sizeof(relays)/sizeof(relays[0]))

int main(void)
{
    HAL_Init();

    /* Release PB3/PB4/PA15 from JTAG to GPIO, keep SWD */
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    /* UART4 on PC10 (CN16) */
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

    /* Init all relay GPIOs as output LOW */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    for (int i = 0; i < (int)RELAY_COUNT; i++) {
        gpio.Pin = relays[i].pin;
        HAL_GPIO_Init(relays[i].port, &gpio);
        HAL_GPIO_WritePin(relays[i].port, relays[i].pin, GPIO_PIN_RESET);
    }

    uart_send("\r\n========================================\r\n");
    uart_send("  Relay Identification Test\r\n");
    uart_send("  Each relay ON 5s, OFF 2s\r\n");
    uart_send("  Listen for click, note PCB component\r\n");
    uart_send("========================================\r\n\r\n");

    HAL_Delay(2000);

    for (int i = 0; i < (int)RELAY_COUNT; i++) {
        /* Announce */
        uart_send("[NEXT] ");
        uart_send(relays[i].name);
        uart_send("\r\n");
        uart_send("       -> ON in 3...");
        HAL_Delay(1000);
        uart_send("2...");
        HAL_Delay(1000);
        uart_send("1...");
        HAL_Delay(1000);

        /* Turn ON */
        HAL_GPIO_WritePin(relays[i].port, relays[i].pin, GPIO_PIN_SET);
        uart_send("ON!\r\n");
        uart_send("       (holding 5 seconds - which U clicked?)\r\n");
        HAL_Delay(5000);

        /* Turn OFF */
        HAL_GPIO_WritePin(relays[i].port, relays[i].pin, GPIO_PIN_RESET);
        uart_send("       -> OFF\r\n\r\n");
        HAL_Delay(2000);
    }

    uart_send("========================================\r\n");
    uart_send("  ALL 9 RELAYS TESTED\r\n");
    uart_send("========================================\r\n");

    while (1) { HAL_Delay(1000); }
}
