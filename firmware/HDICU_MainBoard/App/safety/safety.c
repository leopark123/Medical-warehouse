/**
 * @file    safety.c
 * @brief   Runtime safety — buzzer feedback + WDT-assisted reset + event log
 */
#include "safety.h"
#include "bsp_config.h"
#include "stm32f1xx_hal.h"

/* ========================================================================= */
/*  Ring buffer for non-fatal events (RAM-only, lost on reset)               */
/* ========================================================================= */
#define SAFETY_LOG_SIZE     16

typedef struct {
    SafetyEvent_t evt;
    uint32_t      context;
    uint32_t      tick_ms;
} SafetyLogEntry_t;

static SafetyLogEntry_t s_log[SAFETY_LOG_SIZE];
static volatile uint32_t s_log_head  = 0;  /* next write slot */
static volatile uint32_t s_log_count = 0;  /* total events ever */
static volatile SafetyEvent_t s_last_evt = SAFETY_EVT_NONE;

/* ========================================================================= */
/*  Internal: buzzer control (bypass GPIO HAL since may be in fault ctx)     */
/* ========================================================================= */
static inline void buzzer_raw_on(void)
{
    BSP_BUZZER_PORT->BSRR = BSP_BUZZER_PIN;
}
static inline void buzzer_raw_off(void)
{
    BSP_BUZZER_PORT->BSRR = (uint32_t)BSP_BUZZER_PIN << 16;
}

/* Busy-wait delay via simple loop — safe in fault/hook contexts where
 * SysTick/HAL_Delay may not be trustworthy.
 * Tuned for 72MHz Cortex-M3 (rough, ~4 cycles/iter). */
static void busy_delay_ms(uint32_t ms)
{
    volatile uint32_t cnt = ms * (72000UL / 4U);
    while (cnt--) { __asm volatile ("nop"); }
}

/* Feed IWDG manually during busy-wait patterns so we control reset timing */
static inline void iwdg_refresh(void)
{
    IWDG->KR = 0xAAAA;
}

/* ========================================================================= */
/*  Buzzer beep patterns per event class                                     */
/*  Pattern: N short beeps (0.2s on, 0.2s off), 1s pause, repeat.            */
/*  Counts chosen to be distinguishable by ear:                              */
/*    1 beep = POST/init fail (0x10-0x1F)                                    */
/*    2 beeps = stack/malloc (0x20-0x2F)                                     */
/*    3 beeps = CPU fault (0x30-0x3F)                                        */
/*    Steady on = unknown/unclassified                                       */
/* ========================================================================= */
static uint8_t event_beep_count(SafetyEvent_t evt)
{
    uint8_t hi = (uint8_t)((uint32_t)evt >> 4);
    switch (hi) {
        case 0x1: return 1;
        case 0x2: return 2;
        case 0x3: return 3;
        case 0x4: return 4;
        case 0x5: return 5;
        default:  return 0;  /* steady on */
    }
}

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */
__attribute__((noreturn))
void safety_fatal(SafetyEvent_t evt)
{
    /* Disable all interrupts — we own the CPU now */
    __disable_irq();

    /* FIX I2: guard against re-entry (nested fault during fatal handling).
     * If a second fault happens inside HAL_GPIO_Init or delay loop, jump
     * straight to WDT-wait with buzzer steady on — no HAL calls. */
    static volatile uint8_t s_in_fatal = 0;
    if (s_in_fatal) {
        /* Best effort: buzzer may or may not be configured, but the WDT
         * will still fire within ~4 seconds either way. */
        BSP_BUZZER_PORT->BSRR = BSP_BUZZER_PIN;  /* steady on, direct reg write */
        for (;;) { __asm volatile ("nop"); }
    }
    s_in_fatal = 1;

    /* Record event for post-mortem (may or may not survive) */
    s_last_evt = evt;

    /* Ensure buzzer clock + pin mode are valid (may be called pre-init).
     * Acceptable RMW race with mainline code: GPIOB clock is enabled very
     * early in app_init() and rarely changes; worst case we re-enable it. */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {
        .Pin   = BSP_BUZZER_PIN,
        .Mode  = GPIO_MODE_OUTPUT_PP,
        .Pull  = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(BSP_BUZZER_PORT, &gpio);

    uint8_t beeps = event_beep_count(evt);

    /* Beep audibly for ~3 cycles, refreshing IWDG between short delays.
     * FIX I1: LSI-clocked IWDG has ±30% drift, so worst-case reload-to-reset
     * is ~2.8 s. Keep every refresh-to-refresh gap ≤ 1.5 s to stay safe.
     * Steady-on path now splits the 3 s display into two 1.5 s halves. */
    uint8_t cycle = 0;
    for (;;) {
        if (beeps == 0) {
            /* Steady-on pattern for unclassified events */
            buzzer_raw_on();
            iwdg_refresh();
            busy_delay_ms(1500);
            iwdg_refresh();
            busy_delay_ms(1500);
            cycle++;
        } else {
            for (uint8_t i = 0; i < beeps; i++) {
                buzzer_raw_on();
                iwdg_refresh();
                busy_delay_ms(200);
                buzzer_raw_off();
                busy_delay_ms(200);
            }
            iwdg_refresh();
            busy_delay_ms(1000);   /* inter-pattern pause (< 1.5 s margin) */
            cycle++;
        }

        /* After 3 full audible cycles, let IWDG reset us. */
        if (cycle >= 3) {
            buzzer_raw_on();  /* steady on as final cue */
            for (;;) { __asm volatile ("nop"); }  /* no refresh → WDT fires */
        }
    }
}

void safety_record(SafetyEvent_t evt, uint32_t context)
{
    /* Lock-free-ish: brief irq-disable for ring buffer write */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint32_t idx = s_log_head;
    s_log[idx].evt     = evt;
    s_log[idx].context = context;
    s_log[idx].tick_ms = HAL_GetTick();
    /* SAFETY_LOG_SIZE is power-of-two → bitmask is MISRA-12.1-friendly */
    s_log_head = (idx + 1U) & (SAFETY_LOG_SIZE - 1U);
    if (s_log_count < 0xFFFFFFFFu) s_log_count++;
    s_last_evt = evt;

    __set_PRIMASK(primask);
}

SafetyEvent_t safety_last_event(void)
{
    return s_last_evt;
}

uint32_t safety_event_count(void)
{
    return s_log_count;
}

__attribute__((noreturn))
void safety_assert_fail(uint32_t line)
{
    /* Record line first so last_evt reflects the assert with location */
    safety_record(SAFETY_EVT_ASSERT, line);
    /* 5-beep pattern (0x5_ class) + WDT reset */
    safety_fatal(SAFETY_EVT_ASSERT);
}
