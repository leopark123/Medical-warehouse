/**
 * @file    tasks.c
 * @brief   FreeRTOS task implementations — 6 tasks per frozen spec
 *
 *          Data flow:
 *          SensorTask → app_data.sensor (writes)
 *          ControlTask → reads sensor + setpoint, writes control + relay
 *          AlarmTask → reads sensor + setpoint + control, writes alarm
 *          CommScreenTask → reads all, sends display data; receives commands
 *          CommIPadTask → reads all, responds to queries; receives write cmds
 *          SystemTask → runtime counting, flash save, watchdog
 */

#include "task_defs.h"
#include "bsp_config.h"
#include "app_data.h"

/* Sensor drivers */
#include "co2_sensor.h"
#include "o2_sensor.h"
#include "ntc_sensor.h"
#include "jfc103_sensor.h"

/* Control modules */
#include "temp_control.h"
#include "humidity_control.h"
#include "oxygen_control.h"
#include "control_timers.h"
#include "interlock.h"

/* Protocol handlers */
#include "ipad_protocol.h"
#include "screen_protocol.h"
#include "uart_driver.h"         /* g_uart_rx_ok[], uart_driver_recover_screen() */

/* Flash storage */
#include "flash_storage.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ADC and PWM/relay drivers */
#include "adc_driver.h"
#include "pwm_driver.h"
#include "relay_driver.h"

/* ========================================================================= */
/*  SensorTask — High priority, 100ms                                        */
/*  Reads: ADC (NTC), UART3 (CO2), UART4 (O2), UART5 (JFC103)              */
/*  Writes: app_data.sensor                                                  */
/* ========================================================================= */
static void SensorTask(void *arg)
{
    (void)arg;
    AppData_t *d = app_data_get();

    /* Start JFC103 acquisition */
    jfc103_sensor_start();

    for (;;) {
        /* --- NTC Temperature (ADC) --- */
        uint16_t adc_vals[NTC_CHANNEL_COUNT];
        adc_driver_read_all(adc_vals);
        d->sensor.temperature_avg = ntc_calc_average(adc_vals, d->sensor.temperature);

        /* --- CO2 (UART3, data arrives via ISR → co2_sensor_rx_byte) --- */
        d->sensor.co2_ppm = co2_sensor_get_ppm();
        d->sensor.co2_valid = co2_sensor_is_valid();

        /* --- O2 (UART4, data arrives via ISR → o2_sensor_rx_byte) --- */
        O2SensorData_t o2 = o2_sensor_get_data();
        d->sensor.o2_raw = o2.o2_raw;
        d->sensor.o2_percent = (uint8_t)((o2.o2_raw + 5) / 10);   /* Round to integer for screen */
        d->sensor.humidity_raw = o2.humidity_raw;
        d->sensor.humidity = (uint8_t)((o2.humidity_raw + 5) / 10);
        d->sensor.o2_valid = o2_sensor_is_valid();

        /* --- JFC103 (UART5, data arrives via ISR) --- */
        jfc103_sensor_tick();   /* Adaptive 0x8A management */
        d->sensor.heart_rate = jfc103_get_heart_rate();
        d->sensor.spo2 = jfc103_get_spo2();
        d->sensor.jfc103_valid = jfc103_is_valid();

        /* --- Liquid level (PB14) + Urine detect (PB15) --- */
        d->sensor.liquid_level = (uint8_t)HAL_GPIO_ReadPin(BSP_LIQUID_DETECT_PORT, BSP_LIQUID_DETECT_PIN);
        d->sensor.urine_detect = (uint8_t)HAL_GPIO_ReadPin(BSP_URINE_DETECT_PORT, BSP_URINE_DETECT_PIN);

        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_SENSOR_MS));
    }
}

