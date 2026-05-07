/**
 * @file    flash_storage.c
 * @brief   Dual-region flash storage with wear leveling
 * @note    Region A: runtime_min, Page 254/255 (0x0807F000/0x0807F800)
 *          Region B: config (calibration+limits), Page 252/253 (0x0807E000/0x0807E800)
 *          STM32F103VET6: 2KB per page, word (32-bit) programming
 *          Each region: alternating pages, version counter selects latest
 */

#include "flash_storage.h"
#include "bsp_config.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* ========================================================================= */
/*  Region A: Runtime state                                                  */
/* ========================================================================= */
static FlashParams_t s_runtime_current;
static uint8_t       s_runtime_next_page;    /* 0 or 1 */

/* ========================================================================= */
/*  Region B: Config state (v2.1)                                            */
/* ========================================================================= */
static FlashConfig_t s_config_current;
static uint8_t       s_config_next_page;     /* 0 or 1 */
static bool          s_config_valid;         /* true if Flash has valid config */

/* ========================================================================= */
/*  Common helpers                                                           */
/* ========================================================================= */
static uint32_t calc_runtime_checksum(const FlashParams_t *p)
{
    const uint8_t *bytes = (const uint8_t *)p;
    uint32_t sum = 0;
    for (size_t i = 0; i < offsetof(FlashParams_t, checksum); i++) {
        sum += bytes[i];
    }
    return sum;
}

static uint32_t calc_config_checksum(const FlashConfig_t *p)
{
    const uint8_t *bytes = (const uint8_t *)p;
    uint32_t sum = 0;
    for (size_t i = 0; i < offsetof(FlashConfig_t, checksum); i++) {
        sum += bytes[i];
    }
    return sum;
}

static bool read_runtime_page(uint8_t page, FlashParams_t *out)
{
    uint32_t addr = (page == 0) ? BSP_FLASH_PARAM_PAGE0 : BSP_FLASH_PARAM_PAGE1;
    memcpy(out, (const void *)addr, sizeof(FlashParams_t));
    return (out->checksum == calc_runtime_checksum(out) && out->version != 0xFFFFFFFF);
}

static bool read_config_page(uint8_t page, FlashConfig_t *out)
{
    uint32_t addr = (page == 0) ? BSP_FLASH_CONFIG_PAGE0 : BSP_FLASH_CONFIG_PAGE1;
    memcpy(out, (const void *)addr, sizeof(FlashConfig_t));
    return (out->checksum == calc_config_checksum(out) && out->version != 0xFFFFFFFF);
}

static bool erase_page(uint32_t page_addr)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0;
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = page_addr;
    erase.NbPages = 1;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) return false;
    return (page_error == 0xFFFFFFFF);
}

static bool write_words(uint32_t addr, const uint32_t *src, size_t word_count)
{
    for (size_t i = 0; i < word_count; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              addr + i * 4, src[i]) != HAL_OK) {
            return false;
        }
    }
    return true;
}

/* ========================================================================= */
/*  Public API: Init (both regions)                                          */
/* ========================================================================= */
bool flash_storage_init(void)
{
    /* --- Region A: runtime --- */
    FlashParams_t p0, p1;
    bool v0 = read_runtime_page(0, &p0);
    bool v1 = read_runtime_page(1, &p1);

    if (v0 && v1) {
        if ((int32_t)(p0.version - p1.version) >= 0) {
            s_runtime_current = p0;
            s_runtime_next_page = 1;
        } else {
            s_runtime_current = p1;
            s_runtime_next_page = 0;
        }
    } else if (v0) {
        s_runtime_current = p0;
        s_runtime_next_page = 1;
    } else if (v1) {
        s_runtime_current = p1;
        s_runtime_next_page = 0;
    } else {
        memset(&s_runtime_current, 0, sizeof(s_runtime_current));
        s_runtime_current.version = 0;
        s_runtime_next_page = 0;
    }

    /* --- Region B: config (v2.1) --- */
    FlashConfig_t c0, c1;
    bool cv0 = read_config_page(0, &c0);
    bool cv1 = read_config_page(1, &c1);

    if (cv0 && cv1) {
        if ((int32_t)(c0.version - c1.version) >= 0) {
            s_config_current = c0;
            s_config_next_page = 1;
        } else {
            s_config_current = c1;
            s_config_next_page = 0;
        }
        s_config_valid = true;
    } else if (cv0) {
        s_config_current = c0;
        s_config_next_page = 1;
        s_config_valid = true;
    } else if (cv1) {
        s_config_current = c1;
        s_config_next_page = 0;
        s_config_valid = true;
    } else {
        /* 首次上电或Flash损坏, 使用默认值 (由 app_data_init 填充) */
        memset(&s_config_current, 0, sizeof(s_config_current));
        s_config_current.version = 0;
        s_config_next_page = 0;
        s_config_valid = false;
    }

    return true;
}

