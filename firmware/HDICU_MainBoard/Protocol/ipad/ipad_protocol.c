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
#include "flash_storage.h"
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

    /* Bytes 24-33: Reserved (v1.0), already zeroed */

    /* ===== v2.1 扩展字段 (byte 34-45) ===== */
    app_data_lock();
    uint8_t s_cancel_temp  = d->cancel_flags.temp;
    uint8_t s_cancel_humid = d->cancel_flags.humid;
    uint8_t s_cancel_o2    = d->cancel_flags.o2;
    uint8_t s_cancel_co2   = d->cancel_flags.co2;
    int16_t s_cal_temp  = d->calibration.temp;
    int16_t s_cal_humid = d->calibration.humid;
    int16_t s_cal_o2    = d->calibration.o2;
    int16_t s_cal_co2   = d->calibration.co2;
    app_data_unlock();

    payload[34] = s_cancel_temp;
    payload[35] = s_cancel_humid;
    payload[36] = s_cancel_o2;
    payload[37] = s_cancel_co2;
    payload[38] = (uint8_t)(s_cal_temp >> 8);
    payload[39] = (uint8_t)(s_cal_temp & 0xFF);
    payload[40] = (uint8_t)(s_cal_humid >> 8);
    payload[41] = (uint8_t)(s_cal_humid & 0xFF);
    payload[42] = (uint8_t)(s_cal_o2 >> 8);
    payload[43] = (uint8_t)(s_cal_o2 & 0xFF);
    payload[44] = (uint8_t)(s_cal_co2 >> 8);
    payload[45] = (uint8_t)(s_cal_co2 & 0xFF);

    uint16_t frame_len = frame_build_ipad(s_tx_buf, IPAD_RSP_PARAMS, payload, IPAD_PARAMS_RSP_LEN);
    bsp_uart_ipad_send(s_tx_buf, frame_len);
}

/**
 * @brief Parse and apply 0x03 write parameters (30 bytes, v2.1)
 *        v2.1 新增:
 *          byte 18-21: 取消标志 (0=取消不更新, 非0=更新)
 *          byte 22-29: 校准值 (int16 ×4)
 *        OOB规则: 任一启用字段超限 → 拒绝整包. 被取消的字段不参与OOB检查.
 *        校准值: 非零时写入Flash并生效; 零时保持Flash中的现值不变.
 *        动态OOB: target_temp/humid/o2 使用 d->limits 中的上下限作为边界.
 */
