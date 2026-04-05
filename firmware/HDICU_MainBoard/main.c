/**
 * @file    main.c
 * @brief   System entry point — minimal CubeMX-equivalent init
 * @note    This replaces CubeMX-generated main.c.
 *          Clock: HSE 8MHz → PLL ×9 → 72MHz SYSCLK
 *          Debug: SWD (PA13/PA14)
 *          After init, calls app_init() which starts FreeRTOS.
 */

#include "stm32f1xx_hal.h"
#include <string.h>

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
    HAL_StatusTypeDef status;

    /* Try HSE (8MHz external crystal) → PLL ×9 → 72MHz */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;     /* 8MHz × 9 = 72MHz */
    status = HAL_RCC_OscConfig(&osc);

    if (status != HAL_OK) {
        /* HSE failed (no crystal?) — fallback to HSI + PLL → 64MHz
         * HSI = 8MHz, PLL input = HSI/2 = 4MHz, ×16 = 64MHz */
        memset(&osc, 0, sizeof(osc));
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLState = RCC_PLL_ON;
        osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
        osc.PLL.PLLMUL = RCC_PLL_MUL16;    /* 4MHz × 16 = 64MHz */
        status = HAL_RCC_OscConfig(&osc);
    }

    if (status != HAL_OK) {
        /* Both HSE and HSI+PLL failed — run on raw HSI 8MHz */
        /* UART baud rates will still be correct because
         * SystemCoreClockUpdate() reads actual RCC registers */
        return;
    }

    /* SYSCLK = PLL output (72MHz or 64MHz) */
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;    /* Max 36MHz for APB1 */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        /* PLL didn't lock or switch failed — stay on HSI */
        /* SystemCoreClockUpdate() will read the actual clock from registers */
    }

    /* DO NOT hardcode SystemCoreClock here.
     * HAL_RCC_ClockConfig() already calls SystemCoreClockUpdate() internally.
     * Our SystemCoreClockUpdate() reads actual RCC registers, so it's always correct. */
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
