/**
 * @file    pwm_stm32h7.c
 * @brief   PWM 的 STM32H7 实现，见 05_platform/pwm/pwm.h
 * @note    定时器的初始分频和周期由 CubeMX 配置：
 *          - TIM3 通道 4（PB1，加热片）：275 MHz / 80 / 20001 ≈ 172 Hz（旧工程时钟下为 100 Hz；加热只看占空比）；
 *          - TIM12 通道 2（PB15，蜂鸣器）：频率由 pwm_set_frequency() 设置。
 *          两个定时器都挂在 APB1 上。
 */
#include "05_platform/pwm/pwm.h"

#include <tim.h>

typedef struct
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
} PwmMap;

static const PwmMap channels[PWM_COUNT] = {
    [PWM_IMU_HEATER] = { &htim3, TIM_CHANNEL_4 },
    [PWM_BUZZER] = { &htim12, TIM_CHANNEL_2 },
};

static float duty_of[PWM_COUNT]; /* 当前占空比，改频率时保持 */

/* APB1 定时器时钟：APB1 分频不为 1 时是 PCLK1 的 2 倍（参考手册定时器时钟规则，TIMPRE = 0） */
static uint32_t apb1_timer_clock_hz(void)
{
    const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    return ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) == RCC_APB1_DIV1) ? pclk1 : 2u * pclk1;
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
    const uint32_t ticks = (uint32_t)((float)apb1_timer_clock_hz() / hz + 0.5f);
    const uint32_t psc = (ticks - 1u) / 65536u;
    const uint32_t arr = ticks / (psc + 1u) - 1u;
    __HAL_TIM_SET_PRESCALER(c->htim, psc);
    __HAL_TIM_SET_AUTORELOAD(c->htim, arr);
    write_compare(c, duty_of[ch]);
    c->htim->Instance->EGR = TIM_EGR_UG; /* 立即装入新的分频值（否则要等下一次溢出） */
}
