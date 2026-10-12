/**
 * @file    battery.h
 * @brief   电池电压与低电量提示（纯逻辑，电脑上可测；只提示，不限制车的动作）
 * @note    电压 = ADC 引脚电压 × 分压比。连续 hold_ms 低于 low_v 才判为低电量（电机启动时的压降不误报），
 *          回到 recover_v 以上才解除（阈值附近不来回跳）。
 *          只提示、不限速：要不要回去换电由操作手决定。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    /* 电池电压 / 引脚电压；MC02 为 11（板上电阻分压，可用万用表量电池电压对照核对） */
    float divider;
    float low_v;     /* 低于它开始计时 */
    float recover_v; /* 高于它解除低电量 */
    uint32_t hold_ms;
} BatteryConfig;

typedef struct
{
    const BatteryConfig *cfg;
    float voltage_v;
    bool low;
    bool below; /* 当前低于 low_v，正在计时 */
    uint64_t below_since_us;
} Battery;

/** 清零状态并记下配置（cfg 在整个运行期间有效）；初始为“不低” */
void battery_init(Battery *bat, const BatteryConfig *cfg);

/** 用一次 ADC 读数更新；返回是否低电量 */
bool battery_update(Battery *bat, float pin_volts, uint64_t now_us);

#ifdef __cplusplus
}
#endif
