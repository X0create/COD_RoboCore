/**
 * @file    battery.c
 * @brief   电池电压与低电量提示，见 battery.h
 */
#include "battery.h"

void battery_init(Battery *bat, const BatteryConfig *cfg)
{
    *bat = (Battery){ .cfg = cfg };
}

bool battery_update(Battery *bat, float pin_volts, uint64_t now_us)
{
    const BatteryConfig *cfg = bat->cfg;
    bat->voltage_v = pin_volts * cfg->divider; /* ADC 引脚电压 × 分压比 = 电池电压 */

    /* 记下“从什么时候开始低于阈值”；电机加速时电压会短暂跌落，要持续 hold_ms 才算低电量 */
    if (bat->voltage_v >= cfg->low_v)
    {
        bat->below = false;
    }
    else if (!bat->below)
    {
        bat->below = true;
        bat->below_since_us = now_us;
    }

    /* 回差：低电量后要回到更高的 recover_v 才解除，电压在阈值附近抖动时提示不会反复开关 */
    if (bat->low && bat->voltage_v > cfg->recover_v)
    {
        bat->low = false;
    }
    else if (!bat->low && bat->below
             && now_us - bat->below_since_us >= (uint64_t)cfg->hold_ms * 1000u)
    {
        bat->low = true;
    }
    return bat->low;
}