/* ========================================================================= */
/*  Region A: Runtime API                                                    */
/* ========================================================================= */
bool flash_storage_save(uint32_t runtime_min)
{
    FlashParams_t new_params;
    new_params.version = s_runtime_current.version + 1;
    new_params.total_runtime_min = runtime_min;
    new_params.checksum = calc_runtime_checksum(&new_params);

    uint32_t addr = (s_runtime_next_page == 0) ? BSP_FLASH_PARAM_PAGE0 : BSP_FLASH_PARAM_PAGE1;

    if (HAL_FLASH_Unlock() != HAL_OK) return false;

    taskENTER_CRITICAL();
    bool ok = erase_page(addr);
    if (ok) {
        ok = write_words(addr, (const uint32_t *)&new_params,
                         sizeof(FlashParams_t) / sizeof(uint32_t));
    }
    taskEXIT_CRITICAL();

    HAL_FLASH_Lock();

    if (ok) {
        FlashParams_t readback;
        memcpy(&readback, (const void *)addr, sizeof(FlashParams_t));
        if (memcmp(&readback, &new_params, sizeof(FlashParams_t)) == 0) {
            s_runtime_current = new_params;
            s_runtime_next_page = (s_runtime_next_page == 0) ? 1 : 0;
        } else {
            ok = false;
        }
    }

    return ok;
}

uint32_t flash_storage_get_runtime(void)
{
    return s_runtime_current.total_runtime_min;
}

/* ========================================================================= */
/*  Region B: Config API (v2.1)                                              */
/* ========================================================================= */
bool flash_storage_save_config(const CalibrationData_t *cal, const FactoryLimits_t *lim)
{
    if (!cal || !lim) return false;

    FlashConfig_t new_config;
    memset(&new_config, 0, sizeof(new_config));
    new_config.version = s_config_current.version + 1;
    new_config.calibration = *cal;
    new_config.limits = *lim;
    new_config.checksum = calc_config_checksum(&new_config);

    uint32_t addr = (s_config_next_page == 0) ? BSP_FLASH_CONFIG_PAGE0 : BSP_FLASH_CONFIG_PAGE1;

    if (HAL_FLASH_Unlock() != HAL_OK) return false;

    taskENTER_CRITICAL();
    bool ok = erase_page(addr);
    if (ok) {
        ok = write_words(addr, (const uint32_t *)&new_config,
                         sizeof(FlashConfig_t) / sizeof(uint32_t));
    }
    taskEXIT_CRITICAL();

    HAL_FLASH_Lock();

    if (ok) {
        FlashConfig_t readback;
        memcpy(&readback, (const void *)addr, sizeof(FlashConfig_t));
        if (memcmp(&readback, &new_config, sizeof(FlashConfig_t)) == 0) {
            s_config_current = new_config;
            s_config_next_page = (s_config_next_page == 0) ? 1 : 0;
            s_config_valid = true;
        } else {
            ok = false;
        }
    }

    return ok;
}

bool flash_storage_load_config(CalibrationData_t *cal, FactoryLimits_t *lim)
{
    if (!cal || !lim) return false;
    if (!s_config_valid) return false;

    *cal = s_config_current.calibration;
    *lim = s_config_current.limits;
    return true;
}

bool flash_storage_has_config(void)
{
    return s_config_valid;
}

bool flash_storage_erase_config(void)
{
    if (HAL_FLASH_Unlock() != HAL_OK) return false;

    taskENTER_CRITICAL();
    bool ok1 = erase_page(BSP_FLASH_CONFIG_PAGE0);
    bool ok2 = erase_page(BSP_FLASH_CONFIG_PAGE1);
    taskEXIT_CRITICAL();

    HAL_FLASH_Lock();

    if (ok1 && ok2) {
        memset(&s_config_current, 0, sizeof(s_config_current));
        s_config_current.version = 0;
        s_config_next_page = 0;
        s_config_valid = false;
        return true;
    }
    return false;
}
