/**
 * @file    ntc_sensor.c
 * @brief   NTC temperature calculation using Steinhart-Hart simplified formula.
 *
 * Stage 8 (2026-05-06): 加入 3s 滑动平均滤波 (30 × 100ms 采样窗口).
 *   - 4 通道 + avg 同步滤波, 各自独立环
 *   - 单点 -999 (invalid) 不参与平均, 全部 -999 时输出 -999
 *   - warmup 期间只遍历已填槽位, 避免 0°C 污染
 *   - 必须先调 ntc_filter_init() 显式初始化为 -999
 */

#include "ntc_sensor.h"
#include <math.h>

#define NTC_R_REF       10000.0f    /* Pullup resistor: 10kΩ */
#define NTC_R0          10000.0f    /* NTC nominal: 10kΩ @ 25°C */
#define NTC_T0_K        298.15f     /* 25°C in Kelvin */
#define NTC_B           3950.0f     /* B-value */
#define NTC_ADC_MAX     4095.0f     /* 12-bit ADC */

#define NTC_FILTER_DEPTH 30         /* 30 × 100ms = 3s 滑窗 */

/* 静态环形 buffer — 由 ntc_filter_init() 显式置 -999 */
static int16_t s_filt_ch[NTC_CHANNEL_COUNT][NTC_FILTER_DEPTH];
static int16_t s_filt_avg[NTC_FILTER_DEPTH];
static uint8_t s_filt_idx;
static uint8_t s_filt_warmup;       /* 0~NTC_FILTER_DEPTH, cap 后保持满窗口 */

void ntc_filter_init(void)
{
    for (int i = 0; i < NTC_CHANNEL_COUNT; i++) {
        for (int k = 0; k < NTC_FILTER_DEPTH; k++) {
            s_filt_ch[i][k] = -999;
        }
    }
    for (int k = 0; k < NTC_FILTER_DEPTH; k++) {
        s_filt_avg[k] = -999;
    }
    s_filt_idx = 0;
    s_filt_warmup = 0;
}

int16_t ntc_adc_to_temp_x10(uint16_t adc_value)
{
    if (adc_value < 10 || adc_value > 4085) {
        return -999;    /* Invalid: open(>4085) or shorted(<10).
                         * ADC=4094 with NTC disconnected gives -90°C → false heating.
                         * Widen deadband to catch near-rail readings. */
    }

    /* R_ntc = R_ref * ADC / (4095 - ADC) */
    float r_ntc = NTC_R_REF * (float)adc_value / (NTC_ADC_MAX - (float)adc_value);

    /* T(K) = 1 / (1/T0 + (1/B) * ln(R_ntc/R0)) */
    float t_kelvin = 1.0f / (1.0f / NTC_T0_K + (1.0f / NTC_B) * logf(r_ntc / NTC_R0));

    /* T(°C) = T(K) - 273.15, then x10 for 0.1°C resolution */
    float t_celsius = t_kelvin - 273.15f;

    return (int16_t)(t_celsius * 10.0f);
}

int16_t ntc_calc_average(const uint16_t adc_values[NTC_CHANNEL_COUNT],
                         int16_t temp_out[NTC_CHANNEL_COUNT])
{
    /* === 1. 即时计算 4 通道温度 + 即时 avg, 入环 === */
    int32_t sum_inst = 0;
    int     cnt_inst = 0;
    for (int i = 0; i < NTC_CHANNEL_COUNT; i++) {
        int16_t t_inst = ntc_adc_to_temp_x10(adc_values[i]);
        s_filt_ch[i][s_filt_idx] = t_inst;
        if (t_inst != -999) {
            sum_inst += t_inst;
            cnt_inst++;
        }
    }
    int16_t inst_avg = (cnt_inst > 0) ? (int16_t)(sum_inst / cnt_inst) : -999;
    s_filt_avg[s_filt_idx] = inst_avg;

    /* === 2. 推进环索引 + 预热计数 (cap NTC_FILTER_DEPTH) === */
    s_filt_idx = (uint8_t)((s_filt_idx + 1) % NTC_FILTER_DEPTH);
    if (s_filt_warmup < NTC_FILTER_DEPTH) s_filt_warmup++;

    /* === 3. 输出滤波后 4 通道 → temp_out[] === */
    for (int i = 0; i < NTC_CHANNEL_COUNT; i++) {
        int32_t s = 0;
        int     c = 0;
        for (int k = 0; k < s_filt_warmup; k++) {
            if (s_filt_ch[i][k] != -999) {
                s += s_filt_ch[i][k];
                c++;
            }
        }
        temp_out[i] = (c > 0) ? (int16_t)(s / c) : -999;
    }

    /* === 4. 输出滤波后 avg === */
    int32_t s = 0;
    int     c = 0;
    for (int k = 0; k < s_filt_warmup; k++) {
        if (s_filt_avg[k] != -999) {
            s += s_filt_avg[k];
            c++;
        }
    }
    return (c > 0) ? (int16_t)(s / c) : -999;
}
