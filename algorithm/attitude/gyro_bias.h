/**
 * @file    gyro_bias.h
 * @brief   陀螺零偏标定：静止时采一段样本，平均值即零偏（纯计算，电脑上可测）
 * @note    静止判据用**标准差**，不用峰峰值：峰峰值随采样数增大而增大，采样越多越必然判为“不静止”，
 *          UniC 因此标定从未成功过而没人发现（UniC `imu-calibration-stillness`，附录 A.1）。
 *          另外拒绝“很稳但均值过大”（板子被斜着放、或在匀速转）。
 *          标准差用 Welford 递推，不用“平方和 − 和的平方”，避免相减时丢精度。
 *          何时标定、失败怎么上报由调用者（第 8 步 ins）决定；本模块只算。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    uint32_t count;
    float mean[3];
    float m2[3]; /* 与均值之差的平方和（Welford） */
} GyroBias;

typedef enum
{
    GYRO_BIAS_OK,
    GYRO_BIAS_TOO_FEW,   /* 样本数不够 */
    GYRO_BIAS_NOT_STILL, /* 某轴标准差超过阈值 */
    GYRO_BIAS_TOO_LARGE, /* 某轴均值超过阈值 */
} GyroBiasResult;

void gyro_bias_reset(GyroBias *gb);

/** 加入一个样本（rad/s，未减零偏） */
void gyro_bias_add(GyroBias *gb, const float gyro_rad_s[3]);

/**
 * @brief   判断能否采用，能则输出零偏
 * @param   min_count  至少多少个样本
 * @param   max_std    每轴标准差上限（rad/s）
 * @param   max_bias   每轴均值绝对值上限（rad/s）
 * @param   bias_out   结果为 GYRO_BIAS_OK 时写入三轴零偏
 */
GyroBiasResult gyro_bias_result(const GyroBias *gb, uint32_t min_count, float max_std,
                                float max_bias, float bias_out[3]);

#ifdef __cplusplus
}
#endif
