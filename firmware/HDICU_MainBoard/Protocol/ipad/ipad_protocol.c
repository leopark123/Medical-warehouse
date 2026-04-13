/**
 * @file    ipad_protocol.c
 * @brief   iPad APP communication — frame handling and command dispatch
 *
 *          Priority (high→low): 0x02 > 0x06 > 0x04 > 0xFF
 *          (0x08 not in this phase, so excluded from priority)
 *
 *          Frame-level errors (checksum, length) → silent discard.
 *          Protocol-level errors (unknown cmd, param OOB) → 0xFF response.
 */

#include "ipad_protocol.h"
#include "protocol_defs.h"
#include "app_data.h"
#include "control_timers.h"
#include "stm32f1xx_hal.h"
#include <string.h>

/* Provided by uart_driver.c */
extern void bsp_uart_ipad_send(const uint8_t *data, uint16_t len);

/* ========================================================================= */
/*  Internal State                                                           */
/* ========================================================================= */
static FrameParser_t s_parser;
static uint32_t s_last_rx_time_ms;
static bool s_connected;
static uint8_t s_tx_buf[256];

/* ========================================================================= */
/*  Response Builders                                                        */
/* ========================================================================= */

/**
 * @brief Build 0x02 response — cabin realtime parameters (34 bytes)
 *        Frozen spec 7.6 / 冻结总纲 3.6
 */
static void build_params_response(void)
{
    AppData_t *d = app_data_get();
    uint8_t payload[IPAD_PARAMS_RSP_LEN];
    memset(payload, 0, sizeof(payload));

    /* Snapshot shared fields under lock for a consistent response */
    int16_t  s_temp_avg;
    uint16_t s_humidity_raw, s_o2_raw, s_co2_ppm;
    uint16_t s_fog_rem, s_disinf_rem, s_o2_accum;
    uint8_t  s_fan_speed, s_nursing_level, s_switch_status, s_light_status;
    uint32_t s_runtime_min;

    app_data_lock();
    s_temp_avg      = d->sensor.temperature_avg;
    s_humidity_raw  = d->sensor.humidity_raw;
    s_o2_raw        = d->sensor.o2_raw;
    s_co2_ppm       = d->sensor.co2_ppm;
    s_fog_rem       = d->control.fog_remaining;
    s_disinf_rem    = d->control.disinfect_remaining;
    s_fan_speed     = d->control.fan_speed_actual;
    s_nursing_level = d->control.nursing_level_actual;
    s_switch_status = d->control.switch_status;
    s_light_status  = d->control.light_status;
    s_o2_accum      = d->control.o2_accumulated;
    s_runtime_min   = d->system.total_runtime_min;
    app_data_unlock();

    /* Build packet from local snapshot — no lock needed */

    /* Bytes 0-1: Realtime temperature (x10) */
    payload[0] = (uint8_t)(s_temp_avg >> 8);
    payload[1] = (uint8_t)(s_temp_avg & 0xFF);

    /* Bytes 2-3: Realtime humidity (x10) */
    payload[2] = (uint8_t)(s_humidity_raw >> 8);
    payload[3] = (uint8_t)(s_humidity_raw & 0xFF);

    /* Bytes 4-5: Realtime O2 (x10) */
    payload[4] = (uint8_t)(s_o2_raw >> 8);
    payload[5] = (uint8_t)(s_o2_raw & 0xFF);

    /* Bytes 6-7: Realtime CO2 (ppm) */
    payload[6] = (uint8_t)(s_co2_ppm >> 8);
    payload[7] = (uint8_t)(s_co2_ppm & 0xFF);

    /* Bytes 8-9: Remaining fog time (seconds) */
    payload[8] = (uint8_t)(s_fog_rem >> 8);
    payload[9] = (uint8_t)(s_fog_rem & 0xFF);

    /* Bytes 10-11: Remaining disinfect time */
    payload[10] = (uint8_t)(s_disinf_rem >> 8);
    payload[11] = (uint8_t)(s_disinf_rem & 0xFF);

    /* Byte 12: Current fan speed */
    payload[12] = s_fan_speed;

    /* Byte 13: Current nursing level */
    payload[13] = s_nursing_level;

    /* Byte 14: Inner/outer cycle status (SW_BIT_INNER_CYCLE) */
    payload[14] = (s_switch_status & SW_BIT_INNER_CYCLE) ? 1 : 0;

    /* Byte 15: Fresh air status (SW_BIT_FRESH_AIR) */
    payload[15] = (s_switch_status & SW_BIT_FRESH_AIR) ? 1 : 0;

    /* Byte 16: Open O2 status (SW_BIT_OPEN_O2) */
    payload[16] = (s_switch_status & SW_BIT_OPEN_O2) ? 1 : 0;

    /* Byte 17: Light status */
    payload[17] = s_light_status;

    /* Bytes 18-19: O2 accumulated time (seconds) */
    payload[18] = (uint8_t)(s_o2_accum >> 8);
    payload[19] = (uint8_t)(s_o2_accum & 0xFF);

    /* Bytes 20-23: Total runtime (minutes), uint32 big-endian */
    payload[20] = (uint8_t)(s_runtime_min >> 24);
    payload[21] = (uint8_t)(s_runtime_min >> 16);
    payload[22] = (uint8_t)(s_runtime_min >> 8);
    payload[23] = (uint8_t)(s_runtime_min & 0xFF);

    /* Bytes 24-33: Reserved, already zeroed */

    uint16_t frame_len = frame_build_ipad(s_tx_buf, IPAD_RSP_PARAMS, payload, IPAD_PARAMS_RSP_LEN);
    bsp_uart_ipad_send(s_tx_buf, frame_len);
}

