/**
 * @file    system_stm32f1xx.c
 * @brief   CMSIS system initialization for STM32F103VET6
 *          SystemCoreClockUpdate() reads actual RCC registers — no hardcoding.
 */
#include "stm32f1xx.h"

uint32_t SystemCoreClock = 8000000U;  /* Default to HSI until configured */

const uint8_t AHBPrescTable[16U] = {0,0,0,0,0,0,0,0,1,2,3,4,6,7,8,9};
const uint8_t APBPrescTable[8U]  = {0,0,0,0,1,2,3,4};

void SystemInit(void)
{
    /* Reset RCC to default (HSI 8MHz) */
    RCC->CR |= RCC_CR_HSION;
    RCC->CFGR &= 0xF8FF0000U;
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON | RCC_CR_PLLON);
    RCC->CR &= ~RCC_CR_HSEBYP;
    RCC->CIR = 0x009F0000U;

    SCB->VTOR = FLASH_BASE;
}

void SystemCoreClockUpdate(void)
{
    uint32_t tmp, pllmull, pllsource, prediv1factor;

    /* Get SYSCLK source from RCC_CFGR SWS bits */
    tmp = RCC->CFGR & RCC_CFGR_SWS;

    switch (tmp) {
    case 0x00U:  /* HSI as SYSCLK */
        SystemCoreClock = 8000000U;
        break;

    case 0x04U:  /* HSE as SYSCLK */
        SystemCoreClock = HSE_VALUE;
        break;

    case 0x08U:  /* PLL as SYSCLK */
        pllmull = RCC->CFGR & RCC_CFGR_PLLMULL;
        pllsource = RCC->CFGR & RCC_CFGR_PLLSRC;
        pllmull = (pllmull >> 18U) + 2U;

        if (pllsource == 0x00U) {
            /* HSI/2 as PLL source */
            SystemCoreClock = (8000000U >> 1U) * pllmull;
        } else {
            /* HSE as PLL source */
            prediv1factor = (RCC->CFGR & RCC_CFGR_PLLXTPRE) >> 17U;
            SystemCoreClock = (HSE_VALUE >> prediv1factor) * pllmull;
        }
        break;

    default:
        SystemCoreClock = 8000000U;
        break;
    }

    /* Apply AHB prescaler */
    tmp = AHBPrescTable[((RCC->CFGR & RCC_CFGR_HPRE) >> 4U)];
    SystemCoreClock >>= tmp;
}
