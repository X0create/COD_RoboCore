/**
 * @file    pwm.h
 * @brief   PWM 输出：按用途命名的通道，设置占空比和频率（ADR 0033）
 */
#pragma once

#include <stdbool.h>

#include "platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    PWM_IMU_HEATER, /* IMU 加热片 */
    PWM_BUZZER,     /* 蜂鸣器（改频率就是改音调） */
    PWM_COUNT,
} PwmChannel;

/**
 * @brief   以 0 占空比开始输出
 * @return  false：这块板没有这个通道，或 HAL 启动失败
 */
RM_NODISCARD bool pwm_start(PwmChannel ch);

/** 设置占空比 0–1；超出范围截到 0 或 1（负数不会变成满占空比） */
void pwm_set_duty(PwmChannel ch, float duty);

/**
 * @brief   设置频率（Hz），保持当前占空比
 * @note    会重设定时器计数，同一个定时器上的其他通道也受影响；只用于独占定时器的通道（如蜂鸣器）
 * @pre     hz > 0
 */
void pwm_set_frequency(PwmChannel ch, float hz);

#ifdef __cplusplus
}
#endif
