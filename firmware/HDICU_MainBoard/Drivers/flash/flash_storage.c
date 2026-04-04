/**
 * @file    flash_storage.c
 * @brief   Dual-page flash parameter storage with wear leveling
 * @note    Pages: 0x0807F000 (page 254) and 0x0807F800 (page 255)
 *          STM32F103VET6: 2KB per page, word (32-bit) programming
 *          Wear leveling: alternating pages, version counter selects latest
 */

#include "flash_storage.h"
#include "bsp_config.h"
#include "stm32f1xx_hal.h"
#include <string.h>

static FlashParams_t s_current;
static uint8_t s_next_page;     /* 0 or 1: which page to write next */

static uint32_t calc_checksum(const FlashParams_t *p)
{
    const uint8_t *bytes = (const uint8_t *)p;
    uint32_t sum = 0;
    for (size_t i = 0; i < offsetof(FlashParams_t, checksum); i++) {
        sum += bytes[i];
    }
    return sum;
}

static bool read_page(uint8_t page, FlashParams_t *out)
{
    uint32_t addr = (page == 0) ? BSP_FLASH_PARAM_PAGE0 : BSP_FLASH_PARAM_PAGE1;
    memcpy(out, (const void *)addr, sizeof(FlashParams_t));
    return (out->checksum == calc_checksum(out) && out->version != 0xFFFFFFFF);
}

/**
 * @brief Erase one flash page (2KB)
 * @return true on success
 */
static bool erase_page(uint32_t page_addr)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = page_addr;
    erase.NbPages = 1;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        return false;
    }
    return (page_error == 0xFFFFFFFF);  /* 0xFFFFFFFF = no error */
}

/**
 * @brief Write a FlashParams_t struct to flash, word by word
 * @return true on success
 */
static bool write_params(uint32_t addr, const FlashParams_t *params)
{
    /* FlashParams_t is guaranteed 4-byte aligned by _Static_assert in header.
     * No non-aligned handling needed — cast is always safe. */
    const uint32_t *src = (const uint32_t *)params;
    const size_t words = sizeof(FlashParams_t) / sizeof(uint32_t);

    for (size_t i = 0; i < words; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              addr + i * 4, src[i]) != HAL_OK) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Verify written data by reading back and comparing
 */
static bool verify_write(uint32_t addr, const FlashParams_t *expected)
{
    FlashParams_t readback;
    memcpy(&readback, (const void *)addr, sizeof(FlashParams_t));
    return (memcmp(&readback, expected, sizeof(FlashParams_t)) == 0);
}

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */

bool flash_storage_init(void)
{
    FlashParams_t p0, p1;
    bool v0 = read_page(0, &p0);
    bool v1 = read_page(1, &p1);

    if (v0 && v1) {
        if (p0.version >= p1.version) {
            s_current = p0;
            s_next_page = 1;
        } else {
            s_current = p1;
            s_next_page = 0;
        }
    } else if (v0) {
        s_current = p0;
        s_next_page = 1;
    } else if (v1) {
        s_current = p1;
        s_next_page = 0;
    } else {
        memset(&s_current, 0, sizeof(s_current));
        s_current.version = 0;
        s_next_page = 0;
        return false;
    }

    return true;
}

bool flash_storage_save(uint32_t runtime_min)
{
    FlashParams_t new_params;
    new_params.version = s_current.version + 1;
    new_params.total_runtime_min = runtime_min;
    new_params.checksum = calc_checksum(&new_params);

    uint32_t addr = (s_next_page == 0) ? BSP_FLASH_PARAM_PAGE0 : BSP_FLASH_PARAM_PAGE1;

    /* Unlock flash */
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    /* Erase target page */
    bool ok = erase_page(addr);

    /* Program word by word */
    if (ok) {
        ok = write_params(addr, &new_params);
    }

    /* Lock flash (always, even on failure) */
    HAL_FLASH_Lock();

    /* Verify by readback */
    if (ok) {
        ok = verify_write(addr, &new_params);
    }

    if (ok) {
        /* Success: update state */
        s_current = new_params;
        s_next_page = (s_next_page == 0) ? 1 : 0;
    }
    /* On failure: s_current and s_next_page unchanged — next save retries same page */

    return ok;
}

uint32_t flash_storage_get_runtime(void)
{
    return s_current.total_runtime_min;
}
