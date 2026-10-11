/**
 * @file    quat_ekf.h
 * @brief   四元数扩展卡尔曼姿态解算（纯计算，电脑上可测）
 * @note    移植自 COD-H7-Template `Components/Algorithm/Src/Quaternion.c`，数学不变：
 *          - 状态 x = [q0 q1 q2 q3 bx by]（q 从传感器系转到世界系，bx、by 为 x、y 轴陀螺零偏；z 轴零偏不估计）；
 *          - 预测：陀螺积分；更新：归一化加速度对比预测的重力方向；四元数 z 分量不接受加速度修正（加速度看不出偏航）；
 *          - 零偏行的增益乘以该轴重力方向夹角 / (π/2)，零偏修正每次限幅 ±0.01·dt；
 *          - 卡方检验：新息太大时跳过本次修正，连续 50 次仍太大（且陀螺静止、加速度接近重力）才重新接受。
 *          与旧代码的差异：
 *          - 卡方值按公式 rᵀS⁻¹r 计算。旧代码转置了 3×3 的 S⁻¹ 而不是新息 r，写入按 1×3 声明的缓存
 *            （arm_mat_trans 尺寸检查打开时不写、关闭时越界写 9 个数），无法照搬；
 *          - “连续 50 次后重新接受”那一次，旧代码的增益系数取自上述缓存里的残留值，这里取 1；
 *          - 矩阵存储在结构体里（静态），不再 malloc；五个卡尔曼步骤用 03_algorithm/filter/kalman 的公开函数组合；
 *          - 快速平方根倒数（指针强转，未定义行为）换成 1 / sqrtf；输出的 q 归一化。
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
    /* 参数（旧工程 INS_Task：Q1 10、Q2 0.001、R 1e6；卡方门限 1e-8；当地重力 9.718） */
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

/** 初始化：q = [1 0 0 0]，零偏 0，协方差为旧工程的初值 */
void quat_ekf_init(QuatEkf *ekf, float q_quat, float q_bias, float r_accel);

/**
 * @brief   更新一次
 * @param   gyro_rad_s  角速度（已减上电标定的零偏）
 * @param   accel_m_s2  加速度（旧工程先经过二阶低通）
 * @param   dt_s        距上次更新的实际时间（ins 实测）
 */
void quat_ekf_update(QuatEkf *ekf, const float gyro_rad_s[3], const float accel_m_s2[3],
                     float dt_s);

/** 由 q 算 ZYX 欧拉角（航向、俯仰、横滚），只用于显示和调试 */
void quat_ekf_to_euler(const float q[4], float *yaw_rad, float *pitch_rad, float *roll_rad);

#ifdef __cplusplus
}
#endif