/**
 * @brief Parse and apply 0x03 write parameters (22 bytes)
 *        Frozen spec: any single param OOB → reject ALL, return error.
 *        All params full overwrite.
 */
static void handle_write_params(const uint8_t *data, uint8_t len)
{
    uint8_t ack[2];

    if (len != IPAD_WRITE_DATA_LEN) {
        /* Length mismatch at protocol level — return error */
        ack[0] = IPAD_WRITE_CMD_ERR;
        ack[1] = IPAD_ERR_NONE;
        uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_WRITE_ACK, ack, 2);
        bsp_uart_ipad_send(s_tx_buf, flen);
        return;
    }

    /* Parse all 22 bytes */
    uint16_t temp     = ((uint16_t)data[0] << 8) | data[1];
    uint16_t humid    = ((uint16_t)data[2] << 8) | data[3];
    uint16_t o2       = ((uint16_t)data[4] << 8) | data[5];
    uint16_t co2      = ((uint16_t)data[6] << 8) | data[7];
    uint16_t fog      = ((uint16_t)data[8] << 8) | data[9];
    uint16_t disinf   = ((uint16_t)data[10] << 8) | data[11];
    uint8_t  fan      = data[12];
    uint8_t  nursing  = data[13];
    uint8_t  cycle    = data[14];
    uint8_t  fresh    = data[15];
    uint8_t  open_o2  = data[16];
    uint8_t  light    = data[17];
    /* bytes 18-21: reserved, ignore */

    /* Validate ALL 22 bytes — any single field OOB rejects the ENTIRE packet.
     * Frozen spec: "任一参数超限则全部不更新" */
    uint8_t err_code = IPAD_ERR_NONE;

    if (temp < 100 || temp > 400)        err_code = IPAD_ERR_TEMP_OOB;
    else if (humid < 300 || humid > 900) err_code = IPAD_ERR_HUMID_OOB;
    else if (o2 < 210 || o2 > 1000)      err_code = IPAD_ERR_O2_OOB;
    else if (co2 > 5000)                 err_code = IPAD_ERR_CO2_OOB;
    else if (fog > 3600)                 err_code = IPAD_ERR_FOG_OOB;
    else if (disinf > 3600)              err_code = IPAD_ERR_DISINFECT_OOB;
    else if (fan > 3)                    err_code = IPAD_ERR_FAN_OOB;
    else if (nursing < 1 || nursing > 3) err_code = IPAD_ERR_NURSING_OOB;
    else if (cycle > 1)                  err_code = IPAD_ERR_NURSING_OOB; /* reuse code; boolean OOB */
    else if (fresh > 1)                  err_code = IPAD_ERR_NURSING_OOB;
    else if (open_o2 > 1)               err_code = IPAD_ERR_NURSING_OOB;
    else if (light > 0x0F)              err_code = IPAD_ERR_NURSING_OOB;

    if (err_code != IPAD_ERR_NONE) {
        /* Reject entire packet — do NOT update any setpoint */
        ack[0] = IPAD_WRITE_PARAM_OOB;
        ack[1] = err_code;
    } else {
        /* All valid — full overwrite (every field, no exceptions) */
        AppData_t *d = app_data_get();
        app_data_lock();
        d->setpoint.target_temp     = temp;
        d->setpoint.target_humidity = humid;
        d->setpoint.target_o2       = o2;
        d->setpoint.target_co2      = co2;
        d->setpoint.fog_time        = fog;
        d->setpoint.disinfect_time  = disinf;
        d->setpoint.fan_speed       = fan;
        d->setpoint.nursing_level   = nursing;
        d->setpoint.inner_cycle     = cycle;
        d->setpoint.fresh_air       = fresh;
        d->setpoint.open_o2         = open_o2;
        d->setpoint.light_ctrl      = light;

        /* Start/stop fog and disinfect timers (interlock pre-checked inside).
         * fog=0 stops, fog>0 starts with that duration. Same for disinfect. */
        control_timers_start_fog(d, fog);
        control_timers_start_disinfect(d, disinf);

        app_data_unlock();

        ack[0] = IPAD_WRITE_OK;
        ack[1] = IPAD_ERR_NONE;
    }

    uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_WRITE_ACK, ack, 2);
    bsp_uart_ipad_send(s_tx_buf, flen);
}

