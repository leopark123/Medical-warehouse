/**
 * @file    bsp_config.h
 * @brief   HDICU-ZKB01A Board Support Package - Hardware Pin & Peripheral Mapping
 * @note    Authority: PROJECT_RULES.md + 开发实现基线 V3.3
 *          MCU: STM32F103VET6, 72MHz, 512KB Flash, 64KB RAM
 *
 * WARNING: Do NOT change these mappings without updating the frozen baseline.
 *          UART5 is JFC103, NOT WiFi (schematic label is outdated).
 *          GPIO pins updated to schematic netlist 2026-04-09.
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
/* Schematic-confirmed fan pins (原理图 PIU24 NL网表 + 硬件工程师确认 2026-04-09)
 *
 * Hardware engineer: only PTC fan needs speed control (PE9=PWM).
 *                    All other fans are ON/OFF only.
 *
 * PE5  = FENGJI-NEI-IO  (内循环风机)   via AO3404 — ON/OFF
 * PE6  = FENGJI-PTC-IO  (PTC风机使能)  via AO3404 — ON/OFF (enable)
 * PC13 = FENGJI-NEI2-IO (空调内风机)   via AO3404 — ON/OFF (low-speed IO)
 * PE9  = PWM1           (PTC风机调速)               — PWM (唯一调速通道)
 */
#define BSP_FAN_NEI_PORT            GPIOE       /* PE5: 内循环风机 ON/OFF */
#define BSP_FAN_NEI_PIN             GPIO_PIN_5
#define BSP_FAN_PTC_EN_PORT         GPIOE       /* PE6: PTC风机使能 ON/OFF */
#define BSP_FAN_PTC_EN_PIN          GPIO_PIN_6
#define BSP_FAN_NEI2_PORT           GPIOC       /* PC13: 空调内风机 ON/OFF */
#define BSP_FAN_NEI2_PIN            GPIO_PIN_13
#define BSP_FAN_PTC_PWM_PORT        GPIOE       /* PE9: PTC风机调速 PWM */
#define BSP_FAN_PTC_PWM_PIN         GPIO_PIN_9

/* Legacy aliases for code that uses BSP_PWM_FANx naming */
#define BSP_PWM_FAN1_PORT           BSP_FAN_NEI_PORT
#define BSP_PWM_FAN1_PIN            BSP_FAN_NEI_PIN
#define BSP_PWM_FAN2_PORT           BSP_FAN_PTC_EN_PORT
#define BSP_PWM_FAN2_PIN            BSP_FAN_PTC_EN_PIN
#define BSP_PWM_FAN3_PORT           BSP_FAN_NEI2_PORT
#define BSP_PWM_FAN3_PIN            BSP_FAN_NEI2_PIN

/* ========================================================================= */
/*  Relay Outputs (Frozen — 开发基线 2.1.5, ULN2003A/ULN2001 driven)         */
/* ========================================================================= */
/* ULN2003A (U5): 5 channels */
#define BSP_RELAY_PTC_IO            0   /* PTC加热器, 220V */
#define BSP_RELAY_JIARE_IO          1   /* 底部加热, 220V */
#define BSP_RELAY_RED_IO            2   /* 红外灯, 220V (硬件确认:空着不用) */
#define BSP_RELAY_ZIY_IO            3   /* 紫外灯, 220V */
#define BSP_RELAY_O2_IO             4   /* O2阀门, 12V */

/* ULN2001 (U34): 3 channels */
#define BSP_RELAY_JIASHI_IO         5   /* 加湿器, 220V */
#define BSP_RELAY_FENGJI_IO         6   /* 空调外风机, 220V */
#define BSP_RELAY_YASUO_IO          7   /* 空调压缩机, 220V */

/* ULN2001 (U33): 1 channel */
#define BSP_RELAY_WH_IO             8   /* 雾化器, 12V */

/* 硬件工程师确认(2026-04-16):
 * - 内/外循环 = 推拉电磁铁 PE7(MAGNET-IO)，不是CN32 FAI
 * - 新风净化 = PTC风机调速 PE9(PWM)+PE6(使能)，已有pwm_driver */

/* ========================================================================= */
/*  照明灯 U32连接器 (硬件确认 2026-04-16)                                    */
/*  4色灯, 高电平亮低电平灭. L-LMP照明板原理图: SCH_Schematic3.pdf            */
/* ========================================================================= */
#define BSP_LIGHT_LED1_PORT         GPIOE       /* PE10 = LED1 照明颜色1 */
#define BSP_LIGHT_LED1_PIN          GPIO_PIN_10
#define BSP_LIGHT_LED2_PORT         GPIOE       /* PE11 = LED2 照明颜色2 */
#define BSP_LIGHT_LED2_PIN          GPIO_PIN_11
#define BSP_LIGHT_LED3_PORT         GPIOE       /* PE12 = LED3 照明颜色3 */
#define BSP_LIGHT_LED3_PIN          GPIO_PIN_12
#define BSP_LIGHT_LED4_PORT         GPIOE       /* PE13 = LED4 照明颜色4 */
#define BSP_LIGHT_LED4_PIN          GPIO_PIN_13

