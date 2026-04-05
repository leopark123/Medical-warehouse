/**
 * @file    FreeRTOSConfig.h
 * @brief   FreeRTOS configuration for STM32F103VET6 @ 72MHz
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
/* Use actual clock — SystemCoreClock is set by SystemCoreClockUpdate() at runtime */
extern uint32_t SystemCoreClock;
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
/* Remove configSYSTICK_CLOCK_HZ to let FreeRTOS use configCPU_CLOCK_HZ */
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                128
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0

/* Memory allocation */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t)(32 * 1024))  /* 32KB for FreeRTOS heap */
#define configAPPLICATION_ALLOCATED_HEAP        0

/* Hook functions */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2   /* Enable stack overflow detection */
#define configUSE_MALLOC_FAILED_HOOK            1

/* Run-time stats */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* Co-routine (unused) */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2

/* Software timer */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

/* Cortex-M3 interrupt priority configuration */
#ifdef __NVIC_PRIO_BITS
  #define configPRIO_BITS   __NVIC_PRIO_BITS
#else
  #define configPRIO_BITS   4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configKERNEL_INTERRUPT_PRIORITY       ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* Map FreeRTOS handlers to CMSIS names */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
/* NOTE: Do NOT map xPortSysTickHandler to SysTick_Handler here.
 * We provide a custom SysTick_Handler in freertos_hooks.c that:
 *   - Before scheduler starts: only calls HAL_IncTick()
 *   - After scheduler starts: calls both xPortSysTickHandler() and HAL_IncTick()
 * This prevents FreeRTOS tick from running before vTaskStartScheduler(). */

/* Assert */
#define configASSERT(x) if((x) == 0) { taskDISABLE_INTERRUPTS(); for(;;); }

/* Optional function includes */
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskDelete                 0
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xQueueGetMutexHolder        1

/* core_cm3.h is included via Lib/CMSIS/Include/ in the include path */

#endif /* FREERTOS_CONFIG_H */
