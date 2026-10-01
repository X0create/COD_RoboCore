/**
 * @file    buzzer.c
 * @brief   蜂鸣器提示音，见 buzzer.h
 */
#include "buzzer.h"

#include "05_platform/pwm/pwm.h"

#define DUTY_ON 0.5f /* 无源蜂鸣器：50% 占空比的方波最响，频率决定音高 */

/* 每个音符 { 频率 Hz, 响多久 ms, 之后停多久 ms } */
static const BuzzerNote startup[] = { { 523u, 100u, 20u }, /* do mi sol 上行：开机 */
                                      { 659u, 100u, 20u },
                                      { 784u, 150u, 0u } };
static const BuzzerNote arm[] = { { 1568u, 80u, 20u }, { 2093u, 80u, 0u } }; /* 低→高：解锁 */
static const BuzzerNote disarm[] = { { 2093u, 80u, 20u }, { 1568u, 80u, 0u } }; /* 高→低：上锁 */
static const BuzzerNote low_battery[] = { { 2000u, 100u, 100u }, { 2000u, 100u, 0u } }; /* 嘀嘀 */

/* 一段提示音 = 音符数组 + 个数；PATTERN() 自动算个数 */
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

/* 开始响当前音符：设频率、打开占空比，从 0 开始计时 */
static void start_note(Buzzer *bz)
{
    pwm_set_frequency(PWM_BUZZER, (float)bz->notes[bz->index].freq_hz);
    pwm_set_duty(PWM_BUZZER, DUTY_ON);
    bz->sounding = true;
    bz->elapsed_ms = 0u;
}

/* 立即从第一个音符开始播放；正在播放的提示音被打断 */
void buzzer_play(Buzzer *bz, BuzzerPattern pattern)
{
    bz->notes = patterns[pattern].notes;
    bz->count = patterns[pattern].count;
    bz->index = 0u;
    bz->active = true;
    start_note(bz);
}

/*
 * 每个音符分两段：先响 on_ms，再静音 off_ms，然后进入下一个音符；最后一个音符结束就停。
 * 不用延时，由 indicator_task 每 25 ms 调用一次推进，所以音符时长的精度是 25 ms。
 */
void buzzer_step(Buzzer *bz, uint32_t dt_ms)
{
    if (!bz->active)
    {
        return;
    }
    bz->elapsed_ms += dt_ms;
    const BuzzerNote *note = &bz->notes[bz->index];
    if (bz->sounding) /* 响的那一段：时间到了就静音，进入停顿段 */
    {
        if (bz->elapsed_ms < note->on_ms)
        {
            return;
        }
        pwm_set_duty(PWM_BUZZER, 0.0f);
        bz->sounding = false;
        bz->elapsed_ms = 0u;
    }
    if (bz->elapsed_ms < note->off_ms) /* 停顿段还没结束 */
    {
        return;
    }
    if (++bz->index < bz->count) /* 下一个音符；没有了就结束 */
    {
        start_note(bz);
    }
    else
    {
        bz->active = false;
    }
}