static void handle_write_params(const uint8_t *data, uint8_t len)
{
    uint8_t ack[2];

    if (len != IPAD_WRITE_DATA_LEN) {
        ack[0] = IPAD_WRITE_CMD_ERR;
        ack[1] = IPAD_ERR_NONE;
        uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_WRITE_ACK, ack, 2);
        bsp_uart_ipad_send(s_tx_buf, flen);
        return;
    }

    AppData_t *d = app_data_get();

    /* ===== Parse 30 bytes ===== */
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
    uint8_t  cancel_temp  = data[18];    /* v2.1: 0=取消, 非0=更新 */
    uint8_t  cancel_humid = data[19];
    uint8_t  cancel_o2    = data[20];
    uint8_t  cancel_co2   = data[21];
    int16_t  cal_temp  = (int16_t)(((uint16_t)data[22] << 8) | data[23]);
    int16_t  cal_humid = (int16_t)(((uint16_t)data[24] << 8) | data[25]);
    int16_t  cal_o2    = (int16_t)(((uint16_t)data[26] << 8) | data[27]);
    int16_t  cal_co2   = (int16_t)(((uint16_t)data[28] << 8) | data[29]);

    /* ===== Validate (动态OOB + 校准值OOB) ===== */
    uint8_t err_code = IPAD_ERR_NONE;

    /* 动态OOB: 仅对启用的字段检查 (取消=0 则跳过OOB) */
    if (cancel_temp) {
        if (temp < d->limits.temp_lower || temp > d->limits.temp_upper)
            err_code = IPAD_ERR_TEMP_OOB;
    }
    if (!err_code && cancel_humid) {
        if (humid < d->limits.humid_lower || humid > d->limits.humid_upper)
            err_code = IPAD_ERR_HUMID_OOB;
    }
    if (!err_code && cancel_o2) {
        if (o2 < d->limits.o2_lower || o2 > d->limits.o2_upper)
            err_code = IPAD_ERR_O2_OOB;
    }
    if (!err_code && cancel_co2) {
        if (co2 > 5000) err_code = IPAD_ERR_CO2_OOB;
    }
    /* 非取消类字段(定时/开关/灯光)固定范围检查 */
    if (!err_code && fog > d->limits.fog_upper)       err_code = IPAD_ERR_FOG_OOB;
    if (!err_code && disinf > d->limits.uv_upper)     err_code = IPAD_ERR_DISINFECT_OOB;
    if (!err_code && fan > 3)                         err_code = IPAD_ERR_FAN_OOB;
    if (!err_code && (nursing < 1 || nursing > 3))    err_code = IPAD_ERR_NURSING_OOB;
    if (!err_code && cycle > 1)                       err_code = IPAD_ERR_NURSING_OOB;
    if (!err_code && fresh > 1)                       err_code = IPAD_ERR_NURSING_OOB;
    if (!err_code && open_o2 > 1)                     err_code = IPAD_ERR_NURSING_OOB;
    if (!err_code && light > 0x0F)                    err_code = IPAD_ERR_NURSING_OOB;

    /* 校准值范围检查 (±500 for 温湿O2 x10, ±5000 for CO2 ppm) */
    if (!err_code) {
        if (cal_temp  < -500  || cal_temp  > 500)     err_code = IPAD_ERR_CAL_OOB;
        else if (cal_humid < -500 || cal_humid > 500) err_code = IPAD_ERR_CAL_OOB;
        else if (cal_o2  < -500 || cal_o2  > 500)     err_code = IPAD_ERR_CAL_OOB;
        else if (cal_co2 < -5000 || cal_co2 > 5000)   err_code = IPAD_ERR_CAL_OOB;
    }

    if (err_code != IPAD_ERR_NONE) {
        ack[0] = IPAD_WRITE_PARAM_OOB;
        ack[1] = err_code;
    } else {
        /* ===== 应用更新 ===== */
        app_data_lock();

        /* 更新取消标志 (v2.1: session state, 不持久化) */
        d->cancel_flags.temp  = cancel_temp  ? 1 : 0;
        d->cancel_flags.humid = cancel_humid ? 1 : 0;
        d->cancel_flags.o2    = cancel_o2    ? 1 : 0;
        d->cancel_flags.co2   = cancel_co2   ? 1 : 0;

        /* 只有非取消字段才更新 setpoint */
        if (cancel_temp)  d->setpoint.target_temp     = temp;
        if (cancel_humid) d->setpoint.target_humidity = humid;
        if (cancel_o2)    d->setpoint.target_o2       = o2;
        if (cancel_co2)   d->setpoint.target_co2      = co2;

        /* 其他字段(定时/开关/灯光)无取消概念, 始终更新 */
        d->setpoint.fog_time       = fog;
        d->setpoint.disinfect_time = disinf;
        d->setpoint.fan_speed      = fan;
        d->setpoint.nursing_level  = nursing;
        d->setpoint.inner_cycle    = cycle;
        d->setpoint.fresh_air      = fresh;
        d->setpoint.open_o2        = open_o2;
        d->setpoint.light_ctrl     = light;

        /* 校准值更新 + Flash持久化 (仅当有值变化时写Flash以节约寿命) */
        bool cal_changed = (d->calibration.temp  != cal_temp) ||
                           (d->calibration.humid != cal_humid) ||
                           (d->calibration.o2    != cal_o2) ||
                           (d->calibration.co2   != cal_co2);
        if (cal_changed) {
            d->calibration.temp  = cal_temp;
            d->calibration.humid = cal_humid;
            d->calibration.o2    = cal_o2;
            d->calibration.co2   = cal_co2;
        }

        /* 启停定时器 (互锁内部检查) */
        control_timers_start_fog(d, fog);
        control_timers_start_disinfect(d, disinf);

        app_data_unlock();

        /* 写Flash (锁外, 避免长时间持锁). 失败不影响内存状态. */
        if (cal_changed) {
            extern bool flash_storage_save_config(const CalibrationData_t *, const FactoryLimits_t *);
            flash_storage_save_config(&d->calibration, &d->limits);
        }

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
 * @brief Handle 0x09 — 写出厂限值 (18 bytes)
 *
 * 限值校验规则:
 *   1. 温/湿/O2: 下限 < 上限, 且在硬件物理范围内
 *   2. UV/红外/雾化: 只有上限, 0~3600秒
 *   3. 任何字段越界 → 整包拒绝, 返回 0x04 err=0x0A
 */
static void handle_write_limits(const uint8_t *data, uint8_t len)
{
    uint8_t ack[2];

    if (len != IPAD_LIMITS_DATA_LEN) {
        ack[0] = IPAD_WRITE_CMD_ERR;
        ack[1] = IPAD_ERR_NONE;
        uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_WRITE_ACK, ack, 2);
        bsp_uart_ipad_send(s_tx_buf, flen);
        return;
    }

    /* Parse 18 bytes */
    FactoryLimits_t new_limits;
    new_limits.temp_upper     = ((uint16_t)data[0]  << 8) | data[1];
    new_limits.temp_lower     = ((uint16_t)data[2]  << 8) | data[3];
    new_limits.humid_upper    = ((uint16_t)data[4]  << 8) | data[5];
    new_limits.humid_lower    = ((uint16_t)data[6]  << 8) | data[7];
    new_limits.o2_upper       = ((uint16_t)data[8]  << 8) | data[9];
    new_limits.o2_lower       = ((uint16_t)data[10] << 8) | data[11];
    new_limits.uv_upper       = ((uint16_t)data[12] << 8) | data[13];
    new_limits.infrared_upper = ((uint16_t)data[14] << 8) | data[15];
    new_limits.fog_upper      = ((uint16_t)data[16] << 8) | data[17];

    /* Validate: upper > lower, and within hardware ranges */
    uint8_t err_code = IPAD_ERR_NONE;

    /* 温度 [10.0, 40.0]°C 物理范围 ×10 = [100, 400] */
    if (new_limits.temp_lower >= new_limits.temp_upper ||
        new_limits.temp_lower < 100 || new_limits.temp_upper > 400) {
        err_code = IPAD_ERR_LIMITS_INVALID;
    }
    /* 湿度 [30, 90]% ×10 = [300, 900] */
    else if (new_limits.humid_lower >= new_limits.humid_upper ||
             new_limits.humid_lower < 300 || new_limits.humid_upper > 900) {
        err_code = IPAD_ERR_LIMITS_INVALID;
    }
    /* O2 [21, 100]% ×10 = [210, 1000] */
    else if (new_limits.o2_lower >= new_limits.o2_upper ||
             new_limits.o2_lower < 210 || new_limits.o2_upper > 1000) {
        err_code = IPAD_ERR_LIMITS_INVALID;
    }
    /* 时间类 [0, 3600] 秒 (仅上限) */
    else if (new_limits.uv_upper > 3600 ||
             new_limits.infrared_upper > 3600 ||
             new_limits.fog_upper > 3600) {
        err_code = IPAD_ERR_LIMITS_INVALID;
    }

    if (err_code != IPAD_ERR_NONE) {
        ack[0] = IPAD_WRITE_PARAM_OOB;
        ack[1] = err_code;
    } else {
        /* Apply + persist */
        AppData_t *d = app_data_get();
        app_data_lock();
        d->limits = new_limits;
        app_data_unlock();

        /* Save to Flash (config region) */
        flash_storage_save_config(&d->calibration, &d->limits);

        ack[0] = IPAD_WRITE_OK;
        ack[1] = IPAD_ERR_NONE;
    }

    uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_WRITE_ACK, ack, 2);
    bsp_uart_ipad_send(s_tx_buf, flen);
}

