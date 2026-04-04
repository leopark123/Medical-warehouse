/**
 * @file    stm32f1xx_hal_conf.h
 * @brief   HAL configuration — enable only the modules we use
 */
#ifndef STM32F1XX_HAL_CONF_H
#define STM32F1XX_HAL_CONF_H

#define HSE_VALUE            8000000U
#define HSE_STARTUP_TIMEOUT  100U
#define HSI_VALUE            8000000U
#define LSE_VALUE            32768U
#define LSE_STARTUP_TIMEOUT  5000U
#define LSI_VALUE            40000U

/* Module enables */
#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* Tick and priority */
#define TICK_INT_PRIORITY       15U
#define USE_RTOS                0U   /* HAL requires 0; FreeRTOS integration via separate hooks */
#define PREFETCH_ENABLE         1U

/* Assert macro — disable parameter checking in release builds */
#ifdef USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t* file, uint32_t line);
#else
  #define assert_param(expr) ((void)0U)
#endif

/* Include HAL sub-headers */
#include "stm32f1xx_hal_def.h"
#include "stm32f1xx_hal_rcc.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_dma.h"
#include "stm32f1xx_hal_cortex.h"
#include "stm32f1xx_hal_adc.h"
#include "stm32f1xx_hal_flash.h"
#include "stm32f1xx_hal_tim.h"
#include "stm32f1xx_hal_uart.h"

#endif