/* ========================================================================= */
/*  ControlTask — High priority, 200ms                                       */
/*  Reads: sensor + setpoint                                                 */
/*  Writes: control state, relay bitmap                                      */
/* ========================================================================= */
static void ControlTask(void *arg)
{
    (void)arg;
    AppData_t *d = app_data_get();
    uint32_t timer_counter = 0;

    for (;;) {
        /* Run state machines */
        temp_control_update(d);
        humidity_control_update(d);
        oxygen_control_update(d);

        /* Apply safety interlocks (LAST step before output) */
        interlock_apply(d);

        /* Update nursing level and fan speed */
        d->control.nursing_level_actual = d->setpoint.nursing_level;
        d->control.fan_speed_actual = d->setpoint.fan_speed;
        d->control.light_status = d->setpoint.light_ctrl;

        /* 1-second sub-tick for timers (200ms * 5 = 1s) */
        timer_counter++;
        if (timer_counter >= 5) {
            timer_counter = 0;
            control_timers_tick_1s(d);
        }

        /* Apply nursing level LEDs (PB1=level1, PB0=level2, PC5=level3) */
        HAL_GPIO_WritePin(BSP_LED_HULI1_PORT, BSP_LED_HULI1_PIN,
                          (d->control.nursing_level_actual == 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BSP_LED_HULI2_PORT, BSP_LED_HULI2_PIN,
                          (d->control.nursing_level_actual == 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BSP_LED_HULI3_PORT, BSP_LED_HULI3_PIN,
                          (d->control.nursing_level_actual == 3) ? GPIO_PIN_SET : GPIO_PIN_RESET);

        /* Timer expiry beep: countdown 200ms ticks for 3s beep duration.
         * Actual PB3 driving is done by AlarmTask (single owner of buzzer GPIO).
         * AlarmTask checks both buzzer_active and timer_beep_counter. */
        if (d->control.timer_beep_counter > 0) {
            d->control.timer_beep_counter--;
            if (d->control.timer_beep_counter == 0) {
                d->control.timer_beep_request = 0;
            }
        }

        /* Apply physical outputs */
        relay_driver_apply(d->control.relay_status);
        pwm_set_fan_speed(d->control.fan_speed_actual);

        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_CONTROL_MS));
    }
}

/* ========================================================================= */
/*  AlarmTask — High priority, 100ms                                         */
/*  Reads: sensor + setpoint + control                                       */
/*  Writes: alarm state                                                      */
/* ========================================================================= */

/* Alarm thresholds from frozen spec 4.3 */
#include "alarm_config.h"

