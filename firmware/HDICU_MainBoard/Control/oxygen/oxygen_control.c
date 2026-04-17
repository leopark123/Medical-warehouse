/**
 * @file    oxygen_control.c
 * @brief   O2 control — valve on/off with hysteresis (-2% = -20 in x10)
 *
 * O2 阀门开启 (OR逻辑, 任一成立):
 *   - 传感器自动供氧 (滞环控制, 需o2_valid)
 *   - 手动开放供氧 (setpoint.open_o2)
 *   - 外部请求 (PD8总制氧机 或 PB6制氧机, active-low)
 *
 * Fail-safe: O2传感器离线时关阀, 除非手动开放或外部请求.
 * 互锁规则最终裁决 (interlock_apply).
 */
#include "oxygen_control.h"
#include "bsp_config.h"

#define O2_HYSTERESIS_X10   20  /* 2.0% in x10 units */

void oxygen_control_update(AppData_t *d)
{
    bool external_demand = d->sensor.o2_master_demand || d->sensor.o2_req_demand;

    /* FAIL-SAFE: O2 sensor offline → close O2 valve
     * 例外: 手动开放供氧模式 或 外部请求 时继续执行 */
    if (!d->sensor.o2_valid &&
        d->control.o2_state != O2_STATE_OPEN_MODE &&
        !external_demand) {
        d->control.relay_status &= ~(1U << BSP_RELAY_O2_IO);
        d->control.o2_state = O2_STATE_IDLE;
        return;
    }

    /* 滞环状态机 (仅在传感器有效时更新) */
    if (d->sensor.o2_valid) {
        int16_t actual = (int16_t)d->sensor.o2_raw;
        int16_t setpoint = (int16_t)d->setpoint.target_o2;

        switch (d->control.o2_state) {
        case O2_STATE_IDLE:
            if (actual < setpoint - O2_HYSTERESIS_X10) {
                d->control.o2_state = O2_STATE_SUPPLYING;
            }
            break;

        case O2_STATE_SUPPLYING:
            if (actual >= setpoint) {
                d->control.o2_state = O2_STATE_IDLE;
            }
            break;

        case O2_STATE_OPEN_MODE:
            /* Open O2 mode: valve stays ON, managed by interlock module */
            break;
        }
    }

    /* Handle open O2 mode from setpoint */
    if (d->setpoint.open_o2) {
        d->control.o2_state = O2_STATE_OPEN_MODE;
    } else if (d->control.o2_state == O2_STATE_OPEN_MODE) {
        d->control.o2_state = O2_STATE_IDLE;
    }

    /* Apply relay — OR逻辑: 状态机自动供氧/开放模式/外部请求 任一成立则开阀
     * 外部请求(PD8 or PB6低电平)仍受互锁规则约束, 互锁会在interlock_apply中最终裁决 */
    uint16_t *r = &d->control.relay_status;
    if (d->control.o2_state == O2_STATE_SUPPLYING ||
        d->control.o2_state == O2_STATE_OPEN_MODE ||
        external_demand) {
        *r |= (1U << BSP_RELAY_O2_IO);
    } else {
        *r &= ~(1U << BSP_RELAY_O2_IO);
    }
}
