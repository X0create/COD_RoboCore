/**
 * @file    rls.h
 * @brief   带遗忘因子的递推最小二乘（RLS），单输出（纯计算，电脑上可测）
 * @note    模型 y = wᵀx，每来一组 (x, y) 更新一次估计 w：
 *            e = y − wᵀx，K = P x / (λ + xᵀP x)，w ← w + K e，P ← (P − K (P x)ᵀ) / λ。
 *          用途：底盘功率模型 P = τω + k1|ω| + k2τ² + k3/n 中在线辨识 k1、k2（《架构设计》功率控制）。
 *          COD-H7-Template 的 RLS.c 没有调用者且跑不起来（增益向量只分配 1 个 float 却按 2 个使用、
 *          λ 从未赋值为 0、1×1 乘 2×2），不照搬，按上面的标准公式重写（CHANGES）。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define RLS_MAX_N 4u

typedef struct
{
    uint8_t n;    /* 参数个数 */
    float lambda; /* 遗忘因子，0 < λ ≤ 1；越小越快跟上参数变化，噪声也越大 */
    float w[RLS_MAX_N];
    float p[RLS_MAX_N * RLS_MAX_N]; /* 按行存储，n×n */
} Rls;

/**
 * @param   p0  初始协方差（单位阵乘 p0），越大初期收敛越快
 * @param   w0  初始估计，n 个
 * @pre     1 ≤ n ≤ RLS_MAX_N，0 < lambda ≤ 1，p0 > 0
 */
void rls_init(Rls *rls, uint8_t n, float lambda, float p0, const float w0[]);

/** 更新一次，返回更新前的预测误差 y − wᵀx */
float rls_update(Rls *rls, const float x[], float y);

/** 按当前估计预测 wᵀx */
float rls_predict(const Rls *rls, const float x[]);

#ifdef __cplusplus
}
#endif