static void AlarmTask(void *arg)
{
    (void)arg;
    AppData_t *d = app_data_get();

    /* Delay counters (100ms ticks) */
    uint16_t temp_high_cnt = 0, temp_low_cnt = 0;
    uint16_t humid_cnt = 0, o2_cnt = 0, co2_cnt = 0;
    uint16_t hr_cnt = 0, spo2_cnt = 0;

    /* Startup grace period: skip COMM_FAULT for first 5s to let screen board boot */
    uint32_t boot_tick = HAL_GetTick();
    uint8_t grace = 1;

    /* Buzzer intermittent blink counter (100ms ticks, period=10 → 1s cycle) */
    static uint8_t buzzer_blink_cnt = 0;

    for (;;) {
        /* End grace period after 5s */
        if (grace && (HAL_GetTick() - boot_tick > 5000)) {
            grace = 0;
        }
        uint16_t flags = 0;

        /* Temperature: setpoint ± 5°C, 10s delay (skip if NTC invalid) */
        int16_t t_set = (int16_t)d->setpoint.target_temp;
        if (d->sensor.temperature_avg != -999 &&
            d->sensor.temperature_avg > t_set + ALARM_TEMP_OFFSET_X10) {
            if (++temp_high_cnt >= ALARM_DELAY_SLOW_TICKS) flags |= ALARM_TEMP_HIGH;
        } else { temp_high_cnt = 0; }

        if (d->sensor.temperature_avg != -999 &&
            d->sensor.temperature_avg < t_set - ALARM_TEMP_OFFSET_X10) {
            if (++temp_low_cnt >= ALARM_DELAY_SLOW_TICKS) flags |= ALARM_TEMP_LOW;
        } else { temp_low_cnt = 0; }

        /* Humidity: setpoint ± 5%, 10s delay (skip if O2 sensor offline) */
        if (d->sensor.o2_valid) {
            int16_t h_set = (int16_t)d->setpoint.target_humidity;
            if (d->sensor.humidity_raw > (uint16_t)(h_set + ALARM_HUMID_OFFSET_X10)) {
                if (++humid_cnt >= ALARM_DELAY_SLOW_TICKS) flags |= ALARM_HUMID;
            } else if (d->sensor.humidity_raw < (uint16_t)(h_set - ALARM_HUMID_OFFSET_X10)) {
                if (++humid_cnt >= ALARM_DELAY_SLOW_TICKS) flags |= ALARM_HUMID;
            } else { humid_cnt = 0; }
        } else { humid_cnt = 0; }

        /* O2: setpoint ± 5%, 10s delay (skip if O2 sensor offline) */
        if (d->sensor.o2_valid) {
            int16_t o2_set = (int16_t)d->setpoint.target_o2;
            if (d->sensor.o2_raw > (uint16_t)(o2_set + ALARM_O2_OFFSET_X10) ||
                d->sensor.o2_raw < (uint16_t)(o2_set - ALARM_O2_OFFSET_X10)) {
                if (++o2_cnt >= ALARM_DELAY_SLOW_TICKS) flags |= ALARM_O2_CO2;
            } else { o2_cnt = 0; }
        } else { o2_cnt = 0; }

        /* CO2: >5000ppm, 10s delay (skip if CO2 sensor offline) */
        if (d->sensor.co2_valid && d->sensor.co2_ppm > ALARM_CO2_HIGH_PPM) {
            if (++co2_cnt >= ALARM_DELAY_SLOW_TICKS) flags |= ALARM_O2_CO2;
        } else { co2_cnt = 0; }

        /* Heart rate: 50~140 bpm, 3s delay (skip if JFC103 offline) */
        if (d->sensor.jfc103_valid && d->sensor.heart_rate > 0) {
            if (d->sensor.heart_rate < ALARM_HR_LOW || d->sensor.heart_rate > ALARM_HR_HIGH) {
                if (++hr_cnt >= ALARM_DELAY_FAST_TICKS) flags |= ALARM_HEART_RATE;
            } else { hr_cnt = 0; }
        } else { hr_cnt = 0; }

        /* SpO2: <85%, 3s delay (skip if JFC103 offline) */
        if (d->sensor.jfc103_valid && d->sensor.spo2 > 0 && d->sensor.spo2 < ALARM_SPO2_LOW_THRESHOLD) {
            if (++spo2_cnt >= ALARM_DELAY_FAST_TICKS) flags |= ALARM_SPO2_LOW;
        } else { spo2_cnt = 0; }

        /* Alarm clear condition (frozen spec 4.3):
         * Alarm clears ONLY when BOTH conditions are met:
         *   1. Parameter returns to normal range (flags bit cleared)
         *   2. User has pressed alarm confirm key (acknowledged = true)
         *
         * Implementation:
         * - alarm_flags tracks which conditions are currently in alarm
         * - active_alarms is the "latched" alarm state shown to user
         * - An alarm is latched when first detected, stays until both conditions met */

        /* === Comm fault alarm: separate path from sensor alarms === */
        /* ALARM_COMM_FAULT is set externally by screen_protocol_tick().
         * It can only clear when screen is BACK ONLINE + user acknowledged.
         * Skip during startup grace period to let screen board boot. */
        if (!grace) {
            if (screen_protocol_is_connected()) {
                /* Screen is online — comm fault condition resolved */
                /* But don't clear the flag yet; need ack too */
            } else {
                /* Screen offline — latch comm fault */
                flags |= ALARM_COMM_FAULT;
            }
        }

        /* Latch all new alarms (sensor + comm) into active set */
        d->alarm.alarm_flags |= flags;

        /* Clear alarms only when BOTH conditions met:
         *   1. The triggering condition has returned to normal (bit NOT in flags)
         *   2. User has pressed alarm confirm key (acknowledged = true)
         */
        if (d->alarm.acknowledged) {
            /* 'flags' contains all currently-active conditions.
             * Any bit in alarm_flags that is NOT in flags = condition resolved.
             * Those resolved bits can now be cleared (since user also ack'd). */
            uint16_t resolved = d->alarm.alarm_flags & ~flags;
            d->alarm.alarm_flags &= ~resolved;

            if (d->alarm.alarm_flags == 0) {
                d->alarm.acknowledged = false;
            }
        }

        /* Buzzer: active if any alarm is latched and not yet acknowledged */
        d->alarm.buzzer_active = (d->alarm.alarm_flags != 0) && !d->alarm.acknowledged;

        /* Drive buzzer GPIO — intermittent for alarm, continuous for timer beep */
        {
            uint8_t buzzer_on = 0;
            if (d->control.timer_beep_counter > 0) {
                buzzer_on = 1;  /* Timer expiry: continuous beep */
            } else if (d->alarm.buzzer_active) {
                /* Alarm: 500ms on / 500ms off (10 ticks × 100ms = 1s cycle) */
                buzzer_blink_cnt++;
                if (buzzer_blink_cnt >= 10) buzzer_blink_cnt = 0;
                buzzer_on = (buzzer_blink_cnt < 5) ? 1 : 0;
            } else {
                buzzer_blink_cnt = 0;
            }
            HAL_GPIO_WritePin(BSP_BUZZER_PORT, BSP_BUZZER_PIN,
                              buzzer_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }

        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_ALARM_MS));
    }
}

