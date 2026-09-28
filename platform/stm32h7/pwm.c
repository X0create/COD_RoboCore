/**
 * @file    pwm.c
 * @brief   PWM 的 STM32H7 实现，见 platform/pwm.h
 * @note    定时器的分频和周期由 CubeMX 配置。TIM3（加热片）：275 MHz / 80 / 20001 ≈ 172 Hz
 *          （旧工程时钟下为 100 Hz；加热只看占空比，频率不影响，见 CHANGES）。
 */
#include "platform/pwm.h"

#include "tim.h"

typedef struct
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
} PwmMap;

static const PwmMap channels[PWM_COUNT] = {
    [PWM_IMU_HEATER] = { &htim3, TIM_CHANNEL_4 },
};

bool pwm_start(PwmChannel ch)
{
    const PwmMap *c = &channels[ch];
    __HAL_TIM_SET_COMPARE(c->htim, c->channel, 0u);
    return HAL_TIM_PWM_Start(c->htim, c->channel) == HAL_OK;
}

void pwm_set_duty(PwmChannel ch, float duty)
{
    const PwmMap *c = &channels[ch];
    if (duty < 0.0f)
    {
        duty = 0.0f;
    }
    else if (duty > 1.0f)
    {
        duty = 1.0f;
    }
    /* 比较值 = 占空比 × 一个周期的计数（ARR + 1）；等于 ARR + 1 时一直为高 */
    const float period = (float)(__HAL_TIM_GET_AUTORELOAD(c->htim) + 1u);
    __HAL_TIM_SET_COMPARE(c->htim, c->channel, (uint32_t)(duty * period + 0.5f));
}
