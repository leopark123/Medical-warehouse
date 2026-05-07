/**
 * @file    relay_driver.c
 * @brief   Relay GPIO driver — schematic-confirmed pin mapping
 * @note    GPIO from 原理图.pdf U24 NL netlist extraction (2026-04-09):
 *          PE1=PTC-IO(U5), PE0=JIARE-IO(U5), PB9=RED-IO(U5),
 *          PB8=ZIY-IO(U5), PB7=O2-IO(U5), PE4=JIASHI-IO(U34),
 *          PE3=FENGJI-IO(U34), PE2=YASUO-IO(U34), PB4=WH-IO(U33)
 */

#include "relay_driver.h"
#include "bsp_config.h"
#include "stm32f1xx_hal.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} RelayGpioMap_t;

/* Schematic-confirmed GPIO mapping — 原理图 PIU24 NL网表 */
static const RelayGpioMap_t s_relay_map[BSP_RELAY_COUNT] = {
    [BSP_RELAY_PTC_IO]     = { GPIOE, GPIO_PIN_1 },    /* PE1  PTC heater    220V U5 */
    [BSP_RELAY_JIARE_IO]   = { GPIOE, GPIO_PIN_0 },    /* PE0  Bottom heat   220V U5 */
    [BSP_RELAY_RED_IO]     = { GPIOB, GPIO_PIN_9 },    /* PB9  IR lamp       220V U5 */
    [BSP_RELAY_ZIY_IO]     = { GPIOB, GPIO_PIN_8 },    /* PB8  UV lamp       220V U5 */
    [BSP_RELAY_O2_IO]      = { GPIOB, GPIO_PIN_7 },    /* PB7  O2 valve      12V  U5 */
    [BSP_RELAY_JIASHI_IO]  = { GPIOE, GPIO_PIN_4 },    /* PE4  Humidifier    220V U34 */
    [BSP_RELAY_FENGJI_IO]  = { GPIOE, GPIO_PIN_3 },    /* PE3  AC outer fan  220V U34 */
    [BSP_RELAY_YASUO_IO]   = { GPIOE, GPIO_PIN_2 },    /* PE2  Compressor    220V U34 */
    [BSP_RELAY_WH_IO]      = { GPIOB, GPIO_PIN_4 },    /* PB4  Nebulizer     12V  U33 */
};

static uint16_t s_current_bitmap;

void relay_driver_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* Enable GPIO clocks for relay pins: GPIOB(PB4/7/8/9) + GPIOE(PE0-4) */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    for (int i = 0; i < BSP_RELAY_COUNT; i++) {
        gpio.Pin = s_relay_map[i].pin;
        HAL_GPIO_Init(s_relay_map[i].port, &gpio);
        HAL_GPIO_WritePin(s_relay_map[i].port, s_relay_map[i].pin, GPIO_PIN_RESET);
    }

    s_current_bitmap = 0;
}

void relay_driver_apply(uint16_t bitmap)
{
    for (int i = 0; i < BSP_RELAY_COUNT; i++) {
        GPIO_PinState state = (bitmap & (1U << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET;
        HAL_GPIO_WritePin(s_relay_map[i].port, s_relay_map[i].pin, state);
    }
    s_current_bitmap = bitmap;
}

void relay_driver_set(uint8_t relay_idx, bool on)
{
    if (relay_idx >= BSP_RELAY_COUNT) return;
    if (on) {
        s_current_bitmap |= (1U << relay_idx);
    } else {
        s_current_bitmap &= ~(1U << relay_idx);
    }
    GPIO_PinState state = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(s_relay_map[relay_idx].port, s_relay_map[relay_idx].pin, state);
}

uint16_t relay_driver_read(void) { return s_current_bitmap; }