/**
 * @brief Handle 0x0A — 读出厂限值
 * 返回格式同 0x09 (18 bytes)
 */
static void handle_read_limits(void)
{
    AppData_t *d = app_data_get();
    uint8_t payload[IPAD_LIMITS_DATA_LEN];

    app_data_lock();
    FactoryLimits_t lim = d->limits;
    app_data_unlock();

    payload[0]  = (uint8_t)(lim.temp_upper >> 8);
    payload[1]  = (uint8_t)(lim.temp_upper & 0xFF);
    payload[2]  = (uint8_t)(lim.temp_lower >> 8);
    payload[3]  = (uint8_t)(lim.temp_lower & 0xFF);
    payload[4]  = (uint8_t)(lim.humid_upper >> 8);
    payload[5]  = (uint8_t)(lim.humid_upper & 0xFF);
    payload[6]  = (uint8_t)(lim.humid_lower >> 8);
    payload[7]  = (uint8_t)(lim.humid_lower & 0xFF);
    payload[8]  = (uint8_t)(lim.o2_upper >> 8);
    payload[9]  = (uint8_t)(lim.o2_upper & 0xFF);
    payload[10] = (uint8_t)(lim.o2_lower >> 8);
    payload[11] = (uint8_t)(lim.o2_lower & 0xFF);
    payload[12] = (uint8_t)(lim.uv_upper >> 8);
    payload[13] = (uint8_t)(lim.uv_upper & 0xFF);
    payload[14] = (uint8_t)(lim.infrared_upper >> 8);
    payload[15] = (uint8_t)(lim.infrared_upper & 0xFF);
    payload[16] = (uint8_t)(lim.fog_upper >> 8);
    payload[17] = (uint8_t)(lim.fog_upper & 0xFF);

    uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_LIMITS, payload, IPAD_LIMITS_DATA_LEN);
    bsp_uart_ipad_send(s_tx_buf, flen);
}

