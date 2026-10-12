/**
 * @file    quat_ekf.h
 * @brief   四元数扩展卡尔曼姿态解算（纯计算，电脑上可测）
 * @note    思路：陀螺仪积分得到姿态（短时间准，但零偏会让它慢慢漂），加速度计测到的重力方向
 *          能告诉我们“哪边是下”（不漂，但车加减速时会被干扰），EKF 按两者各自的可信程度把它们融合起来。
 *          - 状态 x = [q0 q1 q2 q3 bx by]（q 从传感器系转到世界系，bx、by 为 x、y 轴陀螺零偏；z 轴零偏不估计）；
 *          - 预测：陀螺积分；更新：归一化加速度对比预测的重力方向；四元数 z 分量不接受加速度修正（加速度看不出偏航）；
 *          - 零偏行的增益乘以该轴重力方向夹角 / (π/2)，零偏修正每次限幅 ±0.01·dt；
 *          - 卡方检验：新息太大时跳过本次修正，连续 50 次仍太大（且陀螺静止、加速度接近重力）才重新接受。
 *            新息 = 实测加速度方向 − 预测的重力方向；太大说明车在剧烈加减速或被撞，这时加速度不可信。
 *          实现要点：
 *          - 卡方值按公式 rᵀS⁻¹r 计算（r 为新息，S 为新息协方差）；
 *          - “连续 50 次后重新接受”那一次，增益系数取 1；
 *          - 矩阵存储在结构体里（静态），不用 malloc；五个卡尔曼步骤用 03_algorithm/filter/kalman 的公开函数组合；
 *          - 归一化用 1 / sqrtf（不用“快速平方根倒数”的指针强转技巧，那是未定义行为）；输出的 q 归一化。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "03_algorithm/filter/kalman.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    /* 参数（常用值：Q1 10、Q2 0.001、R 1e6；卡方门限 1e-8；当地重力 9.718，换地方可按当地重力改） */
    float q_quat;  /* 四元数过程噪声，每秒 */
    float q_bias;  /* 零偏过程噪声，每秒 */
    float r_accel; /* 加速度测量噪声 */
    float chi_threshold;
    float gravity_m_s2;

    KalmanFilter kf;
    float storage[KALMAN_STORAGE_FLOATS(6u, 3u, 0u)];

    /* 卡方检验状态 */
    bool chi_result;   /* 最近是否处于“新息正常”的状态 */
    uint8_t chi_count; /* 新息连续偏大的次数 */

    /* 输出 */
    float q[4];         /* 传感器系 → 世界系，[w x y z]，已归一化 */
    float gyro_bias[3]; /* 估计的零偏（z 恒为 0），rad/s */
} QuatEkf;

/** 初始化：q = [1 0 0 0]（还没转过），零偏 0，协方差取较大的初值（刚开始不确定，让滤波器快速收敛） */
void quat_ekf_init(QuatEkf *ekf, float q_quat, float q_bias, float r_accel);

/**
 * @brief   更新一次
 * @param   gyro_rad_s  角速度（已减上电标定的零偏）
 * @param   accel_m_s2  加速度（调用者先过二阶低通，滤掉振动）
 * @param   dt_s        距上次更新的实际时间（ins 实测）
 */
void quat_ekf_update(QuatEkf *ekf, const float gyro_rad_s[3], const float accel_m_s2[3],
                     float dt_s);

/** 由 q 算 ZYX 欧拉角（航向、俯仰、横滚），只用于显示和调试 */
void quat_ekf_to_euler(const float q[4], float *yaw_rad, float *pitch_rad, float *roll_rad);

#ifdef __cplusplus
}
#endif
