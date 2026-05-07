/**
 * @file    freertos_hooks.c
 * @brief   FreeRTOS hook functions + CPU fault handlers + safe SysTick
 *
 *          SysTick conflict resolution:
 *          HAL_Init() enables SysTick before FreeRTOS scheduler starts.
 *          If SysTick_Handler directly calls xPortSysTickHandler(), the RTOS
 *          tick runs before scheduler is ready → crash.
 *
 *          Solution: custom SysTick_Handler that checks scheduler state:
 *          - Before scheduler: only HAL_IncTick()
 *          - After scheduler: xPortSysTickHandler() + HAL_IncTick()
 *
 *          CPU fault handlers:
 *          All Cortex-M faults route to safety_fatal() which:
 *            1. Turns on buzzer (distinctive pattern)
 *            2. Holds for 3 audible cycles
 *            3. Stops refreshing IWDG → MCU auto-resets (~4s)
 */
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include "safety.h"

/* FreeRTOS port provides this — we call it manually after scheduler starts */
extern void xPortSysTickHandler(void);

/* ========================================================================= */
/*  SysTick — bridges HAL tick and FreeRTOS tick                             */
/* ========================================================================= */
void SysTick_Handler(void)
{
    /* Always increment HAL tick (needed for HAL_Delay, HAL_GetTick, UART timeout) */
    HAL_IncTick();

    /* Only call FreeRTOS tick after scheduler has started */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

/* ========================================================================= */
/*  FreeRTOS hooks — route to safety_fatal() for audible feedback            */
/* ========================================================================= */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    /* safety_fatal: 2-beep pattern (0x2_ class), then WDT reset */
    safety_fatal(SAFETY_EVT_STACK_OVERFLOW);
}

void vApplicationMallocFailedHook(void)
{
    /* safety_fatal: 2-beep pattern, then WDT reset */
    safety_fatal(SAFETY_EVT_MALLOC_FAILED);
}

/* ========================================================================= */
/*  CPU fault handlers — Cortex-M3 exception entries                         */
/*  Previously all aliased to Default_Handler (infinite loop, no feedback)   */
/* ========================================================================= */
void NMI_Handler(void)
{
    safety_fatal(SAFETY_EVT_NMI);
}

void HardFault_Handler(void)
{
    /* 3-beep pattern (0x3_ class). Future: capture stack frame regs.
     * For now keep simple and reliable — forensic capture requires
     * noinit RAM which we can add in P1. */
    safety_fatal(SAFETY_EVT_HARD_FAULT);
}

void MemManage_Handler(void)
{
    safety_fatal(SAFETY_EVT_MEM_FAULT);
}

void BusFault_Handler(void)
{
    safety_fatal(SAFETY_EVT_BUS_FAULT);
}

void UsageFault_Handler(void)
{
    safety_fatal(SAFETY_EVT_USAGE_FAULT);
}
