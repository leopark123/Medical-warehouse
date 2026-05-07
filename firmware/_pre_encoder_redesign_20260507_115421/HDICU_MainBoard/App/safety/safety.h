/**
 * @file    safety.h
 * @brief   Runtime safety module — fatal event handling + event log ring buffer
 *
 *          Design goals (P0 compliance per IEC 62304 Class C):
 *          - Any safety-critical failure triggers audible buzzer + WDT reset
 *          - Non-silent failure: user always hears/sees something is wrong
 *          - Event codes distinguishable via buzzer beep patterns
 *          - Non-persistent event ring buffer (future: Flash-backed)
 *          - Consistent API across init failures, FreeRTOS hooks, faults
 */
#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*  Safety Event Codes                                                       */
/*  Used by safety_fatal() / safety_record() / iPad 0x0C log export (future) */
/* ========================================================================= */
typedef enum {
    SAFETY_EVT_NONE             = 0x00,

    /* Init failures (main_app.c) */
    SAFETY_EVT_QUEUE_FAIL       = 0x10,  /* xQueueCreate returned NULL */
    SAFETY_EVT_TASK_FAIL        = 0x11,  /* xTaskCreate failed */
    SAFETY_EVT_BSP_FAIL         = 0x12,  /* BSP/driver init returned error */
    SAFETY_EVT_POST_FAIL        = 0x13,  /* Power-on self-test failed */

    /* FreeRTOS runtime failures */
    SAFETY_EVT_STACK_OVERFLOW   = 0x20,  /* vApplicationStackOverflowHook */
    SAFETY_EVT_MALLOC_FAILED    = 0x21,  /* vApplicationMallocFailedHook */

    /* CPU faults */
    SAFETY_EVT_HARD_FAULT       = 0x30,
    SAFETY_EVT_MEM_FAULT        = 0x31,
    SAFETY_EVT_BUS_FAULT        = 0x32,
    SAFETY_EVT_USAGE_FAULT      = 0x33,
    SAFETY_EVT_NMI              = 0x34,

    /* Sensor plausibility failures (recorded, not fatal) */
    SAFETY_EVT_TEMP_IMPLAUSIBLE = 0x40,
    SAFETY_EVT_HUMID_IMPLAUSIBLE= 0x41,
    SAFETY_EVT_O2_IMPLAUSIBLE   = 0x42,
    SAFETY_EVT_CO2_IMPLAUSIBLE  = 0x43,

    /* Control/assertion failures */
    SAFETY_EVT_ASSERT           = 0x50,
    /* SAFETY_EVT_INTERLOCK_VIOL = 0x51 — reserved for future use
     * when interlock violations warrant ring-buffer logging. */
} SafetyEvent_t;

/* ========================================================================= */
/*  Fatal event handler                                                      */
/*  Turns on buzzer with distinctive pattern per event code, then hangs.     */
/*  IWDG (~4s) will force reset → retry.                                     */
/*  DO NOT RETURN.                                                           */
/* ========================================================================= */
__attribute__((noreturn))
void safety_fatal(SafetyEvent_t evt);

/* ========================================================================= */
/*  Non-fatal event recorder                                                 */
/*  Adds event to in-RAM ring buffer for later export.                       */
/*  Safe to call from ISR/task context.                                      */
/* ========================================================================= */
void safety_record(SafetyEvent_t evt, uint32_t context);

/* Retrieve last recorded event (0 = none). For debug/iPad query. */
SafetyEvent_t safety_last_event(void);
uint32_t      safety_event_count(void);

/* ========================================================================= */
/*  Assert failure handler — called from configASSERT()                      */
/*  Routes to safety_fatal(SAFETY_EVT_ASSERT). Context = line number.        */
/*  Implemented noreturn so compiler never expects return.                   */
/* ========================================================================= */
__attribute__((noreturn))
void safety_assert_fail(uint32_t line);

#ifdef __cplusplus
}
#endif

#endif /* SAFETY_H */
