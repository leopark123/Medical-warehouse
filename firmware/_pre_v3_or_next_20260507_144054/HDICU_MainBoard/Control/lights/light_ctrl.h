/**
 * @file    light_ctrl.h
 * @brief   Stage 8: 灯光软互斥归一化 (α 方案)
 *
 *          规则: 检查/照明 vs 治疗组 (蓝/红) 软互斥, 治疗组优先.
 *          - 治疗组激活 → 强制清照明 + 检查
 *          - 蓝 + 红同时被设置 → 全灭 (异常输入兜底)
 *          - 治疗组未激活 → 照明 / 检查可独立共存
 *
 *          调用点: 屏幕 0x82 case 0x02/0x03/0x04 + iPad 0x03 写参数 + 0x0B 复位.
 */
#ifndef LIGHT_CTRL_H
#define LIGHT_CTRL_H

#include <stdint.h>

/**
 * @brief 归一化 light_ctrl 4-bit 位图, 应用 α 软互斥规则.
 * @param raw 输入: bit0=检查 bit1=照明 bit2=蓝 bit3=红
 * @return 归一化后的 4-bit 位图, 至多一组激活.
 *
 * 截断真值表 (16 输入):
 *   0x00 → 0x00 (全灭)
 *   0x01 → 0x01 (检查)
 *   0x02 → 0x02 (照明)
 *   0x03 → 0x03 (检查 + 照明, 共存)
 *   0x04 → 0x04 (蓝)        — 治疗组激活
 *   0x05 → 0x04 (检查 + 蓝 → 蓝赢)
 *   0x06 → 0x04 (照明 + 蓝 → 蓝赢)
 *   0x07 → 0x04 (检查 + 照明 + 蓝 → 蓝赢)
 *   0x08 → 0x08 (红)
 *   0x09 → 0x08
 *   0x0A → 0x08
 *   0x0B → 0x08
 *   0x0C → 0x00 (蓝 + 红 → 互冲全灭)
 *   0x0D → 0x00
 *   0x0E → 0x00
 *   0x0F → 0x00 (全 1 → 互冲全灭)
 */
uint8_t light_ctrl_normalize(uint8_t raw);

#endif /* LIGHT_CTRL_H */
