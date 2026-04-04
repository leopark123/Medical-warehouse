/**
 * @file    jfc103_sensor.c
 * @brief   JFC103 vital signs module — 88-byte realtime packet parser
 *
 *          Frame format (confirmed from JFC103 spec v1.3 page 10):
 *          Byte[0]    = 0xFF (header — ONLY 0xFF in the packet)
 *          Byte[1-64] = acdata[64] (signed int8, ECG waveform, -128~+127)
 *          Byte[65]   = heartrate (bpm)
 *          Byte[66]   = spo2 (%)
 *          Byte[67]   = bk (microcirculation)
 *          Byte[68]   = rsv[0] (fatigue index)
 *          Byte[69-70]= rsv[1-2] (reserved)
 *          Byte[71]   = rsv[3] (systolic BP — NOT USED, cloud-only)
 *          Byte[72]   = rsv[4] (diastolic BP — NOT USED, cloud-only)
 *          Byte[73]   = rsv[5] (cardiac output)
 *          Byte[74]   = rsv[6] (peripheral resistance)
 *          Byte[75]   = rsv[7] (RR interval)
 *          Byte[76]   = SDNN (HRV)
 *          Byte[77]   = RMSSD
 *          Byte[78]   = NN50
 *          Byte[79]   = PNN50
 *          Byte[80-85]= rra[6] (RR intervals)
 *          Byte[86-87]= rsv2[2] (reserved)
 *
 *          Key constraint: 0xFF only appears as header (byte[0]).
 *          No other byte in the packet equals 0xFF.
 *          This allows reliable frame synchronization.
 *
 *          Period: every 1.28 seconds (64 samples at 50Hz)
 *          Start command: send 0x8A
 *          Stop command: send 0x88
 *
 *          For this project: only heartrate and spo2 are used.
 *          BP/RR fields are cloud-only features, filled 0 in protocol output.
 */

#include "jfc103_sensor.h"
#include <string.h>

#define JFC103_FRAME_LEN    88
#define JFC103_HEADER       0xFF
#define JFC103_HR_OFFSET    65
#define JFC103_SPO2_OFFSET  66
#define JFC103_START_CMD    0x8A
#define JFC103_STOP_CMD     0x88

extern void bsp_uart_jfc103_send(const uint8_t *data, uint16_t len);

static uint8_t s_buf[JFC103_FRAME_LEN];
static uint8_t s_idx;
static uint8_t s_heart_rate;
static uint8_t s_spo2;
static bool s_valid;
static bool s_synced;

void jfc103_sensor_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_idx = 0;
    s_heart_rate = 0;
    s_spo2 = 0;
    s_valid = false;
    s_synced = false;
}

void jfc103_sensor_start(void)
{
    uint8_t cmd = JFC103_START_CMD;
    bsp_uart_jfc103_send(&cmd, 1);
}

void jfc103_sensor_rx_byte(uint8_t byte)
{
    /*
     * Frame sync strategy (from spec: "实时包以0xFF打头，数据包中不会出现其他0xFF数据"):
     * - If we see 0xFF, it's ALWAYS the start of a new frame
     * - Reset buffer index to 0 and start collecting
     */

    if (byte == JFC103_HEADER) {
        /* Start of new frame — regardless of current state */
        s_buf[0] = byte;
        s_idx = 1;
        s_synced = true;
        return;
    }

    if (!s_synced) {
        return;     /* Haven't seen header yet, discard */
    }

    if (s_idx < JFC103_FRAME_LEN) {
        s_buf[s_idx++] = byte;
    }

    if (s_idx >= JFC103_FRAME_LEN) {
        /* Complete frame received */
        s_heart_rate = s_buf[JFC103_HR_OFFSET];
        s_spo2 = s_buf[JFC103_SPO2_OFFSET];

        /* Basic validity check: physiological range */
        if (s_heart_rate > 0 && s_heart_rate <= 250 &&
            s_spo2 > 0 && s_spo2 <= 100) {
            s_valid = true;
        } else {
            s_valid = false;
        }

        s_synced = false;   /* Wait for next 0xFF */
        s_idx = 0;
    }
}

uint8_t jfc103_get_heart_rate(void) { return s_heart_rate; }
uint8_t jfc103_get_spo2(void) { return s_spo2; }
bool jfc103_is_valid(void) { return s_valid; }
