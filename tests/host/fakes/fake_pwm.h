/**
 * @file    fake_pwm.h
 * @brief   电脑测试用的假 PWM：记录每个通道是否启动、最后设置的占空比
 */
#pragma once

#include <stdbool.h>

#include "platform/pwm.h"

void fake_pwm_reset(void);
bool fake_pwm_started(PwmChannel ch);
/** 最后一次设置的占空比，已按 pwm_set_duty 的约定截到 0–1 */
float fake_pwm_duty(PwmChannel ch);
/** 最后一次设置的频率（Hz），没设置过为 0 */
float fake_pwm_frequency(PwmChannel ch);

/** pwm_set_duty 被调用的次数 */
unsigned fake_pwm_set_count(PwmChannel ch);
