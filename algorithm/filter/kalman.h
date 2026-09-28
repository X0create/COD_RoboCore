/**
 * @file    kalman.h
 * @brief   线性卡尔曼滤波（纯计算，电脑上可测），也是扩展卡尔曼（EKF）的基础
 * @note    移植自 COD-H7-Template `Components/Algorithm/Src/Kalman_Filter.c`，五个步骤的计算公式和顺序不变：
 *            1. 状态预测     x⁻ = A x + B u（没有控制量时 x⁻ = A x）
 *            2. 协方差预测   P⁻ = A P Aᵀ + Q
 *            3. 卡尔曼增益   S = H P⁻ Hᵀ + R，K = P⁻ Hᵀ S⁻¹
 *            4. 状态更新     y = z - H x⁻，x = x⁻ + K y
 *            5. 协方差更新   P = P⁻ - K H P⁻
 *          与旧代码的结构差异：
 *          - 五步各是一个公开函数。EKF 要替换某一步时，自己按顺序调用其余步骤，
 *            不再用旧代码的函数指针和“跳过第 N 步”标志；
 *          - 存储由调用者提供（静态数组），不再 malloc；
 *          - 调用者每次更新前直接写 z（和 u），滤波结果直接读 x；不再有“外部测量缓冲区”和输出副本。
 *
 *          用法：
 *          @code
 *          static float kf_storage[KALMAN_STORAGE_FLOATS(2, 1, 0)];
 *          static KalmanFilter kf;
 *          kalman_init(&kf, 2, 1, 0, kf_storage);   // 所有矩阵清零
 *          // 填 kf.A、kf.H、kf.Q、kf.R、kf.P 和初始 kf.x
 *          // 每个周期：写 kf.z.data[...]，调用 kalman_update(&kf)，读 kf.x.data[...]
 *          @endcode
 */
#pragma once

#include <stdint.h>

#include "algorithm/math/matrix.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define KALMAN_MAX2(a, b) ((a) > (b) ? (a) : (b))

/** n 维状态、m 维测量、u 维控制量所需的 float 个数（用来定义调用者提供的静态数组） */
#define KALMAN_STORAGE_FLOATS(n, m, u)                                                             \
    (2u * (n) + 2u * (m) + (u) + (n) * (u) + 5u * (n) * (n) + 3u * (n) * (m) + 3u * (m) * (m)      \
     + KALMAN_MAX2(n, m) * KALMAN_MAX2(n, m) + KALMAN_MAX2(n, m))

typedef struct
{
    uint8_t n; /* 状态维数 */
    uint8_t m; /* 测量维数 */
    uint8_t u; /* 控制量维数，0 表示没有 */

    /* 模型与输入：调用者填写 */
    Mat A;     /* n×n 状态转移 */
    Mat B;     /* n×u 控制矩阵（u 为 0 时不用） */
    Mat H;     /* m×n 测量矩阵 */
    Mat Q;     /* n×n 过程噪声协方差 */
    Mat R;     /* m×m 测量噪声协方差 */
    Mat z;     /* m×1 本次测量 */
    Mat u_vec; /* u×1 本次控制量 */

    /* 状态：x、P 需要调用者给初值 */
    Mat x;       /* n×1 后验估计（滤波结果） */
    Mat P;       /* n×n 后验协方差 */
    Mat x_minus; /* n×1 先验估计 */
    Mat P_minus; /* n×n 先验协方差 */
    Mat K;       /* n×m 卡尔曼增益 */
    Mat y;       /* m×1 新息 z - H x⁻（第 4 步算出，EKF 的卡方检验会用） */
    Mat S;       /* m×m H P⁻ Hᵀ + R（求逆时被破坏） */
    Mat S_inv;   /* m×m S 的逆（第 3 步算出） */

    /* 计算用的临时存储，各步骤之间不保留内容 */
    Mat tmp_a; /* max(n,m)² */
    Mat tmp_b; /* n×n */
    Mat Ht;    /* n×m */
    Mat vec;   /* max(n,m)×1 */
} KalmanFilter;

/**
 * @brief   把 storage 分给各矩阵并全部清零
 * @pre     n、m >= 1；storage 至少 KALMAN_STORAGE_FLOATS(n, m, u) 个 float
 */
void kalman_init(KalmanFilter *kf, uint8_t n, uint8_t m, uint8_t u, float *storage);

/** 按顺序执行下面五步 */
void kalman_update(KalmanFilter *kf);

void kalman_predict_state(KalmanFilter *kf);
void kalman_predict_cov(KalmanFilter *kf);
/** @pre R 正定（这样 S 一定可逆） */
void kalman_compute_gain(KalmanFilter *kf);
void kalman_update_state(KalmanFilter *kf);
void kalman_update_cov(KalmanFilter *kf);

#ifdef __cplusplus
}
#endif
