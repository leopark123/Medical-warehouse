/**
 * @file    flash_storage.h
 * @brief   Parameter persistence — dual-page wear leveling at 0x0807F000
 * @note    Two pages (2KB each), alternating writes with version counter.
 *          Runtime saved every 10 minutes.
 *          Page 0: 0x0807F000, Page 1: 0x0807F800
 */
#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

/* Persistent parameters — what survives power cycle.
 * MUST be 4-byte aligned and size must be multiple of 4 for word-level flash programming.
 * Current: 3 x uint32_t = 12 bytes. If adding fields, pad to 4-byte boundary. */
typedef struct __attribute__((aligned(4))) {
    uint32_t version;           /* Increments on each save, used to find latest */
    uint32_t total_runtime_min; /* Cumulative runtime in minutes */
    uint32_t checksum;          /* Simple additive checksum */
} FlashParams_t;

/* Compile-time check: struct must be 4-byte aligned for flash word programming */
_Static_assert(sizeof(FlashParams_t) % 4 == 0, "FlashParams_t size must be multiple of 4");

/* Initialize: read both pages, load the one with higher valid version */
bool flash_storage_init(void);

/* Save current runtime to flash (call every 10 minutes) */
bool flash_storage_save(uint32_t runtime_min);

/* Get last loaded runtime */
uint32_t flash_storage_get_runtime(void);

#endif
