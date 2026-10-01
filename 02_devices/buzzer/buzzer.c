/**
 * @file    buzzer.c
 * @brief   蜂鸣器提示音，见 buzzer.h
 */
#include "buzzer.h"

#include "05_platform/pwm/pwm.h"

#define DUTY_ON 0.5f

static const BuzzerNote startup[] = { { 523u, 100u, 20u },
                                      { 659u, 100u, 20u },
                                      { 784u, 150u, 0u } };
static const BuzzerNote arm[] = { { 1568u, 80u, 20u }, { 2093u, 80u, 0u } };
static const BuzzerNote disarm[] = { { 2093u, 80u, 20u }, { 1568u, 80u, 0u } };
static const BuzzerNote low_battery[] = { { 2000u, 100u, 100u }, { 2000u, 100u, 0u } };

typedef struct
{
    const BuzzerNote *notes;
    uint8_t count;
} Pattern;

#define PATTERN(a)                                                                                 \
    {                                                                                              \
        (a), (uint8_t)(sizeof(a) / sizeof((a)[0]))                                                 \
    }

static const Pattern patterns[BUZZER_PATTERN_COUNT] = {
    [BUZZER_STARTUP] = PATTERN(startup),
    [BUZZER_ARM] = PATTERN(arm),
    [BUZZER_DISARM] = PATTERN(disarm),
    [BUZZER_LOW_BATTERY] = PATTERN(low_battery),
};

bool buzzer_init(Buzzer *bz)
{
    *bz = (Buzzer){ 0 };
    return pwm_start(PWM_BUZZER);
}

static void start_note(Buzzer *bz)
{
    pwm_set_frequency(PWM_BUZZER, (float)bz->notes[bz->index].freq_hz);
    pwm_set_duty(PWM_BUZZER, DUTY_ON);
    bz->sounding = true;
    bz->elapsed_ms = 0u;
}

void buzzer_play(Buzzer *bz, BuzzerPattern pattern)
{
    bz->notes = patterns[pattern].notes;
    bz->count = patterns[pattern].count;
    bz->index = 0u;
    bz->active = true;
    start_note(bz);
}

void buzzer_step(Buzzer *bz, uint32_t dt_ms)
{
    if (!bz->active)
    {
        return;
    }
    bz->elapsed_ms += dt_ms;
    const BuzzerNote *note = &bz->notes[bz->index];
    if (bz->sounding)
    {
        if (bz->elapsed_ms < note->on_ms)
        {
            return;
        }
        pwm_set_duty(PWM_BUZZER, 0.0f);
        bz->sounding = false;
        bz->elapsed_ms = 0u;
    }
    if (bz->elapsed_ms < note->off_ms)
    {
        return;
    }
    if (++bz->index < bz->count)
    {
        start_note(bz);
    }
    else
    {
        bz->active = false;
    }
}
