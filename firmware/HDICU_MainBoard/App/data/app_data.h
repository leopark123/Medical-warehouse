/**
 * @file    app_data.h
 * @brief   Central data hub — all shared runtime state for HDICU-ZKB01A
 * @note    This is the single source of truth at runtime.
 *          Main controller is the sole authority (屏幕板 only displays).
 *
 *          Parameter encoding:
 *          - Temperature: x10 (256 = 25.6°C), range 100~400
 *          - Humidity: x10 (800 = 80.0%), range 300~900
 *          - O2 concentration: x10 (210 = 21.0%), range 210~1000
 *          - CO2: raw ppm, range 0~5000
 *          - Nursing level: 1~3 (NOT 1~4)
 */

#ifndef APP_DATA_H
#define APP_DATA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*  Sensor Data (read by SensorTask, consumed by everyone)                   */
/* ========================================================================= */
typedef struct {
    int16_t  temperature[4];    /* x10, 0.1°C resolution, from NTC ADC */
    int16_t  temperature_avg;   /* x10, average of valid channels */
    uint8_t  humidity;          /* %, from O2 sensor (integer for screen) */
    uint16_t humidity_raw;      /* x10, for iPad protocol */
    uint8_t  o2_percent;        /* %, from O2 sensor (integer for screen) */
    uint16_t o2_raw;            /* x10, for iPad protocol */
    uint16_t co2_ppm;           /* raw ppm, from CO2 sensor */
    uint8_t  heart_rate;        /* bpm, from JFC103. 0 = invalid */
    uint8_t  spo2;              /* %, from JFC103. 0 = invalid */
    bool     co2_valid;         /* true if CO2 sensor responding */
    bool     o2_valid;          /* true if O2 sensor responding */
    bool     jfc103_valid;      /* true if JFC103 responding */
    uint8_t  liquid_level;      /* 1=normal, 0=low (PB14, active low with pullup) */
    uint8_t  urine_detect;      /* 1=normal, 0=detected (PB15, active low with pullup) */
} SensorData_t;

/* ========================================================================= */
/*  Setpoints (written by iPad/Screen commands, read by ControlTask)         */
/* ========================================================================= */
typedef struct {
    uint16_t target_temp;       /* x10, 100~400 (10.0~40.0°C) */
    uint16_t target_humidity;   /* x10, 300~900 (30.0~90.0%) */
    uint16_t target_o2;         /* x10, 210~1000 (21.0~100.0%) */
    uint16_t target_co2;        /* ppm, 0~5000 */
    uint16_t fog_time;          /* seconds, 0~3600. 0=off */
    uint16_t disinfect_time;    /* seconds, 0~3600. 0=off */
    uint8_t  fan_speed;         /* 0=off, 1=low, 2=mid, 3=high */
    uint8_t  nursing_level;     /* 1~3 (frozen, NOT 1~4) */
    uint8_t  inner_cycle;       /* 0=off, 1=on */
    uint8_t  fresh_air;         /* 0=off, 1=on */
    uint8_t  open_o2;           /* 0=off, 1=on */
    uint8_t  light_ctrl;        /* bit0=检查, bit1=照明, bit2=蓝, bit3=红 */
} Setpoints_t;

/* ========================================================================= */
/*  Control State (written by ControlTask, read for display/protocol)        */
/* ========================================================================= */
typedef enum {
    TEMP_STATE_IDLE = 0,
    TEMP_STATE_COOLING,
    TEMP_STATE_HEATING,
} TempState_t;

typedef enum {
    HUMID_STATE_IDLE = 0,
    HUMID_STATE_HUMIDIFY,
    HUMID_STATE_DEHUMIDIFY,
} HumidState_t;

typedef enum {
    O2_STATE_IDLE = 0,
    O2_STATE_SUPPLYING,
    O2_STATE_OPEN_MODE,     /* Open O2 mode with interlock rules */
} O2State_t;

typedef struct {
    TempState_t  temp_state;
    HumidState_t humid_state;
    O2State_t    o2_state;

    /* Timer countdowns (managed by main controller, screen only displays) */
    uint16_t fog_remaining;     /* seconds, 0 = not running / finished */
    uint16_t disinfect_remaining;/* seconds */
    uint16_t o2_accumulated;    /* seconds, cumulative open-O2 time */

    /* Relay bitmap: bit N = relay N on (see BSP_RELAY_xxx) */
    uint16_t relay_status;

    /* Light status: bit0=检查, bit1=照明, bit2=蓝, bit3=红 */
    uint8_t  light_status;

    /* Switch status (actual applied state, may differ from setpoint due to interlocks):
     * bit0 = 内循环 active (1=inner cycle on, 0=off/outer cycle)
     * bit1 = 新风净化 active
     * bit2 = 开放式供氧 active */
    #define SW_BIT_INNER_CYCLE  0x01
    #define SW_BIT_FRESH_AIR    0x02
    #define SW_BIT_OPEN_O2      0x04
    uint8_t  switch_status;

    /* Timer expiry beep request: bit0=fog done, bit1=disinfect done.
     * Set by control_timers when countdown reaches 0.
     * Cleared by ControlTask after beep duration (3s). */
    uint8_t  timer_beep_request;
    uint8_t  timer_beep_counter;    /* ControlTask counts down 200ms ticks for beep duration */

    /* Fan speed applied (may differ from setpoint due to interlocks) */
    uint8_t  fan_speed_actual;

    /* Nursing level applied */
    uint8_t  nursing_level_actual;
} ControlState_t;

/* ========================================================================= */
/*  Alarm State (written by AlarmTask)                                       */
/* ========================================================================= */
#define ALARM_TEMP_HIGH         (1U << 0)
#define ALARM_TEMP_LOW          (1U << 1)
#define ALARM_HUMID             (1U << 2)
#define ALARM_O2_CO2            (1U << 3)
#define ALARM_HEART_RATE        (1U << 4)
#define ALARM_SPO2_LOW          (1U << 5)
#define ALARM_COMM_FAULT        (1U << 6)

typedef struct {
    uint16_t alarm_flags;       /* bitmask of ALARM_xxx */
    bool     buzzer_active;
    bool     acknowledged;      /* true after user presses alarm confirm */
} AlarmState_t;

/* ========================================================================= */
/*  System State (written by SystemTask)                                     */
/* ========================================================================= */
typedef struct {
    uint32_t total_runtime_min;     /* cumulative minutes, survives power cycle */
    uint32_t boot_uptime_sec;       /* seconds since this boot */
} SystemState_t;

/* ========================================================================= */
/*  Global Application Data (singleton)                                      */
/* ========================================================================= */
typedef struct {
    SensorData_t    sensor;
    Setpoints_t     setpoint;
    ControlState_t  control;
    AlarmState_t    alarm;
    SystemState_t   system;
} AppData_t;

/* Global instance access */
AppData_t* app_data_get(void);

/* Initialize all fields to safe defaults (per frozen spec 4.5) */
void app_data_init(void);

/* Concurrency protection for shared data.
 * Call lock/unlock around multi-field read/write sequences.
 * Implementation: FreeRTOS critical section (disables interrupts briefly).
 * Single-field atomic reads (uint8/uint16) do not require locking on Cortex-M3. */
void app_data_lock(void);
void app_data_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_H */
