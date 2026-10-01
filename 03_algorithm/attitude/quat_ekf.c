/**
 * @file    quat_ekf.c
 * @brief   四元数扩展卡尔曼姿态解算，见 quat_ekf.h
 * @note    矩阵按行存储：6×6 的第 r 行第 c 列是下标 r * 6 + c；H 是 3×6。下标写法与旧代码一一对应，方便对照。
 */
#include "quat_ekf.h"
#include "03_algorithm/math/math_const.h"

#include <math.h>

#define N 6u
#define M 3u

#define GYRO_STILL_RAD_S 0.3f  /* 卡方“重新接受”的条件：陀螺近乎静止 */
#define GRAVITY_BAND     0.5f  /* 且加速度模长在重力 ±0.5 m/s² 内 */
#define CHI_REACCEPT     50u   /* 连续这么多次新息偏大才重新接受 */
#define BIAS_STEP_LIMIT  1e-2f /* 零偏修正每次限幅：±BIAS_STEP_LIMIT · dt */
#define P_BIAS_LIMIT     10000.0f

void quat_ekf_init(QuatEkf *ekf, float q_quat, float q_bias, float r_accel)
{
    ekf->q_quat = q_quat;
    ekf->q_bias = q_bias;
    ekf->r_accel = r_accel;
    ekf->chi_threshold = 1e-8f;
    ekf->gravity_m_s2 = 9.718f;
    ekf->chi_result = false;
    ekf->chi_count = 0u;

    kalman_init(&ekf->kf, N, M, 0u, ekf->storage);
    KalmanFilter *kf = &ekf->kf;
    kf->x.data[0] = 1.0f;

    /* 旧工程 INS_Task.c 的 QuaternionEKF_P_Data：对角 1e5（四元数）/ 100（零偏），其余 0.1 */
    for (uint32_t r = 0u; r < N; r++)
    {
        for (uint32_t c = 0u; c < N; c++)
        {
            kf->P.data[r * N + c] = (r != c) ? 0.1f : ((r < 4u) ? 100000.0f : 100.0f);
        }
    }

    ekf->q[0] = 1.0f;
    ekf->q[1] = ekf->q[2] = ekf->q[3] = 0.0f;
    ekf->gyro_bias[0] = ekf->gyro_bias[1] = ekf->gyro_bias[2] = 0.0f;
}

static float clampf(float x, float limit)
{
    return (x > limit) ? limit : ((x < -limit) ? -limit : x);
}

/* 状态转移矩阵的陀螺部分（对 q 的偏导），零偏列在预测之后再填（旧代码 QuaternionEKF_Update） */
static void set_a_gyro(float *a, const float half[3])
{
    for (uint32_t i = 0u; i < N * N; i++)
    {
        a[i] = (i % (N + 1u) == 0u) ? 1.0f : 0.0f;
    }
    a[1] = -half[0];
    a[2] = -half[1];
    a[3] = -half[2];
    a[6] = half[0];
    a[8] = half[2];
    a[9] = -half[1];
    a[12] = half[1];
    a[13] = -half[2];
    a[15] = half[0];
    a[18] = half[2];
    a[19] = half[1];
    a[20] = -half[0];
}

/* 预测后：先验四元数归一化，填零偏列，限制零偏协方差（旧代码 QuaternionEKF_A_Update） */
static void after_predict_state(KalmanFilter *kf, float dt)
{
    float *xm = kf->x_minus.data;
    float *a = kf->A.data;
    const float inv = 1.0f / sqrtf(xm[0] * xm[0] + xm[1] * xm[1] + xm[2] * xm[2] + xm[3] * xm[3]);
    for (int i = 0; i < 4; i++)
    {
        xm[i] *= inv;
    }
    a[4] = 0.5f * xm[1] * dt;
    a[5] = 0.5f * xm[2] * dt;
    a[10] = -0.5f * xm[0] * dt;
    a[11] = 0.5f * xm[3] * dt;
    a[16] = -0.5f * xm[3] * dt;
    a[17] = -0.5f * xm[0] * dt;
    a[22] = 0.5f * xm[2] * dt;
    a[23] = -0.5f * xm[1] * dt;
    kf->P.data[28] = clampf(kf->P.data[28], P_BIAS_LIMIT);
    kf->P.data[35] = clampf(kf->P.data[35], P_BIAS_LIMIT);
}

