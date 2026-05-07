/**
 * @file    light_ctrl.c
 * @brief   Stage 8: 灯光软互斥归一化实现
 */
#include "light_ctrl.h"

uint8_t light_ctrl_normalize(uint8_t raw)
{
    uint8_t treat = (uint8_t)(raw & 0x0C);   /* bit2 蓝 + bit3 红 */
    uint8_t basic = (uint8_t)(raw & 0x03);   /* bit0 检查 + bit1 照明 */

    if (treat) {
        /* 治疗组激活 → 强制清照明 + 检查 */
        if ((treat & 0x0C) == 0x0C) {
            /* 蓝 + 红 同时被设置 = 异常输入, 整体全灭 */
            return 0;
        }
        return treat;
    }
    /* 治疗组未激活 → 照明 + 检查可共存, 按原值 */
    return basic;
}
