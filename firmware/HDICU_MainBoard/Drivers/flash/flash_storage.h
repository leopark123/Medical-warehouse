/**
 * @file    flash_storage.h
 * @brief   Parameter persistence — dual-page wear leveling
 * @note    Two independent storage regions:
 *            Region A (Runtime): Page 254/255 (0x0807F000/0x0807F800)
 *              - FlashParams_t: runtime_min only
 *              - Save interval: 1 hour (v2.1 reduced from 10 min for longer life)
 *            Region B (Config): Page 252/253 (0x0807E000/0x0807E800) — v2.1
 *              - FlashConfig_t: calibration + factory limits
 *              - Save interval: event-driven (0x03/0x09/0x0B from iPad)
 *          Each region uses ping-pong wear leveling with version counter.
 */
#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "app_data.h"

/* ========================================================================= */
/*  Region A: Runtime (existing, 仅runtime_min)                               */
/* ========================================================================= */
typedef struct __attribute__((aligned(4))) {
    uint32_t version;
    uint32_t total_runtime_min;
    uint32_t checksum;
} FlashParams_t;

_Static_assert(sizeof(FlashParams_t) % 4 == 0, "FlashParams_t size must be multiple of 4");

/* ========================================================================= */
/*  Region B: Config (v2.1, 校准+限值)                                         */
/* ========================================================================= */
typedef struct __attribute__((aligned(4))) {
    uint32_t version;               /* Increments on save */
    CalibrationData_t calibration;  /* 8 bytes: 4 × int16 */
    FactoryLimits_t   limits;       /* 18 bytes: 9 × uint16 */
    uint16_t _pad;                  /* align to 4-byte */
    uint32_t checksum;              /* 额外的完整性校验 */
} FlashConfig_t;

_Static_assert(sizeof(FlashConfig_t) % 4 == 0, "FlashConfig_t size must be multiple of 4");

/* ========================================================================= */
/*  API                                                                      */
/* ========================================================================= */

/* Initialize both regions — read both pages of each region, load latest version.
 * Returns false only on critical errors; missing data is OK (returns defaults). */
bool flash_storage_init(void);

/* Region A: runtime_min persistence */
bool flash_storage_save(uint32_t runtime_min);
uint32_t flash_storage_get_runtime(void);

/* Region B: config persistence (v2.1) */
bool flash_storage_save_config(const CalibrationData_t *cal, const FactoryLimits_t *lim);
bool flash_storage_load_config(CalibrationData_t *cal, FactoryLimits_t *lim);
/* Returns true if Flash contains valid config data, false if using hardcoded defaults */
bool flash_storage_has_config(void);

/* Erase config region (0x0B 恢复出厂) */
bool flash_storage_erase_config(void);

#endif
