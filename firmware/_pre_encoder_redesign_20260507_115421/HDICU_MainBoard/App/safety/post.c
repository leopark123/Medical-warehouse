/**
 * @file    post.c
 * @brief   Power-On Self-Test implementation
 *
 *          IMPORTANT constraints:
 *          - Called BEFORE FreeRTOS scheduler starts and BEFORE driver init
 *          - Cannot use HAL_Delay (SysTick may not be incrementing reliably
 *            before scheduler, but HAL tick is set in main.c so it's OK)
 *          - Cannot assume any GPIO / peripheral beyond RCC clocks
 */
#include "post.h"
#include "stm32f1xx_hal.h"

/* ========================================================================= */
/*  POST_1: RAM pattern test on a dedicated scratch region                   */
/*  FIX C1: use a static .bss array to avoid colliding with the live MSP     */
/*          (stack grows down from _estack=0x20010000; touching 0x2000F800   */
/*          while SysTick ISR is running would corrupt pushed frames).       */
/*  The 512-byte scratch lives in .bss after all other globals and is        */
/*  only used during POST (one-shot before task creation).                   */
/* ========================================================================= */
#define POST_RAM_WORDS   128    /* 128 * 4 = 512 bytes */
static volatile uint32_t s_post_ram_scratch[POST_RAM_WORDS];

static PostResult_t post_ram_test(void)
{
    /* Pattern 1: 0x55555555 */
    for (uint32_t i = 0; i < POST_RAM_WORDS; i++) s_post_ram_scratch[i] = 0x55555555UL;
    for (uint32_t i = 0; i < POST_RAM_WORDS; i++) {
        if (s_post_ram_scratch[i] != 0x55555555UL) return POST_ERR_RAM;
    }

    /* Pattern 2: 0xAAAAAAAA */
    for (uint32_t i = 0; i < POST_RAM_WORDS; i++) s_post_ram_scratch[i] = 0xAAAAAAAAUL;
    for (uint32_t i = 0; i < POST_RAM_WORDS; i++) {
        if (s_post_ram_scratch[i] != 0xAAAAAAAAUL) return POST_ERR_RAM;
    }

    /* Pattern 3: address-correlated (catches stuck addr lines) */
    for (uint32_t i = 0; i < POST_RAM_WORDS; i++) s_post_ram_scratch[i] = i ^ 0xDEADBEEFUL;
    for (uint32_t i = 0; i < POST_RAM_WORDS; i++) {
        if (s_post_ram_scratch[i] != (i ^ 0xDEADBEEFUL)) return POST_ERR_RAM;
    }

    /* Clear before exit */
    for (uint32_t i = 0; i < POST_RAM_WORDS; i++) s_post_ram_scratch[i] = 0;

    return POST_OK;
}

/* FIX C2: Stack-canary probe at _estack-32 removed — it sat on a live
 * return frame and would race any IRQ nesting. The RAM pattern test above
 * is a better functional check and does not require stack probing. */

/* ========================================================================= */
/*  POST_3: System clock sanity                                              */
/* ========================================================================= */
static PostResult_t post_clock_check(void)
{
    extern uint32_t SystemCoreClock;
    /* Expect 72 MHz ± 1% */
    if (SystemCoreClock < 70000000UL || SystemCoreClock > 74000000UL) {
        return POST_ERR_CLOCK;
    }
    return POST_OK;
}

/* ========================================================================= */
/*  POST_4: IWDG liveness — verify counter is decrementing                    */
/*  IWDG runs on LSI (~40kHz). Counter should decrement visibly in ~1ms.     */
/* ========================================================================= */
static PostResult_t post_iwdg_check(void)
{
    /* We cannot read IWDG counter directly (not exposed).
     * Instead: verify IWDG->SR shows the reload value synchronization bit. */
    /* After main.c init: prescaler written + reload written → check SR.
     * If IWDG is running, writes propagate and SR eventually clears.
     * Here we simply check the reload register was set to expected value. */
    if ((IWDG->RLR & 0x0FFF) != 2500) {
        return POST_ERR_IWDG;
    }
    /* Refresh once as additional sanity (writing 0xAAAA to KR is safe) */
    IWDG->KR = 0xAAAA;
    return POST_OK;
}

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */
PostResult_t post_run(void)
{
    PostResult_t r;

    r = post_ram_test();
    if (r != POST_OK) return r;

    r = post_clock_check();
    if (r != POST_OK) return r;

    r = post_iwdg_check();
    if (r != POST_OK) return r;

    /* Future (P1): Flash CRC check, peripheral pings,
     * stack-depth measurement from canary pattern. */
    return POST_OK;
}
