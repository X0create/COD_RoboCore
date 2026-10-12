/**
 * @file    buzzer.h
 * @brief   蜂鸣器提示音：按音符序列播放，不阻塞
 * @note    MC02 板载蜂鸣器接 TIM12 通道 2（PB15），改 PWM 频率就是改音调，占空比 50% 发声、0 静音。
 *          “不阻塞”：buzzer_play() 只记下要播哪段，立刻返回；之后调用者定期调用 buzzer_step() 推进到下一个音符，
 *          所以播放期间任务照常运行（本框架在 25 ms 的 indicator_task 里调用）。只在一个任务里调用。
 *          加一段新提示音：在 buzzer.c 的音符表里加一行，再在下面的枚举里加一个名字。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    uint16_t freq_hz;
    uint16_t on_ms;
    uint16_t off_ms;
} BuzzerNote;

typedef enum
{
    BUZZER_STARTUP,     /* 启动完成：三声上行 */
    BUZZER_ARM,         /* 解锁：两声上行短音 */
    BUZZER_DISARM,      /* 回到 Safe：两声下行短音 */
    BUZZER_LOW_BATTERY, /* 低电量：两声短促高音 */
    BUZZER_PATTERN_COUNT,
} BuzzerPattern;

typedef struct
{
    const BuzzerNote *notes;
    uint8_t count;
    uint8_t index;
    bool active;
    bool sounding;       /* 当前处于发声段（否则是音符后的静音段） */
    uint32_t elapsed_ms; /* 当前段已经过的时间 */
} Buzzer;

/** @return false：PWM 启动失败 @pre 调度器已启动 */
RM_NODISCARD bool buzzer_init(Buzzer *bz);

/** 开始播放一段提示音，打断正在播放的 */
void buzzer_play(Buzzer *bz, BuzzerPattern pattern);

/** 推进 dt_ms 毫秒 */
void buzzer_step(Buzzer *bz, uint32_t dt_ms);

#ifdef __cplusplus
}
#endif
