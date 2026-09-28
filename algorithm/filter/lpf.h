/**
 * @file    lpf.h
 * @brief   一阶、二阶低通滤波（纯计算，电脑上可测）
 * @note    移植自 COD-H7-Template `Components/Algorithm/Src/LPF.c`，计算公式不变。
 *          两种滤波器都用第一次输入填满历史值，避免从 0 慢慢爬升。
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * 一阶低通：y = alpha * y_prev + (1 - alpha) * x。
 * alpha 在 0–1 之间：越大越平滑、滞后越大；越小越接近原始输入、噪声越大。
 */
typedef struct
{
    float alpha;
    float output;
    bool primed; /* 是否已用第一次输入填充过 output */
} Lpf1;

/**
 * 二阶低通（IIR）：y = a[0] * y[n-1] + a[1] * y[n-2] + a[2] * x。
 * 系数由采样率和截止频率离线算出；a[0] + a[1] + a[2] = 1 时直流增益为 1。
 */
typedef struct
{
    float a[3];
    float y1; /* y[n-1] */
    float y2; /* y[n-2] */
    bool primed;
} Lpf2;

/** @pre 0 <= alpha < 1 */
void lpf1_init(Lpf1 *lpf, float alpha);
float lpf1_update(Lpf1 *lpf, float input);

void lpf2_init(Lpf2 *lpf, const float a[3]);
float lpf2_update(Lpf2 *lpf, float input);

#ifdef __cplusplus
}
#endif