/* ========================================================================= */
/*  CommScreenTask — Medium priority, 100ms                                  */
/*  Sends display data every 100ms, heartbeat every 1s                       */
/* ========================================================================= */
static void CommScreenTask(void *arg)
{
    (void)arg;
    extern QueueHandle_t g_screen_rx_queue;
    uint32_t heartbeat_counter = 0;
    uint32_t last_frame_tick_snapshot = 0;
    uint32_t last_wd_check_tick = 0;
    uint8_t screen_ever_connected = 0;  /* C3 fix: only enable watchdog after first connection */

    for (;;) {
        /* B2 fix: check if ISR flagged a deferred recovery */
        if (g_uart_screen_recover_pending) {
            uart_driver_recover_screen();
        }

        /* Drain RX queue — process bytes received from ISR in task context */
        uint8_t rx_byte;
        while (xQueueReceive(g_screen_rx_queue, &rx_byte, 0) == pdTRUE) {
            screen_protocol_rx_byte(rx_byte);
        }

        /* Only send display data when screen is connected (C5 fix: reduce TX load) */
        if (screen_protocol_is_connected()) {
            screen_send_display_data();
            screen_ever_connected = 1;
        }

        /* Heartbeat every 1s (100ms * 10) — always send so screen can detect us */
        heartbeat_counter++;
        if (heartbeat_counter >= 10) {
            heartbeat_counter = 0;
            screen_send_heartbeat();
        }

        /* Check for disconnect */
        screen_protocol_tick(HAL_GetTick());

        /* Protocol-level watchdog: if no valid FRAME for 10s, force-recover USART1.
         * P1-1 fix: uses frame timestamp instead of raw byte count, so noisy-but-
         * no-valid-frame scenarios also trigger recovery.
         * Only active after screen has connected at least once (C3 fix). */
        if (screen_ever_connected) {
            uint32_t now = HAL_GetTick();
            if (now - last_wd_check_tick >= 10000) {
                last_wd_check_tick = now;
                uint32_t cur_frame_tick = screen_protocol_last_frame_tick();
                if (cur_frame_tick == last_frame_tick_snapshot) {
                    /* No new valid frame in 10s — force recover */
                    uart_driver_recover_screen();
                }
                last_frame_tick_snapshot = cur_frame_tick;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_COMM_SCREEN_MS));
    }
}

/* ========================================================================= */
/*  CommIPadTask — Medium priority, event-driven                             */
/*  Receives UART2 data via ISR, processes commands, sends responses          */
/* ========================================================================= */
static void CommIPadTask(void *arg)
{
    (void)arg;
    extern QueueHandle_t g_ipad_rx_queue;

    for (;;) {
        /* Drain RX queue — process iPad bytes in task context (NOT ISR).
         * This is critical because handle_write_params() writes to shared AppData. */
        uint8_t rx_byte;
        while (xQueueReceive(g_ipad_rx_queue, &rx_byte, 0) == pdTRUE) {
            ipad_protocol_rx_byte(rx_byte);
        }

        /* Disconnect detection */
        ipad_protocol_tick(HAL_GetTick());

        vTaskDelay(pdMS_TO_TICKS(10));  /* Fast poll: 10ms for <100ms response */
    }
}

/* ========================================================================= */
/*  SystemTask — Low priority, 1000ms                                        */
/*  Runtime counting, flash save, watchdog                                   */
/* ========================================================================= */
/* Debug output — only compiled when HDICU_DEBUG is defined.
 * Sends status via UART4 (CN16/O2 port). DO NOT enable in production. */
#ifdef HDICU_DEBUG
static uint16_t debug_itoa(int32_t val, char *buf)
{
    uint16_t len = 0;
    if (val < 0) { buf[len++] = '-'; val = -val; }
    if (val == 0) { buf[len++] = '0'; return len; }
    char tmp[12];
    uint16_t tlen = 0;
    while (val > 0) { tmp[tlen++] = '0' + (val % 10); val /= 10; }
    while (tlen > 0) { buf[len++] = tmp[--tlen]; }
    return len;
}

