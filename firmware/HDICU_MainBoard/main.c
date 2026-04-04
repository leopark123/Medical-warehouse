/**
 * @file    main.c
 * @brief   System entry point — minimal CubeMX-equivalent init
 * @note    This replaces CubeMX-generated main.c.
 *          Clock: HSE 8MHz → PLL ×9 → 72MHz SYSCLK
 *          Debug: SWD (PA13/PA14)
 *          After init, calls app_init() which starts FreeRTOS.
 */

#include "stm32f1xx_hal.h"

/* Provided by App/main_app.c */
extern void app_init(void);

/**
 * @brief System Clock Configuration
 *        HSE 8MHz → PLL ×9 → SYSCLK 72MHz
 *        AHB=72MHz, APB1=36MHz, APB2=72MHz
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* Enable HSE */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;     /* 8MHz × 9 = 72MHz */
    HAL_RCC_OscConfig(&osc);

    /* SYSCLK=PLL, AHB=1, APB1=/2, APB2=1 */
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;       /* 72MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV2;        /* 36MHz (max for APB1) */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;        /* 72MHz */
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2); /* 2 wait states for 72MHz */

    /* Update SystemCoreClock variable */
    SystemCoreClockUpdate();
}

int main(void)
{
    /* HAL init (SysTick, NVIC priority grouping) */
    HAL_Init();

    /* Configure system clock to 72MHz */
    SystemClock_Config();

    /* Application init — starts FreeRTOS, never returns */
    app_init();

    /* Should never reach here */
    for (;;) {}
}
