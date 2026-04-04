/**
 * @file    bsp_config.h
 * @brief   HDICU-ZKB01A Board Support Package - Hardware Pin & Peripheral Mapping
 * @note    Authority: PROJECT_RULES.md + 开发实现基线 V3.3
 *          MCU: STM32F103VET6, 72MHz, 512KB Flash, 64KB RAM
 *
 * WARNING: Do NOT change these mappings without updating the frozen baseline.
 *          UART5 is JFC103, NOT WiFi (schematic label is outdated).
 *          PE4 is FENGJI-NEI2-IO (内循环风机2), NOT 新风风机.
 */

#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

#include "stm32f1xx_hal.h"

/* ========================================================================= */
/*  System Clock                                                             */
/* ========================================================================= */
#define SYS_CLOCK_HZ            72000000U   /* HSE 8MHz -> PLL x9 -> 72MHz */
#define HSE_VALUE_HZ            8000000U

/* ========================================================================= */
/*  UART Allocation (Frozen — 开发基线 2.1.2)                                */
/* ========================================================================= */

/* UART1 — 屏幕板 (双板通信) */
#define BSP_UART_SCREEN             USART1
#define BSP_UART_SCREEN_BAUD        115200U
#define BSP_UART_SCREEN_TX_PORT     GPIOA
#define BSP_UART_SCREEN_TX_PIN      GPIO_PIN_9
#define BSP_UART_SCREEN_RX_PORT     GPIOA
#define BSP_UART_SCREEN_RX_PIN      GPIO_PIN_10

/* UART2 — iPad (上位机通信) */
#define BSP_UART_IPAD               USART2
#define BSP_UART_IPAD_BAUD          115200U
#define BSP_UART_IPAD_TX_PORT       GPIOA
#define BSP_UART_IPAD_TX_PIN        GPIO_PIN_2
#define BSP_UART_IPAD_RX_PORT       GPIOA
#define BSP_UART_IPAD_RX_PIN        GPIO_PIN_3

/* UART3 — CO2 传感器 MWD1006E */
#define BSP_UART_CO2                USART3
#define BSP_UART_CO2_BAUD           9600U
#define BSP_UART_CO2_TX_PORT        GPIOB
#define BSP_UART_CO2_TX_PIN         GPIO_PIN_10
#define BSP_UART_CO2_RX_PORT        GPIOB
#define BSP_UART_CO2_RX_PIN         GPIO_PIN_11

/* UART4 — O2 传感器 OCS-3RL-2.0 */
#define BSP_UART_O2                 UART4
#define BSP_UART_O2_BAUD            9600U
#define BSP_UART_O2_TX_PORT         GPIOC
#define BSP_UART_O2_TX_PIN          GPIO_PIN_10
#define BSP_UART_O2_RX_PORT         GPIOC
#define BSP_UART_O2_RX_PIN          GPIO_PIN_11

/* UART5 — 体征模块 JFC103 (NOT WiFi — schematic label outdated) */
#define BSP_UART_JFC103             UART5
#define BSP_UART_JFC103_BAUD        38400U
#define BSP_UART_JFC103_TX_PORT     GPIOC
#define BSP_UART_JFC103_TX_PIN      GPIO_PIN_12
#define BSP_UART_JFC103_RX_PORT     GPIOD
#define BSP_UART_JFC103_RX_PIN      GPIO_PIN_2

/* ========================================================================= */
/*  ADC — NTC Temperature (Frozen — 开发基线 2.1.3)                          */
/* ========================================================================= */
#define BSP_ADC_NTC_COUNT           4
#define BSP_ADC_NTC1_PORT           GPIOA
#define BSP_ADC_NTC1_PIN            GPIO_PIN_0       /* ADC1_CH0 */
#define BSP_ADC_NTC2_PORT           GPIOA
#define BSP_ADC_NTC2_PIN            GPIO_PIN_1       /* ADC1_CH1 */
#define BSP_ADC_NTC3_PORT           GPIOA
#define BSP_ADC_NTC3_PIN            GPIO_PIN_4       /* ADC1_CH4 */
#define BSP_ADC_NTC4_PORT           GPIOA
#define BSP_ADC_NTC4_PIN            GPIO_PIN_5       /* ADC1_CH5 */

#define BSP_ADC_NTC_CHANNELS        { ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_4, ADC_CHANNEL_5 }

/* ========================================================================= */
/*  PWM — Fan Control (Frozen — 开发基线 2.1.4)                              */
/* ========================================================================= */
/* PE2 = FENGJI-NEI-IO  (内循环风机)   via AO3404 MOS */
/* PE3 = FENGJI-PTC-IO  (PTC加热风机)  via AO3404 MOS */
/* PE4 = FENGJI-NEI2-IO (内循环风机2)  via AO3404 MOS */
#define BSP_PWM_FAN1_PORT           GPIOE
#define BSP_PWM_FAN1_PIN            GPIO_PIN_2
#define BSP_PWM_FAN2_PORT           GPIOE
#define BSP_PWM_FAN2_PIN            GPIO_PIN_3
#define BSP_PWM_FAN3_PORT           GPIOE
#define BSP_PWM_FAN3_PIN            GPIO_PIN_4