static void debug_send_status(AppData_t *d)
{
    /* Build a human-readable status line via UART4 (CN16)
     * Format: "[HDICU] uptime=XXs temp=XX.X humid=XX o2=XX co2=XXXX hr=XX spo2=XX\r\n" */
    char buf[128];
    uint16_t pos = 0;
    const char *hdr = "[HDICU] up=";
    for (uint16_t i = 0; hdr[i]; i++) buf[pos++] = hdr[i];
    pos += debug_itoa(d->system.boot_uptime_sec, &buf[pos]);
    buf[pos++] = 's'; buf[pos++] = ' ';

    const char *t = "T=";
    for (uint16_t i = 0; t[i]; i++) buf[pos++] = t[i];
    pos += debug_itoa(d->sensor.temperature_avg / 10, &buf[pos]);
    buf[pos++] = '.';
    int16_t frac = d->sensor.temperature_avg % 10;
    if (frac < 0) frac = -frac;
    buf[pos++] = '0' + frac;
    buf[pos++] = ' ';

    const char *h = "H=";
    for (uint16_t i = 0; h[i]; i++) buf[pos++] = h[i];
    pos += debug_itoa(d->sensor.humidity, &buf[pos]);
    buf[pos++] = ' ';

    const char *o = "O2=";
    for (uint16_t i = 0; o[i]; i++) buf[pos++] = o[i];
    pos += debug_itoa(d->sensor.o2_percent, &buf[pos]);
    buf[pos++] = ' ';

    const char *c = "CO2=";
    for (uint16_t i = 0; c[i]; i++) buf[pos++] = c[i];
    pos += debug_itoa(d->sensor.co2_ppm, &buf[pos]);
    buf[pos++] = ' ';

    const char *hr = "HR=";
    for (uint16_t i = 0; hr[i]; i++) buf[pos++] = hr[i];
    pos += debug_itoa(d->sensor.heart_rate, &buf[pos]);
    buf[pos++] = ' ';

    const char *sp = "SpO2=";
    for (uint16_t i = 0; sp[i]; i++) buf[pos++] = sp[i];
    pos += debug_itoa(d->sensor.spo2, &buf[pos]);

    buf[pos++] = '\r'; buf[pos++] = '\n';

    /* Debug output via UART4 (PC10 = CN16), avoid polluting UART1 screen protocol */
    extern void uart_driver_send(uint8_t ch, const uint8_t *data, uint16_t len);
    uart_driver_send(3, (const uint8_t *)buf, pos);  /* 3 = UART_CH_O2 = UART4 */
}
#endif /* HDICU_DEBUG */

static void SystemTask(void *arg)
{
    (void)arg;
    AppData_t *d = app_data_get();
    uint32_t save_counter = 0;

    /* Enable UART RX interrupts now that scheduler is running.
     * Must be done here, not in app_init(), to avoid calling
     * xQueueSendFromISR before scheduler is ready. */
    extern void uart_driver_start_rx(void);
    uart_driver_start_rx();

    for (;;) {
        /* Feed watchdog — IWDG ~4s timeout, SystemTask runs every 1s */
        IWDG->KR = 0xAAAA;     /* Reload IWDG counter */

        /* Uptime */
        d->system.boot_uptime_sec++;

        /* Runtime (in minutes) — increment every 60 seconds */
        if (d->system.boot_uptime_sec % 60 == 0) {
            d->system.total_runtime_min++;
        }

        /* Flash save every 10 minutes (600 seconds) */
        save_counter++;
        if (save_counter >= 600) {
            save_counter = 0;
            flash_storage_save(d->system.total_runtime_min);
        }

#ifdef HDICU_DEBUG
        /* Debug: output status via UART4 (CN16) every second */
        debug_send_status(d);
#endif

        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_SYSTEM_MS));
    }
}

/* ========================================================================= */
/*  Task Creation                                                            */
/* ========================================================================= */
void tasks_create_all(void)
{
    extern void fatal_init_error(void);

    /* Check each task creation individually — &= can mask failures */
    if (xTaskCreate(SensorTask,     "Sensor",    TASK_STACK_SENSOR,      NULL, TASK_PRIO_SENSOR,      NULL) != pdPASS) fatal_init_error();
    if (xTaskCreate(ControlTask,    "Control",   TASK_STACK_CONTROL,     NULL, TASK_PRIO_CONTROL,     NULL) != pdPASS) fatal_init_error();
    if (xTaskCreate(AlarmTask,      "Alarm",     TASK_STACK_ALARM,       NULL, TASK_PRIO_ALARM,       NULL) != pdPASS) fatal_init_error();
    if (xTaskCreate(CommScreenTask, "CommScr",   TASK_STACK_COMM_SCREEN, NULL, TASK_PRIO_COMM_SCREEN, NULL) != pdPASS) fatal_init_error();
    if (xTaskCreate(CommIPadTask,   "CommIPad",  TASK_STACK_COMM_IPAD,   NULL, TASK_PRIO_COMM_IPAD,   NULL) != pdPASS) fatal_init_error();
    if (xTaskCreate(SystemTask,     "System",    TASK_STACK_SYSTEM,      NULL, TASK_PRIO_SYSTEM,      NULL) != pdPASS) fatal_init_error();
}
