/**
 * @file    freertos_hooks.c
 * @brief   FreeRTOS mandatory hook functions
 */
#include "FreeRTOS.h"
#include "task.h"

/* Stack overflow hook — called when a task exceeds its stack */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    /* Halt — should never happen in production */
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

/* Malloc failed hook — called when pvPortMalloc fails */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}
