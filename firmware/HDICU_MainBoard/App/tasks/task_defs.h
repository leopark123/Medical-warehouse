/**
 * @file    task_defs.h
 * @brief   FreeRTOS task declarations — 6 tasks per frozen spec 6.1
 *
 *          Task          | Priority | Period  | Function
 *          --------------|----------|---------|---------------------------
 *          SensorTask    | High     | 100ms   | ADC + UART sensor parsing
 *          ControlTask   | High     | 200ms   | Temp/humid/O2 + relay/PWM
 *          AlarmTask     | High     | 100ms   | Alarm detection + buzzer
 *          CommScreenTask| Medium   | 100ms   | Screen board communication
 *          CommIPadTask  | Medium   | Event   | iPad protocol handler
 *          SystemTask    | Low      | 1000ms  | Watchdog/time/flash save
 */

#ifndef TASK_DEFS_H
#define TASK_DEFS_H

/* Create all 6 FreeRTOS tasks. Call once from main() after HAL and BSP init. */
void tasks_create_all(void);

#endif