/* TODO: Confirm timer/channel assignment from CubeMX after project creation.
 * PE2/PE3/PE4 may not be standard TIM channels on STM32F103VET6.
 * If no timer remap available, use software PWM via GPIO toggle in SysTick/timer ISR. */

/* ========================================================================= */
/*  Relay Outputs (Frozen — 开发基线 2.1.5, ULN2003A/ULN2001 driven)         */
/* ========================================================================= */
/* ULN2003A (U5): 5 channels */
#define BSP_RELAY_PTC_IO            0   /* PTC加热器, 220V */
#define BSP_RELAY_JIARE_IO          1   /* 底部加热, 220V */
#define BSP_RELAY_RED_IO            2   /* 红外灯, 220V */
#define BSP_RELAY_ZIY_IO            3   /* 紫外灯, 220V */
#define BSP_RELAY_O2_IO             4   /* O2阀门, 12V */

/* ULN2001 (U34): 3 channels */
#define BSP_RELAY_JIASHI_IO         5   /* 加湿器, 220V */
#define BSP_RELAY_FENGJI_IO         6   /* 空调外风机, 220V */
#define BSP_RELAY_YASUO_IO          7   /* 空调压缩机, 220V */

/* ULN2001 (U33): 1 channel */
#define BSP_RELAY_WH_IO             8   /* 雾化器, 12V */

#define BSP_RELAY_COUNT             9

/* TODO: Map relay indices to GPIO port/pin after confirming from schematic.
 * These are logical IDs; physical GPIO mapping goes in Drivers/gpio/ */

/* ========================================================================= */
/*  Nursing Level LEDs (Frozen — 开发基线 2.1.4)                             */
/* ========================================================================= */
#define BSP_LED_HULI1_PORT          GPIOD
#define BSP_LED_HULI1_PIN           GPIO_PIN_10
#define BSP_LED_HULI2_PORT          GPIOD
#define BSP_LED_HULI2_PIN           GPIO_PIN_11
#define BSP_LED_HULI3_PORT          GPIOD
#define BSP_LED_HULI3_PIN           GPIO_PIN_12

/* ========================================================================= */
/*  Other I/O (Frozen — 开发基线 2.1.6)                                      */
/* ========================================================================= */
#define BSP_BUZZER_PORT             GPIOB
#define BSP_BUZZER_PIN              GPIO_PIN_0

#define BSP_LIQUID_DETECT_PORT      GPIOB
#define BSP_LIQUID_DETECT_PIN       GPIO_PIN_14

#define BSP_URINE_DETECT_PORT       GPIOB
#define BSP_URINE_DETECT_PIN        GPIO_PIN_15

/* ========================================================================= */
/*  Flash Storage (Frozen — 开发基线 11.3, corrected from 0x08070000)         */
/* ========================================================================= */
#define BSP_FLASH_PARAM_BASE        0x0807F000U     /* Last 2 pages = 4KB */
#define BSP_FLASH_PARAM_PAGE0       0x0807F000U     /* Page 254: 2KB */
#define BSP_FLASH_PARAM_PAGE1       0x0807F800U     /* Page 255: 2KB */
#define BSP_FLASH_PAGE_SIZE         2048U

/* ========================================================================= */
/*  FreeRTOS Task Priorities (Frozen — 开发基线 6.1)                          */
/* ========================================================================= */
#define TASK_PRIO_SENSOR            (configMAX_PRIORITIES - 1)  /* High */
#define TASK_PRIO_CONTROL           (configMAX_PRIORITIES - 1)  /* High */
#define TASK_PRIO_ALARM             (configMAX_PRIORITIES - 1)  /* High */
#define TASK_PRIO_COMM_SCREEN       (configMAX_PRIORITIES - 2)  /* Medium */
#define TASK_PRIO_COMM_IPAD         (configMAX_PRIORITIES - 2)  /* Medium */
#define TASK_PRIO_SYSTEM            (configMAX_PRIORITIES - 3)  /* Low */

#define TASK_STACK_SENSOR           512
#define TASK_STACK_CONTROL          512
#define TASK_STACK_ALARM            256
#define TASK_STACK_COMM_SCREEN      512
#define TASK_STACK_COMM_IPAD        512
#define TASK_STACK_SYSTEM           256

/* Task periods in ms */
#define TASK_PERIOD_SENSOR_MS       100
#define TASK_PERIOD_CONTROL_MS      200
#define TASK_PERIOD_ALARM_MS        100
#define TASK_PERIOD_COMM_SCREEN_MS  100
#define TASK_PERIOD_SYSTEM_MS       1000
/* CommIPadTask is event-driven, no fixed period */

#endif /* BSP_CONFIG_H */