/* 测量雅可比：重力方向对 q 的偏导（旧代码 QuaternionEKF_H_Update） */
static void set_h(KalmanFilter *kf)
{
    const float *q = kf->x_minus.data;
    float *h = kf->H.data;
    for (uint32_t i = 0u; i < M * N; i++)
    {
        h[i] = 0.0f;
    }
    h[0] = -2.0f * q[2];
    h[1] = 2.0f * q[3];
    h[2] = -2.0f * q[0];
    h[3] = 2.0f * q[1];
    h[6] = 2.0f * q[1];
    h[7] = 2.0f * q[0];
    h[8] = 2.0f * q[3];
    h[9] = 2.0f * q[2];
    h[12] = 2.0f * q[0];
    h[13] = -2.0f * q[1];
    h[14] = -2.0f * q[2];
    h[15] = 2.0f * q[3];
}

/*
 * 卡方检验（旧代码 QuaternionEKF_ChiSqrtTest，公式改正为 rᵀS⁻¹r）。
 * 返回 true 表示跳过本次修正；gain 输出增益系数（新息越小增益越大）。
 */
static bool chi_square_skip(QuatEkf *ekf, bool still, float *gain)
{
    const KalmanFilter *kf = &ekf->kf;
    const float *s_inv = kf->S_inv.data;
    const float *y = kf->y.data;
    const float thr = ekf->chi_threshold;

    float chi = 0.0f;
    for (uint32_t r = 0u; r < M; r++)
    {
        for (uint32_t c = 0u; c < M; c++)
        {
            chi += y[r] * s_inv[r * M + c] * y[c];
        }
    }

    *gain = 1.0f;
    if (chi < 0.5f * thr)
    {
        ekf->chi_result = true;
    }
    if (chi > thr && ekf->chi_result)
    {
        ekf->chi_count = still ? (uint8_t)(ekf->chi_count + 1u) : 0u;
        if (ekf->chi_count > CHI_REACCEPT)
        {
            ekf->chi_result = false; /* 长时间都“偏大”：认为是估计错了，重新接受加速度修正 */
            return false;
        }
        return true;
    }
    if (chi > 0.1f * thr && ekf->chi_result)
    {
        *gain = (thr - chi) / (0.9f * thr);
    }
    ekf->chi_count = 0u;
    return false;
}