/**
 * @brief Build 0x06 response — vitals data (20 bytes)
 *        JFC103 only provides heart_rate + SpO2.
 *        Bytes 6~13 (BP/RR) are FIXED ZERO this phase.
 */
static void build_vitals_response(void)
{
    AppData_t *d = app_data_get();
    uint8_t payload[IPAD_VITALS_RSP_LEN];
    memset(payload, 0, sizeof(payload));

    /* Bytes 0-1: Heart rate */
    payload[0] = 0;
    payload[1] = d->sensor.heart_rate;

    /* Bytes 2-3: Pulse (= heart rate for JFC103) */
    payload[2] = 0;
    payload[3] = d->sensor.heart_rate;

    /* Bytes 4-5: SpO2 */
    payload[4] = 0;
    payload[5] = d->sensor.spo2;

    /* Bytes 6-13: BP + RR — FIXED ZERO (JFC103 does not support) */
    /* Already zeroed by memset */

    /* Bytes 14-19: Reserved — already zeroed */

    uint16_t frame_len = frame_build_ipad(s_tx_buf, IPAD_RSP_VITALS, payload, IPAD_VITALS_RSP_LEN);
    bsp_uart_ipad_send(s_tx_buf, frame_len);
}

/**
 * @brief Send 0xFF error response
 */
static void send_error_response(uint8_t error_type, uint8_t error_pos)
{
    uint8_t payload[2];
    payload[0] = error_type;
    payload[1] = error_pos;
    uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_ERROR, payload, 2);
    bsp_uart_ipad_send(s_tx_buf, flen);
}

/* ========================================================================= */
/*  Command Dispatch                                                         */
/* ========================================================================= */

static void dispatch_command(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    switch (cmd) {
    case IPAD_CMD_READ_PARAMS:
        /* 0x01 → respond with 0x02 */
        build_params_response();
        break;

    case IPAD_CMD_WRITE_PARAMS:
        /* 0x03 → validate & apply, respond with 0x04 */
        handle_write_params(data, len);
        break;

    case IPAD_CMD_READ_VITALS:
        /* 0x05 → respond with 0x06 */
        build_vitals_response();
        break;

    case IPAD_CMD_READ_CURVE:
        /* 0x07 → RESERVED this phase.
         * Return error: command not supported for now. */
        send_error_response(IPAD_EXCEPT_CMD_UNSUPPORTED, IPAD_EPOS_CMD);
        break;

    default:
        /* Unknown command → 0xFF */
        send_error_response(IPAD_EXCEPT_CMD_UNSUPPORTED, IPAD_EPOS_CMD);
        break;
    }
}

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */

void ipad_protocol_init(void)
{
    frame_parser_init(&s_parser, false);    /* iPad: single header */
    s_last_rx_time_ms = 0;
    s_connected = false;
}

void ipad_protocol_rx_byte(uint8_t byte)
{
    if (frame_parser_feed(&s_parser, byte)) {
        /* Complete valid frame received */
        s_last_rx_time_ms = HAL_GetTick();
        s_connected = true;
        dispatch_command(s_parser.cmd, s_parser.data, s_parser.len);
        /* Reset parser for next frame */
        frame_parser_init(&s_parser, false);
    }
    /* If parser rejects (checksum/length/tail error), it auto-resets.
     * Per frozen spec: frame-level errors → silent discard, no 0xFF. */
}

void ipad_protocol_tick(uint32_t now_ms)
{
    if (s_connected && (now_ms - s_last_rx_time_ms > IPAD_DISCONNECT_TIMEOUT_MS)) {
        s_connected = false;
        /* iPad disconnect is informational, not a hard alarm.
         * Screen disconnect triggers ALARM_COMM_FAULT. */
    }
}

bool ipad_protocol_is_connected(void)
{
    return s_connected;
}