/* ========================================================================= */
/*  内/外循环电磁铁 (硬件确认 2026-04-16)                                     */
/* ========================================================================= */
#define BSP_MAGNET_PORT             GPIOE       /* PE7 = MAGNET-IO 推拉电磁铁 */
#define BSP_MAGNET_PIN              GPIO_PIN_7

/* ========================================================================= */
/*  供氧机握手输出信号 (新语义: 跟随PD8, 默认HIGH, PD8低时LOW)                 */
/* ========================================================================= */
#define BSP_GY_PORT                 GPIOB       /* PB5 = 对PD8的应答信号 */
#define BSP_GY_PIN                  GPIO_PIN_5

/* ========================================================================= */
/*  外部O2需求输入 (新增, active-low, 200ms去抖)                              */
/* ========================================================================= */
#define BSP_O2_MASTER_PORT          GPIOD       /* PD8 总制氧机信号, LOW=请求开O2 */
#define BSP_O2_MASTER_PIN           GPIO_PIN_8
#define BSP_O2_REQ_PORT             GPIOB       /* PB6 制氧机信号, LOW=请求开O2 */
#define BSP_O2_REQ_PIN              GPIO_PIN_6

/* ========================================================================= */
/*  压缩机指示灯 (硬件确认 2026-04-16)                                       */
/* ========================================================================= */
#define BSP_COMPRESSOR_LED_PORT     GPIOB       /* PB12 = LED01-IO 压缩机启动亮 */
#define BSP_COMPRESSOR_LED_PIN      GPIO_PIN_12

#define BSP_RELAY_COUNT             9

/* Relay logical IDs; physical GPIO mapping confirmed in Drivers/gpio/relay_driver.c */

/* ========================================================================= */
/*  Nursing Level LEDs (Frozen — 开发基线 2.1.4)                             */
/* ========================================================================= */
/* Schematic-confirmed care LED pins (原理图 PIU24 NL网表) */
#define BSP_LED_HULI1_PORT          GPIOB       /* PB1 = HULI01-IO */
#define BSP_LED_HULI1_PIN           GPIO_PIN_1
#define BSP_LED_HULI2_PORT          GPIOB       /* PB0 = HULI02-IO */
#define BSP_LED_HULI2_PIN           GPIO_PIN_0
#define BSP_LED_HULI3_PORT          GPIOC       /* PC5 = HULI03-IO */
#define BSP_LED_HULI3_PIN           GPIO_PIN_5

/* ========================================================================= */
/*  Other I/O (Frozen — 开发基线 2.1.6)                                      */
/* ========================================================================= */
/* Schematic-confirmed: PB3 = BUZZER (原理图 PIU24089) */
#define BSP_BUZZER_PORT             GPIOB
#define BSP_BUZZER_PIN              GPIO_PIN_3

/* Hardware engineer confirmed 2026-04-09: PB14=液位检测, PB15=尿液检测.
 * Schematic NL labels (KEY3/KEY2) are net names, actual function is detect inputs. */
#define BSP_LIQUID_DETECT_PORT      GPIOB
#define BSP_LIQUID_DETECT_PIN       GPIO_PIN_14

#define BSP_URINE_DETECT_PORT       GPIOB
#define BSP_URINE_DETECT_PIN        GPIO_PIN_15

/* ========================================================================= */
/*  Flash Storage (Frozen — 开发基线 11.3, corrected from 0x08070000)         */
/* ========================================================================= */
#define BSP_FLASH_PARAM_BASE        0x0807F000U     /* Last 2 pages = 4KB */
#define BSP_FLASH_PARAM_PAGE0       0x0807F000U     /* Page 254: runtime_min 主页 */
#define BSP_FLASH_PARAM_PAGE1       0x0807F800U     /* Page 255: runtime_min 备份页 */
#define BSP_FLASH_PAGE_SIZE         2048U

/* v2.1: 配置存储 (校准+限值), 独立于runtime的另外两页 */
#define BSP_FLASH_CONFIG_PAGE0      0x0807E000U     /* Page 252: 配置主页 */
#define BSP_FLASH_CONFIG_PAGE1      0x0807E800U     /* Page 253: 配置备份页 */

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