void quat_ekf_update(QuatEkf *ekf, const float gyro_rad_s[3], const float accel_m_s2[3], float dt_s)
{
    KalmanFilter *kf = &ekf->kf;

    /* 1. 输入：减去估计的零偏（z 不估计），模长用于卡方的静止判断 */
    float g[3];
    g[0] = gyro_rad_s[0] - ekf->gyro_bias[0];
    g[1] = gyro_rad_s[1] - ekf->gyro_bias[1];
    g[2] = gyro_rad_s[2];
    const float gyro_norm = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    const float half[3] = { 0.5f * g[0] * dt_s, 0.5f * g[1] * dt_s, 0.5f * g[2] * dt_s };
    const float accel_norm = sqrtf(accel_m_s2[0] * accel_m_s2[0] + accel_m_s2[1] * accel_m_s2[1]
                                   + accel_m_s2[2] * accel_m_s2[2]);
    for (int i = 0; i < 3; i++)
    {
        kf->z.data[i] = accel_m_s2[i] / accel_norm;
    }
    const bool still = gyro_norm < GYRO_STILL_RAD_S && accel_norm > ekf->gravity_m_s2 - GRAVITY_BAND
                       && accel_norm < ekf->gravity_m_s2 + GRAVITY_BAND;

    /* 2. 噪声矩阵 */
    for (uint32_t i = 0u; i < 4u; i++)
    {
        kf->Q.data[i * (N + 1u)] = ekf->q_quat * dt_s;
    }
    kf->Q.data[28] = ekf->q_bias * dt_s;
    kf->Q.data[35] = ekf->q_bias * dt_s;
    for (uint32_t i = 0u; i < M; i++)
    {
        kf->R.data[i * (M + 1u)] = ekf->r_accel;
    }

    /* 3. 预测 */
    set_a_gyro(kf->A.data, half);
    kalman_predict_state(kf);
    after_predict_state(kf, dt_s);
    kalman_predict_cov(kf);

    /* 4. 增益与新息：新息用非线性的预测重力方向 h(x⁻)，不是 H·x⁻ */
    set_h(kf);
    kalman_compute_gain(kf);
    const float *xm = kf->x_minus.data;
    const float h[3] = {
        2.0f * (xm[1] * xm[3] - xm[0] * xm[2]),
        2.0f * (xm[0] * xm[1] + xm[2] * xm[3]),
        xm[0] * xm[0] - xm[1] * xm[1] - xm[2] * xm[2] + xm[3] * xm[3],
    };
    for (int i = 0; i < 3; i++)
    {
        kf->y.data[i] = kf->z.data[i] - h[i];
    }

    /* 5. 卡方检验：跳过时后验 = 先验 */
    float gain;
    if (chi_square_skip(ekf, still, &gain))
    {
        for (uint32_t i = 0u; i < N; i++)
        {
            kf->x.data[i] = kf->x_minus.data[i];
        }
        for (uint32_t i = 0u; i < N * N; i++)
        {
            kf->P.data[i] = kf->P_minus.data[i];
        }
    }
    else
    {
        /* 6. 增益调整：整体乘 gain；零偏两行再乘“该轴与重力方向的夹角 / (π/2)”（轴接近竖直时看不出零偏） */
        float *k = kf->K.data;
        for (uint32_t i = 0u; i < N * M; i++)
        {
            k[i] *= gain;
        }
        for (uint32_t r = 4u; r < 6u; r++)
        {
            const float angle = acosf(fabsf(h[r - 4u]));
            for (uint32_t c = 0u; c < M; c++)
            {
                k[r * M + c] *= angle / RM_HALF_PI;
            }
        }

        /* 7. 后验：x = x⁻ + K·y；零偏修正限幅；四元数 z 分量不修正 */
        mat_mul(&kf->K, &kf->y, &kf->vec);
        float *corr = kf->vec.data;
        if (ekf->chi_result)
        {
            corr[4] = clampf(corr[4], BIAS_STEP_LIMIT * dt_s);
            corr[5] = clampf(corr[5], BIAS_STEP_LIMIT * dt_s);
        }
        corr[3] = 0.0f;
        for (uint32_t i = 0u; i < N; i++)
        {
            kf->x.data[i] = kf->x_minus.data[i] + corr[i];
        }
        kalman_update_cov(kf);
    }

    /* 8. 输出 */
    const float *x = kf->x.data;
    const float inv = 1.0f / sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2] + x[3] * x[3]);
    for (int i = 0; i < 4; i++)
    {
        ekf->q[i] = x[i] * inv;
    }
    ekf->gyro_bias[0] = x[4];
    ekf->gyro_bias[1] = x[5];
    ekf->gyro_bias[2] = 0.0f;
}

void quat_to_euler(const float q[4], float *yaw_rad, float *pitch_rad, float *roll_rad)
{
    float s = 2.0f * (q[0] * q[2] - q[1] * q[3]);
    s = (s > 1.0f) ? 1.0f : ((s < -1.0f) ? -1.0f : s); /* 舍入可能略超 ±1，asinf 会得 NaN */
    *yaw_rad =
        atan2f(2.0f * (q[0] * q[3] + q[1] * q[2]), 1.0f - 2.0f * (q[2] * q[2] + q[3] * q[3]));
    *pitch_rad = asinf(s);
    *roll_rad =
        atan2f(2.0f * (q[0] * q[1] + q[2] * q[3]), 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]));
}
