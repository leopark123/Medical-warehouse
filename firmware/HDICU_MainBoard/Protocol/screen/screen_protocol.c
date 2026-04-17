/**
 * @file    screen_protocol.c
 * @brief   Screen board communication — send display data + receive commands
 */

#include "screen_protocol.h"
#include "bsp_config.h"
#include "control_timers.h"
#include <string.h>

extern void bsp_uart_screen_send(const uint8_t *data, uint16_t len);

static FrameParser_t s_parser;
static uint32_t s_last_rx_time_ms;
static bool s_connected;
static uint8_t s_tx_buf[64];

/* ========================================================================= */
/*  Outgoing: Main → Screen                                                  */
/* ========================================================================= */

void screen_send_display_data(void)
{
    AppData_t *d = app_data_get();
    uint8_t payload[SCR_DISPLAY_DATA_LEN];
    memset(payload, 0, sizeof(payload));

    /* Snapshot shared fields under lock for a consistent packet */
    int16_t  s_temp_avg;
    uint8_t  s_humidity, s_o2_percent, s_heart_rate, s_spo2;
    uint16_t s_co2_ppm;
    uint8_t  s_fan_speed, s_nursing_level, s_light_status, s_switch_status;
    uint16_t s_fog_rem, s_disinf_rem, s_o2_accum, s_relay_status;
    uint16_t s_alarm_flags;

    app_data_lock();
    s_temp_avg      = d->sensor.temperature_avg;
    s_humidity      = d->sensor.humidity;
    s_o2_percent    = d->sensor.o2_percent;
    s_co2_ppm       = d->sensor.co2_ppm;
    s_heart_rate    = d->sensor.heart_rate;
    s_spo2          = d->sensor.spo2;
    s_fan_speed     = d->control.fan_speed_actual;
    s_nursing_level = d->control.nursing_level_actual;
    s_fog_rem       = d->control.fog_remaining;
    s_disinf_rem    = d->control.disinfect_remaining;
    s_o2_accum      = d->control.o2_accumulated;
    s_relay_status  = d->control.relay_status;
    s_light_status  = d->control.light_status;
    s_switch_status = d->control.switch_status;
    s_alarm_flags   = d->alarm.alarm_flags;
    app_data_unlock();

    /* Build packet from local snapshot — no lock needed */

    /* Bytes 0-1: Realtime temperature (int16, x10) */
    payload[0] = (uint8_t)(s_temp_avg >> 8);
    payload[1] = (uint8_t)(s_temp_avg & 0xFF);

    /* Byte 2: Realtime humidity (uint8, integer %) */
    payload[2] = s_humidity;

    /* Byte 3: Realtime O2 (uint8, integer %) */
    payload[3] = s_o2_percent;

    /* Bytes 4-5: Realtime CO2 (uint16, ppm) */
    payload[4] = (uint8_t)(s_co2_ppm >> 8);
    payload[5] = (uint8_t)(s_co2_ppm & 0xFF);

    /* Byte 6: Heart rate */
    payload[6] = s_heart_rate;

    /* Byte 7: SpO2 */
    payload[7] = s_spo2;

    /* Byte 8: Fan speed */
    payload[8] = s_fan_speed;

    /* Byte 9: Nursing level */
    payload[9] = s_nursing_level;

    /* Bytes 10-11: Fog remaining (seconds) */
    payload[10] = (uint8_t)(s_fog_rem >> 8);
    payload[11] = (uint8_t)(s_fog_rem & 0xFF);

    /* Bytes 12-13: Disinfect remaining */
    payload[12] = (uint8_t)(s_disinf_rem >> 8);
    payload[13] = (uint8_t)(s_disinf_rem & 0xFF);

    /* Bytes 14-15: O2 accumulated time */
    payload[14] = (uint8_t)(s_o2_accum >> 8);
    payload[15] = (uint8_t)(s_o2_accum & 0xFF);

    /* Bytes 16-17: Relay status bitmap */
    payload[16] = (uint8_t)(s_relay_status >> 8);
    payload[17] = (uint8_t)(s_relay_status & 0xFF);

    /* Byte 18: Light status */
    payload[18] = s_light_status;

    /* Byte 19: Switch status */
    payload[19] = s_switch_status;

    /* Bytes 20-21: Alarm flags */
    payload[20] = (uint8_t)(s_alarm_flags >> 8);
    payload[21] = (uint8_t)(s_alarm_flags & 0xFF);

    /* Bytes 22-25: Reserved (zeroed) */

    uint16_t flen = frame_build_screen(s_tx_buf, SCR_CMD_DISPLAY_DATA, payload, SCR_DISPLAY_DATA_LEN);
    bsp_uart_screen_send(s_tx_buf, flen);
}