/**
 * @brief Handle 0x0B — 恢复出厂默认
 * 1. 擦除 Flash 配置页
 * 2. 重新初始化 AppData (校准=0, 限值=硬编码默认, 取消标志=全启用)
 * 3. 恢复 setpoint 默认值 (25°C/50%/21%/1000ppm)
 * 4. 保持 runtime_min 不变
 * 5. 返回 0x04 ACK
 */
static void handle_factory_reset(void)
{
    AppData_t *d = app_data_get();

    app_data_lock();

    /* 保存运行时间 (不恢复) */
    uint32_t preserved_runtime = d->system.total_runtime_min;

    /* 校准值清零 */
    d->calibration.temp  = 0;
    d->calibration.humid = 0;
    d->calibration.o2    = 0;
    d->calibration.co2   = 0;

    /* 限值恢复硬编码默认 */
    d->limits.temp_upper     = 400;
    d->limits.temp_lower     = 100;
    d->limits.humid_upper    = 900;
    d->limits.humid_lower    = 300;
    d->limits.o2_upper       = 1000;
    d->limits.o2_lower       = 210;
    d->limits.uv_upper       = 3600;
    d->limits.infrared_upper = 3600;
    d->limits.fog_upper      = 3600;

    /* 取消标志全启用 */
    d->cancel_flags.temp  = 1;
    d->cancel_flags.humid = 1;
    d->cancel_flags.o2    = 1;
    d->cancel_flags.co2   = 1;

    /* Setpoint 恢复默认 */
    d->setpoint.target_temp     = 250;
    d->setpoint.target_humidity = 500;
    d->setpoint.target_o2       = 210;
    d->setpoint.target_co2      = 1000;
    d->setpoint.fog_time        = 0;
    d->setpoint.disinfect_time  = 0;
    d->setpoint.fan_speed       = 0;
    d->setpoint.nursing_level   = 1;
    d->setpoint.inner_cycle     = 0;
    d->setpoint.fresh_air       = 0;
    d->setpoint.open_o2         = 0;
    d->setpoint.light_ctrl      = 0;

    /* Runtime 保持不变 */
    d->system.total_runtime_min = preserved_runtime;

    app_data_unlock();

    /* 安全: 强制停止所有运行中的定时器, 关闭对应继电器 (医疗设备不能残留输出) */
    control_timers_start_fog(d, 0);
    control_timers_start_disinfect(d, 0);

    /* 擦除Flash配置页 */
    flash_storage_erase_config();

    /* ACK */
    uint8_t ack[2] = {IPAD_WRITE_OK, IPAD_ERR_NONE};
    uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_WRITE_ACK, ack, 2);
    bsp_uart_ipad_send(s_tx_buf, flen);
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

    case IPAD_CMD_WRITE_LIMITS:
        /* 0x09 (v2.1) → 写出厂限值 */
        handle_write_limits(data, len);
        break;

    case IPAD_CMD_READ_LIMITS:
        /* 0x0A (v2.1) → 读出厂限值 */
        handle_read_limits();
        break;

    case IPAD_CMD_FACTORY_RESET:
        /* 0x0B (v2.1) → 恢复出厂默认 */
        handle_factory_reset();
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
