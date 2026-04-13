/* Minimal test: hold PB4 (WH-IO) HIGH forever so user can probe with multimeter */
#include "stm32f1xx_hal.h"
void SysTick_Handler(void) { HAL_IncTick(); }
int main(void) {
    HAL_Init();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);  /* PB4 = HIGH */
    while(1) { HAL_Delay(1000); }
}
