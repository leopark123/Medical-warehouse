/**
 * @file    freertos_hooks.c
 * @brief   FreeRTOS hook functions + safe SysTick_Handler
 *
 *          SysTick conflict resolution:
 *          HAL_Init() enables SysTick before FreeRTOS scheduler starts.
 *          If SysTick_Handler directly calls xPortSysTickHandler(), the RTOS
 *          tick runs before scheduler is ready → crash.
 *
 *          Solution: custom SysTick_Handler that checks scheduler state:
 *          - Before scheduler: only HAL_IncTick()
 *          - After scheduler: xPortSysTickHandler() + HAL_IncTick()
 */
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"

/* FreeRTOS port provides this — we call it manually after scheduler starts */
extern void xPortSysTickHandler(void);

/**
 * @brief Safe SysTick_Handler — bridges HAL and FreeRTOS tick
 *        This replaces the FreeRTOSConfig.h macro mapping approach.
 */
void SysTick_Handler(void)
{
    /* Always increment HAL tick (needed for HAL_Delay, HAL_GetTick, UART timeout) */
    HAL_IncTick();

    /* Only call FreeRTOS tick after scheduler has started */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

/* Stack overflow hook */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

/* Malloc failed hook */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}