void screen_send_heartbeat(void)
{
    AppData_t *d = app_data_get();
    uint8_t payload[8];

    /* Bytes 0-3: Total runtime (seconds) */
    uint32_t rt = d->system.total_runtime_min * 60;
    payload[0] = (uint8_t)(rt >> 24);
    payload[1] = (uint8_t)(rt >> 16);
    payload[2] = (uint8_t)(rt >> 8);
    payload[3] = (uint8_t)(rt & 0xFF);

    /* Bytes 4-7: This boot uptime (seconds) */
    payload[4] = (uint8_t)(d->system.boot_uptime_sec >> 24);
    payload[5] = (uint8_t)(d->system.boot_uptime_sec >> 16);
    payload[6] = (uint8_t)(d->system.boot_uptime_sec >> 8);
    payload[7] = (uint8_t)(d->system.boot_uptime_sec & 0xFF);

    uint16_t flen = frame_build_screen(s_tx_buf, SCR_CMD_HEARTBEAT, payload, 8);
    bsp_uart_screen_send(s_tx_buf, flen);
}

/* ========================================================================= */
/*  Incoming: Screen → Main                                                  */
/* ========================================================================= */

static void dispatch_screen_command(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    AppData_t *d = app_data_get();

    switch (cmd) {
    case SCR_CMD_PARAM_SET: {
        /* 0x81: ParamID(1B) + Value(2B) */
        if (len < 3) break;
        uint8_t param_id = data[0];
        uint16_t value = ((uint16_t)data[1] << 8) | data[2];
        switch (param_id) {
        case 0x01: if (value >= 100 && value <= 400) d->setpoint.target_temp = value; break;
        case 0x02: if (value >= 300 && value <= 900) d->setpoint.target_humidity = value; break;
        case 0x03: if (value >= 210 && value <= 1000) d->setpoint.target_o2 = value; break;
        case 0x04: if (value <= 3) d->setpoint.fan_speed = (uint8_t)value; break;
        case 0x05: if (value >= 1 && value <= 3) d->setpoint.nursing_level = (uint8_t)value; break;
        default: break;
        }
        break;
    }

    case SCR_CMD_KEY_ACTION: {
        /* 0x82: KeyID(1B) + ActionType(1B)
         * Key IDs per spec (功能表.pdf + frozen spec 5.5.2):
         * 0x01=护理等级灯(#11)   0x02=照明灯(#8 bit1)   0x03=检查灯(#8 bit0)
         * 0x04=红蓝光LED治疗灯(#8 bit2+bit3, CN36 L-LMP)
         * 0x05=紫外灯(#6 启动/停止消毒定时周期, NOT direct relay toggle)
         * 0x06=开放式供氧(#12)   0x07=内/外循环(#9)     0x08=新风净化(#13)
         * 0x09=报警确认          0x0A=编码器按下
         * ActionType: 0x01=单击 0x02=长按 0x03=按下 0x04=松开 */
        if (len < 2) break;
        uint8_t key_id = data[0];
        uint8_t action = data[1];
        /* Lock shared data for all key actions that modify setpoints/relay_status.
         * This prevents torn read-modify-write with ControlTask. */
        app_data_lock();

        /* --- Single click actions --- */
        if (action == 0x01) {
            switch (key_id) {
            case 0x01: /* 护理等级灯: cycle 1→2→3→1 */
                d->setpoint.nursing_level = (d->setpoint.nursing_level % 3) + 1;
                break;
            case 0x02: /* 照明灯 toggle (light_ctrl bit1) */
                d->setpoint.light_ctrl ^= 0x02;
                break;
            case 0x03: /* 检查灯 toggle (light_ctrl bit0) */
                d->setpoint.light_ctrl ^= 0x01;
                break;
            case 0x04: /* 红蓝光 LED治疗灯 — CN36 L-LMP 控制板
                        * 功能表 #8: 检查灯/照明/蓝光/红光 为同一控制板的4个灯。
                        * "红蓝光"面板按钮同时控制 蓝(bit2) + 红(bit3) 治疗LED。
                        * 低压LED经 light_ctrl 位图驱动，非220V红外灯继电器。 */
                d->setpoint.light_ctrl ^= 0x0C;  /* bit2=蓝, bit3=红 */
                break;

            case 0x05: /* 紫外灯 = 启动/停止当前消毒定时周期
                        * 功能表 #6 规定: 紫外灯只能由消毒定时器触发。
                        * 本按键不直接翻转继电器，而是切换定时器状态：
                        *   有计时在进行 → 立即停止（关闭UV）
                        *   有预设时长但未启动 → 启动定时周期（由互锁检查）
                        *   未预设时长 → 忽略（防止常亮安全风险） */
                if (d->control.disinfect_remaining > 0) {
                    control_timers_start_disinfect(d, 0);  /* 0 = stop + OFF */
                } else if (d->setpoint.disinfect_time > 0) {
                    control_timers_start_disinfect(d, d->setpoint.disinfect_time);
                }
                /* else: no preset time → ignore for safety */
                break;
            case 0x06: /* 开放式供氧 toggle */
                d->setpoint.open_o2 = d->setpoint.open_o2 ? 0 : 1;
                break;
            case 0x07: /* 内/外循环 toggle */
                d->setpoint.inner_cycle = d->setpoint.inner_cycle ? 0 : 1;
                break;
            case 0x08: /* 新风净化 toggle */
                d->setpoint.fresh_air = d->setpoint.fresh_air ? 0 : 1;
                break;
            case 0x09: /* 报警确认 */
                d->alarm.acknowledged = true;
                break;
            case 0x0A: /* 编码器按下 — confirm current parameter edit */
                /* Encoder press is handled by HMI state machine (screen board side).
                 * Main controller receives this as notification only. */
                break;
            default:
                break;
            }
        }
        /* --- Long press actions (>2s) --- */
        else if (action == 0x02) {
            switch (key_id) {
            case 0x0A: /* 编码器长按3秒 = 供氧累计时间清零 (frozen spec 4.4) */
            {
                extern void control_timers_reset_o2_accum(AppData_t *d);
                control_timers_reset_o2_accum(d);
                break;
            }
            default:
                break;
            }
        }
        app_data_unlock();
        break;
    }

    case SCR_CMD_TIMER_CTRL: {
        /* 0x83: TimerType(1B) + Cmd(1B) + Duration(2B)
         * TimerType: 0x01=雾化 0x02=消毒 0x03=供氧累计
         * Cmd: 0x01=开始 0x02=停止 0x03=清零 */
        if (len < 4) break;
        uint8_t timer_type = data[0];
        uint8_t timer_cmd  = data[1];
        uint16_t duration  = ((uint16_t)data[2] << 8) | data[3];

        extern void control_timers_start_fog(AppData_t *d, uint16_t duration_sec);
        extern void control_timers_start_disinfect(AppData_t *d, uint16_t duration_sec);
        extern void control_timers_reset_o2_accum(AppData_t *d);

        /* Lock shared data — timer functions modify relay_status / control fields */
        app_data_lock();
        switch (timer_type) {
        case 0x01: /* 雾化 */
            if (timer_cmd == 0x01)      control_timers_start_fog(d, duration);
            else if (timer_cmd == 0x02) control_timers_start_fog(d, 0);
            break;
        case 0x02: /* 消毒 */
            if (timer_cmd == 0x01)      control_timers_start_disinfect(d, duration);
            else if (timer_cmd == 0x02) control_timers_start_disinfect(d, 0);
            break;
        case 0x03: /* 供氧累计 */
            if (timer_cmd == 0x03)      control_timers_reset_o2_accum(d);
            break;
        default:
            break;
        }
        app_data_unlock();
        break;
    }

    case SCR_CMD_HEARTBEAT_ACK:
        /* 0x84: Screen acknowledged our heartbeat */
        s_last_rx_time_ms = HAL_GetTick();
        s_connected = true;
        break;

    case SCR_CMD_ALARM_ACK: {
        /* 0x85: AlarmID(1B)
         * 0xFF = acknowledge all alarms
         * Per frozen spec: alarm clears when BOTH parameter normal AND acknowledged.
         * Here we just set the acknowledged flag; AlarmTask handles the actual clearing. */
        if (len < 1) break;
        d->alarm.acknowledged = true;
        break;
    }

    default:
        break;
    }
}

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */

