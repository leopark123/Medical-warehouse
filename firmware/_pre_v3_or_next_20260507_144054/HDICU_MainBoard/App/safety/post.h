/**
 * @file    post.h
 * @brief   Power-On Self-Test (POST) — run before task creation
 *
 *          Checks (in order):
 *            1. RAM pattern test (walking 0x55/0xAA on scratch region)
 *            2. Stack canary at known location
 *            3. Critical peripheral clocks enabled
 *            4. IWDG confirmed running (non-zero counter decrement)
 *            5. Flash CRC (optional, skipped if not configured)
 *
 *          Return codes: 0 = OK, non-zero = error code (see .c)
 *          On failure, caller should invoke safety_fatal(SAFETY_EVT_POST_FAIL).
 */
#ifndef POST_H
#define POST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POST_OK             = 0,
    POST_ERR_RAM        = 0x01,
    POST_ERR_STACK      = 0x02,
    POST_ERR_CLOCK      = 0x03,
    POST_ERR_IWDG       = 0x04,
    POST_ERR_FLASH_CRC  = 0x05,
} PostResult_t;

/* Run POST. Returns POST_OK (0) on success, error code on failure. */
PostResult_t post_run(void);

#ifdef __cplusplus
}
#endif

#endif /* POST_H */
