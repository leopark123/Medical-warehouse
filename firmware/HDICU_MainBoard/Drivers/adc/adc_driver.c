/**
 * @file    adc_driver.c
 * @brief   ADC driver — 4 channel NTC reading via ADC1
 */

#include "adc_driver.h"
#include "bsp_config.h"
#include "stm32f1xx_hal.h"

static ADC_HandleTypeDef s_hadc1;

static const uint32_t s_channels[ADC_CHANNEL_COUNT] = {
    ADC_CHANNEL_0,  /* PA0 - NTC1 */
    ADC_CHANNEL_1,  /* PA1 - NTC2 */
    ADC_CHANNEL_4,  /* PA4 - NTC3 */
    ADC_CHANNEL_5,  /* PA5 - NTC4 */
};

void adc_driver_init(void)
{
    /* GPIO init for analog inputs */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pin = BSP_ADC_NTC1_PIN | BSP_ADC_NTC2_PIN | BSP_ADC_NTC3_PIN | BSP_ADC_NTC4_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* ADC1 config */
    s_hadc1.Instance = ADC1;
    s_hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    s_hadc1.Init.ContinuousConvMode = DISABLE;
    s_hadc1.Init.DiscontinuousConvMode = DISABLE;
    s_hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    s_hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s_hadc1.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&s_hadc1);

    /* Calibration */
    HAL_ADCEx_Calibration_Start(&s_hadc1);
}

uint16_t adc_driver_read_channel(uint8_t ch_idx)
{
    if (ch_idx >= ADC_CHANNEL_COUNT) return 0;

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = s_channels[ch_idx];
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;  /* Max for accuracy */
    HAL_ADC_ConfigChannel(&s_hadc1, &sConfig);

    HAL_ADC_Start(&s_hadc1);
    HAL_ADC_PollForConversion(&s_hadc1, 10);
    uint16_t val = (uint16_t)HAL_ADC_GetValue(&s_hadc1);
    HAL_ADC_Stop(&s_hadc1);

    return val;
}

void adc_driver_read_all(uint16_t adc_values[ADC_CHANNEL_COUNT])
{
    for (int i = 0; i < ADC_CHANNEL_COUNT; i++) {
        adc_values[i] = adc_driver_read_channel(i);
    }
}

/* ADC IRQ Handlers — prevent Default_Handler hard-loop if ADC interrupt fires */
void ADC1_2_IRQHandler(void)
{
    HAL_ADC_IRQHandler(&s_hadc1);
}

void ADC3_IRQHandler(void)
{
    /* ADC3 not used but interrupt may fire — just clear it */
    HAL_ADC_IRQHandler(&s_hadc1);
}
