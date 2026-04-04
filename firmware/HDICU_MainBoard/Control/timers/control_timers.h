/**
 * @file    control_timers.h
 * @brief   Fog/disinfect countdown + O2 accumulation timers
 *          All timer logic in main controller (唯一真值源).
 *          Screen board only displays, does not keep state.
 */
#ifndef CONTROL_TIMERS_H
#define CONTROL_TIMERS_H
#include "app_data.h"

/* Call once per second from ControlTask or SystemTask */
void control_timers_tick_1s(AppData_t *d);

/* Start fogging countdown. duration_sec = 0 means stop. */
void control_timers_start_fog(AppData_t *d, uint16_t duration_sec);

/* Start disinfect countdown. */
void control_timers_start_disinfect(AppData_t *d, uint16_t duration_sec);

/* Reset O2 accumulated time to 0 */
void control_timers_reset_o2_accum(AppData_t *d);

#endif
