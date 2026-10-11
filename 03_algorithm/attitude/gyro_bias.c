/**
 * @file    gyro_bias.c
 * @brief   陀螺零偏标定，见 gyro_bias.h
 */
#include "gyro_bias.h"

#include <math.h>

void gyro_bias_reset(GyroBias *gb)
{
    *gb = (GyroBias){ 0 };
}

/*
 * 加一个样本，逐个更新均值和方差（Welford 算法）：不用保存全部 2000 个样本，
 * 也不会出现“平方和减去和的平方”那种大数相减丢精度的问题
 */
void gyro_bias_add(GyroBias *gb, const float gyro_rad_s[3])
{
    gb->count++;
    for (int i = 0; i < 3; i++)
    {
        const float delta = gyro_rad_s[i] - gb->mean[i];
        gb->mean[i] += delta / (float)gb->count;
        gb->m2[i] += delta * (gyro_rad_s[i] - gb->mean[i]);
    }
}

GyroBiasResult gyro_bias_result(const GyroBias *gb, uint32_t min_count, float max_std,
                                float max_bias, float bias_out[3])
{
    if (gb->count < min_count || gb->count < 2u)
    {
        return GYRO_BIAS_TOO_FEW;
    }
    /* 三个轴都要满足：抖动小（板子没在动），均值也小（零偏在正常范围）才采用 */
    for (int i = 0; i < 3; i++)
    {
        const float std = sqrtf(gb->m2[i] / (float)(gb->count - 1u));
        if (std > max_std)
        {
            return GYRO_BIAS_NOT_STILL;
        }
        if (fabsf(gb->mean[i]) > max_bias)
        {
            return GYRO_BIAS_TOO_LARGE;
        }
    }
    for (int i = 0; i < 3; i++)
    {
        bias_out[i] = gb->mean[i];
    }
    return GYRO_BIAS_OK;
}
