/**
 * @file    battery.h
 * @brief   电池电压与低电量提示（纯逻辑，电脑上可测；ADR 0027 第一版只提示，不限制动作）
 * @note    电压 = ADC 引脚电压 × 分压比。连续 hold_ms 低于 low_v 才判为低电量（电机启动时的压降不误报），
 *          回到 recover_v 以上才解除（阈值附近不来回跳）。
 *          COD-H7-Template 的 bsp_adc.c 只换算电压（×11），没有使用者。
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
    float divider; /* 电池电压 / 引脚电压；MC02 为 11（旧工程，待万用表核对） */
    float low_v;   /* 低于它开始计时 */
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

void battery_init(Battery *bat, const BatteryConfig *cfg);

/** 用一次 ADC 读数更新；返回是否低电量 */
bool battery_update(Battery *bat, float pin_volts, uint64_t now_us);

#ifdef __cplusplus
}
#endif
