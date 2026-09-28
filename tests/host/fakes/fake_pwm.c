/**
 * @file    fake_pwm.c
 * @brief   假 PWM，见 fake_pwm.h；代替 platform/stm32h7/pwm.c
 */
#include "fake_pwm.h"

static bool started[PWM_COUNT];
static float duty[PWM_COUNT];
static unsigned set_count[PWM_COUNT];
static float freq[PWM_COUNT];

void fake_pwm_reset(void)
{
    for (int i = 0; i < (int)PWM_COUNT; i++)
    {
        started[i] = false;
        duty[i] = 0.0f;
        set_count[i] = 0u;
        freq[i] = 0.0f;
    }
}

bool pwm_start(PwmChannel ch)
{
    started[ch] = true;
    duty[ch] = 0.0f;
    return true;
}

void pwm_set_duty(PwmChannel ch, float d)
{
    duty[ch] = (d < 0.0f) ? 0.0f : ((d > 1.0f) ? 1.0f : d);
    set_count[ch]++;
}

void pwm_set_frequency(PwmChannel ch, float hz)
{
    freq[ch] = hz;
}

float fake_pwm_frequency(PwmChannel ch)
{
    return freq[ch];
}

bool fake_pwm_started(PwmChannel ch)
{
    return started[ch];
}

float fake_pwm_duty(PwmChannel ch)
{
    return duty[ch];
}

unsigned fake_pwm_set_count(PwmChannel ch)
{
    return set_count[ch];
}