void screen_protocol_init(void)
{
    frame_parser_init(&s_parser, true);     /* Screen: double header AA 55 */
    s_last_rx_time_ms = 0;
    s_connected = false;
}

void screen_protocol_rx_byte(uint8_t byte)
{
    if (frame_parser_feed(&s_parser, byte)) {
        s_last_rx_time_ms = HAL_GetTick();
        s_connected = true;
        dispatch_screen_command(s_parser.cmd, s_parser.data, s_parser.len);
        frame_parser_init(&s_parser, true);
    }
}

void screen_protocol_tick(uint32_t now_ms)
{
    if (s_connected && (now_ms - s_last_rx_time_ms > SCR_HEARTBEAT_TIMEOUT_MS)) {
        s_connected = false;
        app_data_get()->alarm.alarm_flags |= ALARM_COMM_FAULT;
    }
}

bool screen_protocol_is_connected(void)
{
    return s_connected;
}

uint32_t screen_protocol_last_frame_tick(void)
{
    return s_last_rx_time_ms;
}

void screen_send_encoder_event(uint8_t event_type, int8_t delta)
{
    /* 0x06 Encoder Event: event_type(1B) + delta(1B)
     * event_type: 0x01=push click, 0x02=push long, 0x03=rotation
     * delta: signed rotation clicks (only meaningful for type 0x03) */
    uint8_t payload[2];
    payload[0] = event_type;
    payload[1] = (uint8_t)delta;
    uint16_t flen = frame_build_screen(s_tx_buf, 0x06, payload, 2);
    bsp_uart_screen_send(s_tx_buf, flen);
}
