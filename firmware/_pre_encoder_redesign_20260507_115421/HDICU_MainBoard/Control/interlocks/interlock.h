/**
 * @file    interlock.h
 * @brief   Safety interlock rules (Frozen — 开发基线 6.3.5 + 冻结总纲 第六章)
 *
 *          RULES (immutable):
 *          1. Cooling ↔ Heating: mutually exclusive
 *          2. UV disinfect ↔ Open O2: mutually exclusive
 *          3. Fogging ↔ UV disinfect: mutually exclusive
 *          4. Open O2 mode:
 *             - Inner cycle: FORBIDDEN
 *             - Outer cycle: FORCED ON
 *             - Fresh air: FORCED ON
 *             - Cooling: CONDITIONAL (only if outer/fresh already on)
 *             - Heating: FORBIDDEN
 *             - Fogging: FORBIDDEN
 *             - UV disinfect: FORBIDDEN
 *          5. During fogging: Open O2 forbidden
 *          6. Alarm state: related actuators may be force-off
 *          7. High humidity: may engage cooling for dehumidification
 */

#ifndef INTERLOCK_H
#define INTERLOCK_H

#include "app_data.h"
#include <stdbool.h>

/* Apply all interlock rules. Call before actuator output.
 * Modifies control state in-place to enforce safety constraints.
 * Returns true if any interlock was triggered (for logging). */
bool interlock_apply(AppData_t *d);

/* Check if a specific action is allowed given current state */
bool interlock_can_start_open_o2(const AppData_t *d);
bool interlock_can_start_fogging(const AppData_t *d);
bool interlock_can_start_uv(const AppData_t *d);
bool interlock_can_start_heating(const AppData_t *d);
bool interlock_can_start_cooling(const AppData_t *d);

#endif
