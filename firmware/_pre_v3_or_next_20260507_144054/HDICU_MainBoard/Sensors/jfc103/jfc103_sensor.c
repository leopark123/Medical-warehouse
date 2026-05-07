/**
 * @file    jfc103_sensor.c
 * @brief   JFC103 vital signs module — 88-byte realtime packet parser
 *
 *          Adaptive start strategy (confirmed by PCB testing):
 *          - Phase 1: Send 0x8A every 200ms until first valid frame received
 *          - Phase 2: Stop sending. Module continues transmitting autonomously.
 *          - If no data for 10s, return to Phase 1 to re-start module.
 *
 *          IMPORTANT: Continuously sending 0x8A resets the module's algorithm.
 *          HR/SpO2 will never converge if 0x8A is sent repeatedly.
 *          Only send 0x8A during startup or after timeout recovery.
 */

#include "jfc103_sensor.h"
#include "stm32f1xx_hal.h"
#include <string.h>

#define JFC103_FRAME_LEN    88
#define JFC103_TIMEOUT_MS   5000    /* 5s: ~4× the 1.28s report interval */
#define JFC103_RESTART_MS   10000   /* 10s: re-send 0x8A if no data */
#define JFC103_START_INTERVAL_MS 200 /* Phase 1: 0x8A every 200ms */
#define JFC103_HEADER       0xFF
#define JFC103_HR_OFFSET    65
#define JFC103_SPO2_OFFSET  66
#define JFC103_START_CMD    0x8A

extern void bsp_uart_jfc103_send(const uint8_t *data, uint16_t len);

static uint8_t s_buf[JFC103_FRAME_LEN];
static uint8_t s_idx;
static uint8_t s_heart_rate;
static uint8_t s_spo2;
static uint32_t s_last_valid_tick;
static uint32_t s_last_frame_tick;  /* Last time ANY frame was received */
static uint32_t s_last_send_tick;   /* Last time 0x8A was sent */
static bool s_valid;
static bool s_synced;
static bool s_data_flowing;         /* true = receiving frames, stop sending 0x8A */

void jfc103_sensor_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_idx = 0;
    s_heart_rate = 0;
    s_spo2 = 0;
    s_valid = false;
    s_synced = false;
    s_data_flowing = false;
    s_last_valid_tick = 0;
    s_last_frame_tick = 0;
    s_last_send_tick = 0;
}

void jfc103_sensor_start(void)
{
    /* Just reset the adaptive state — actual 0x8A sending happens in tick() */
    s_data_flowing = false;
    s_last_send_tick = 0;
}

/**
 * @brief Adaptive keepalive — call periodically from SensorTask (~100ms).
 *        Phase 1: sends 0x8A every 200ms until data flows.
 *        Phase 2: does nothing (module runs autonomously).
 *        Recovers from timeout by returning to Phase 1.
 */
void jfc103_sensor_tick(void)
{
    uint32_t now = HAL_GetTick();

    if (!s_data_flowing) {
        /* Phase 1: send 0x8A periodically to start module */
        if (now - s_last_send_tick >= JFC103_START_INTERVAL_MS) {
            uint8_t cmd = JFC103_START_CMD;
            bsp_uart_jfc103_send(&cmd, 1);
            s_last_send_tick = now;
        }
    } else {
        /* Phase 2: check for timeout → re-enter Phase 1 */
        if (s_last_frame_tick > 0 && (now - s_last_frame_tick > JFC103_RESTART_MS)) {
            s_data_flowing = false;     /* Lost connection, restart */
            s_valid = false;
        }
    }
}

void jfc103_sensor_rx_byte(uint8_t byte)
{
    if (byte == JFC103_HEADER) {
        s_buf[0] = byte;
        s_idx = 1;
        s_synced = true;
        return;
    }

    if (!s_synced) return;

    if (s_idx < JFC103_FRAME_LEN) {
        s_buf[s_idx++] = byte;
    }

    if (s_idx >= JFC103_FRAME_LEN) {
        /* Complete frame received */
        s_last_frame_tick = HAL_GetTick();

        /* Switch to Phase 2: stop sending 0x8A */
        if (!s_data_flowing) {
            s_data_flowing = true;
        }

        s_heart_rate = s_buf[JFC103_HR_OFFSET];
        s_spo2 = s_buf[JFC103_SPO2_OFFSET];

        if (s_heart_rate > 0 && s_heart_rate <= 250 &&
            s_spo2 > 0 && s_spo2 <= 100) {
            s_last_valid_tick = HAL_GetTick();
            s_valid = true;
        }
        /* Don't clear s_valid on frames with HR=0 — module may still be warming up.
         * Timeout mechanism handles actual loss of signal. */

        s_synced = false;
        s_idx = 0;
    }
}

uint8_t jfc103_get_heart_rate(void) { return s_heart_rate; }
uint8_t jfc103_get_spo2(void) { return s_spo2; }

bool jfc103_is_valid(void)
{
    if (s_valid && (HAL_GetTick() - s_last_valid_tick > JFC103_TIMEOUT_MS)) {
        s_valid = false;
    }
    return s_valid;
}
