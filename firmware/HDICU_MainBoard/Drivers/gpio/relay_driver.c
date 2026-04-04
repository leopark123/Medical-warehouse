/**
 * @file    relay_driver.c
 * @brief   Relay GPIO driver — stub implementation
 * @note    TODO: Fill in actual GPIO port/pin after visual schematic confirmation.
 *          The relay signals appear on schematic page 2 connected to MCU I/O,
 *          but exact pin numbers need visual verification.
 */

#include "relay_driver.h"
#include "bsp_config.h"
#include "stm32f1xx_hal.h"

/* TODO: Define relay GPIO mapping after schematic visual review.
 * Placeholder structure for when pins are confirmed. */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} RelayGpioMap_t;

/* TODO: Replace with actual pin assignments from schematic */
static const RelayGpioMap_t s_relay_map[BSP_RELAY_COUNT] = {
    [BSP_RELAY_PTC_IO]     = { GPIOD, GPIO_PIN_13 },   /* TODO: confirm */
    [BSP_RELAY_JIARE_IO]   = { GPIOD, GPIO_PIN_14 },   /* TODO: confirm */
    [BSP_RELAY_RED_IO]     = { GPIOD, GPIO_PIN_15 },   /* TODO: confirm */
    [BSP_RELAY_ZIY_IO]     = { GPIOE, GPIO_PIN_7 },    /* TODO: confirm */
    [BSP_RELAY_O2_IO]      = { GPIOE, GPIO_PIN_8 },    /* TODO: confirm */
    [BSP_RELAY_JIASHI_IO]  = { GPIOE, GPIO_PIN_9 },    /* TODO: confirm */
    [BSP_RELAY_FENGJI_IO]  = { GPIOE, GPIO_PIN_10 },   /* TODO: confirm */
    [BSP_RELAY_YASUO_IO]   = { GPIOE, GPIO_PIN_11 },   /* TODO: confirm */
    [BSP_RELAY_WH_IO]      = { GPIOE, GPIO_PIN_12 },   /* TODO: confirm */
};

static uint16_t s_current_bitmap;

void relay_driver_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* Enable GPIO clocks — TODO: enable only needed ports */
    __HAL_RCC_GPIOD_CLK_ENABLE();
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
