/**
 * @file    lpf.c
 * @brief   一阶、二阶低通滤波，见 lpf.h
 */
#include "lpf.h"

void lpf1_init(Lpf1 *lpf, float alpha)
{
    lpf->alpha = alpha;
    lpf->output = 0.0f;
    lpf->primed = false;
}

/* y = α·y上次 + (1 − α)·x：α 越大越平滑、延迟越大 */
float lpf1_update(Lpf1 *lpf, float input)
{
    if (!lpf->primed) /* 第一个样本直接当输出，避免从 0 慢慢爬上来 */
    {
        lpf->output = input;
        lpf->primed = true;
    }
    lpf->output = lpf->alpha * lpf->output + (1.0f - lpf->alpha) * input;
    return lpf->output;
}

void lpf2_init(Lpf2 *lpf, const float a[3])
{
    lpf->a[0] = a[0];
    lpf->a[1] = a[1];
    lpf->a[2] = a[2];
    lpf->y1 = 0.0f;
    lpf->y2 = 0.0f;
    lpf->primed = false;
}

/* y = a0·y[n-1] + a1·y[n-2] + a2·x */
float lpf2_update(Lpf2 *lpf, float input)
{
    if (!lpf->primed) /* 第一个样本填满历史值 */
    {
        lpf->y1 = input;
        lpf->y2 = input;
        lpf->primed = true;
    }
    const float y = lpf->a[0] * lpf->y1 + lpf->a[1] * lpf->y2 + lpf->a[2] * input;
    lpf->y2 = lpf->y1;
    lpf->y1 = y;
    return y;
}
