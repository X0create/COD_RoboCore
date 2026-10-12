/**
 * @file    pwm_stm32f4.c
 * @brief   PWM 的 STM32F4 实现，见 05_platform/pwm/pwm.h
 * @note    定时器的初始分频和周期由 CubeMX 配置（C 板）：
 *          - TIM10 通道 1（PF6，IMU 加热）：168 MHz / 1 / 2000 = 84 kHz（加热只看占空比，频率多少都行）；
 *          - TIM4 通道 3（PD14，蜂鸣器）：频率由 pwm_set_frequency() 设置。
 *          TIM10 挂在 APB2 上、TIM4 挂在 APB1 上，两条总线的定时器时钟不同，所以每路记下自己在哪条总线上。
 */
#include "05_platform/pwm/pwm.h"

#include <tim.h>

typedef struct
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
    bool on_apb2; /* 定时器挂在 APB2 上（否则 APB1） */
} PwmMap;

static const PwmMap channels[PWM_COUNT] = {
    [PWM_IMU_HEATER] = { &htim10, TIM_CHANNEL_1, true },
    [PWM_BUZZER] = { &htim4, TIM_CHANNEL_3, false },
};

static float duty_of[PWM_COUNT]; /* 当前占空比，改频率时保持 */

/* 定时器时钟：所在 APB 总线分频不为 1 时是 PCLK 的 2 倍（参考手册的定时器时钟规则） */
static uint32_t timer_clock_hz(const PwmMap *c)
{
    if (c->on_apb2)
    {
        const uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
        return ((RCC->CFGR & RCC_CFGR_PPRE2) == RCC_CFGR_PPRE2_DIV1) ? pclk2 : 2u * pclk2;
    }
    const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    return ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1) ? pclk1 : 2u * pclk1;
}

static void write_compare(const PwmMap *c, float duty)
{
    /* 比较值 = 占空比 × 一个周期的计数（ARR + 1）；等于 ARR + 1 时一直为高 */
    const float period = (float)(__HAL_TIM_GET_AUTORELOAD(c->htim) + 1u);
    __HAL_TIM_SET_COMPARE(c->htim, c->channel, (uint32_t)(duty * period + 0.5f));
}

bool pwm_start(PwmChannel ch)
{
    const PwmMap *c = &channels[ch];
    duty_of[ch] = 0.0f;
    __HAL_TIM_SET_COMPARE(c->htim, c->channel, 0u);
    return HAL_TIM_PWM_Start(c->htim, c->channel) == HAL_OK;
}

void pwm_set_duty(PwmChannel ch, float duty)
{
    if (duty < 0.0f)
    {
        duty = 0.0f;
    }
    else if (duty > 1.0f)
    {
        duty = 1.0f;
    }
    duty_of[ch] = duty;
    write_compare(&channels[ch], duty);
}

void pwm_set_frequency(PwmChannel ch, float hz)
{
    const PwmMap *c = &channels[ch];
    /* 一个周期的计数 = 时钟 / 频率；16 位计数器放不下时加大分频 */
    const uint32_t ticks = (uint32_t)((float)timer_clock_hz(c) / hz + 0.5f);
    const uint32_t psc = (ticks - 1u) / 65536u;
    const uint32_t arr = ticks / (psc + 1u) - 1u;
    __HAL_TIM_SET_PRESCALER(c->htim, psc);
    __HAL_TIM_SET_AUTORELOAD(c->htim, arr);
    write_compare(c, duty_of[ch]);
    c->htim->Instance->EGR = TIM_EGR_UG; /* 立即装入新的分频值（否则要等下一次溢出） */
}
