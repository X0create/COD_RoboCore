/**
 * @file    pwm.h
 * @brief   PWM 输出：按用途命名的通道，设置占空比（ADR 0033）
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
    PWM_COUNT,
} PwmChannel;

/**
 * @brief   以 0 占空比开始输出
 * @return  false：这块板没有这个通道，或 HAL 启动失败
 */
RM_NODISCARD bool pwm_start(PwmChannel ch);

/** 设置占空比 0–1；超出范围截到 0 或 1（负数不会变成满占空比） */
void pwm_set_duty(PwmChannel ch, float duty);

#ifdef __cplusplus
}
#endif
