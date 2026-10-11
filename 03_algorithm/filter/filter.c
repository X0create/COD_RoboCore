/**
 * @file    filter.c
 * @brief   滤波：一阶 / 二阶低通、卡尔曼（说明见 filter.h）
 */
#include "filter.h"

/* ================================================================== */
/* 低通 lpf（说明见 filter.h 同名一节） */
/* ================================================================== */

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

/* ================================================================== */
/* 卡尔曼 kalman（说明见 filter.h 同名一节） */
/* ================================================================== */

/* 从 storage 里切出 rows×cols 个 float 给 m */
static void take(Matrix *m, uint8_t rows, uint8_t cols, float **storage)
{
    matrix_init(m, rows, cols, *storage);
    matrix_zero(m);
    *storage += (uint32_t)rows * cols;
}

/* 所有矩阵都从调用方给的一块 storage 里按顺序切出来：不用动态内存，大小由 KALMAN_STORAGE_FLOATS 算好 */
void kalman_init(KalmanFilter *kf, uint8_t n, uint8_t m, uint8_t u, float *storage)
{
    const uint8_t big = KALMAN_MAX2(n, m);
    kf->n = n;
    kf->m = m;
    kf->u = u;

    take(&kf->A, n, n, &storage);
    take(&kf->B, n, u, &storage);
    take(&kf->H, m, n, &storage);
    take(&kf->Q, n, n, &storage);
    take(&kf->R, m, m, &storage);
    take(&kf->z, m, 1u, &storage);
    take(&kf->u_vec, u, 1u, &storage);
    take(&kf->x, n, 1u, &storage);
    take(&kf->P, n, n, &storage);
    take(&kf->x_minus, n, 1u, &storage);
    take(&kf->P_minus, n, n, &storage);
    take(&kf->K, n, m, &storage);
    take(&kf->y, m, 1u, &storage);
    take(&kf->S, m, m, &storage);
    take(&kf->S_inv, m, m, &storage);
    take(&kf->tmp_a, big, big, &storage);
    take(&kf->tmp_b, n, n, &storage);
    take(&kf->Ht, n, m, &storage);
    take(&kf->vec, big, 1u, &storage);
}

/* 1. x⁻ = A x（+ B u） */
void kalman_predict_state(KalmanFilter *kf)
{
    matrix_mul(&kf->A, &kf->x, &kf->x_minus);
    if (kf->u > 0u)
    {
        matrix_mul(&kf->B, &kf->u_vec, &kf->vec);
        matrix_add(&kf->x_minus, &kf->vec, &kf->x_minus);
    }
}

/* 2. P⁻ = A P Aᵀ + Q */
void kalman_predict_cov(KalmanFilter *kf)
{
    matrix_mul(&kf->A, &kf->P, &kf->tmp_a);
    matrix_trans(&kf->A, &kf->tmp_b);
    matrix_mul(&kf->tmp_a, &kf->tmp_b, &kf->P_minus);
    matrix_add(&kf->P_minus, &kf->Q, &kf->P_minus);
}

/* 3. S = H P⁻ Hᵀ + R，K = P⁻ Hᵀ S⁻¹ */
void kalman_compute_gain(KalmanFilter *kf)
{
    matrix_trans(&kf->H, &kf->Ht);
    matrix_mul(&kf->H, &kf->P_minus, &kf->tmp_a);
    matrix_mul(&kf->tmp_a, &kf->Ht, &kf->S);
    matrix_add(&kf->S, &kf->R, &kf->S);
    /* R 正定时 S 正定、一定可逆（filter.h 卡尔曼一节的前提），不处理返回值 */
    (void)matrix_inv(&kf->S, &kf->S_inv);
    matrix_mul(&kf->P_minus, &kf->Ht, &kf->tmp_a);
    matrix_mul(&kf->tmp_a, &kf->S_inv, &kf->K);
}

/* 4. y = z − H x⁻（新息），x = x⁻ + K y */
void kalman_update_state(KalmanFilter *kf)
{
    matrix_mul(&kf->H, &kf->x_minus, &kf->vec);
    matrix_sub(&kf->z, &kf->vec, &kf->y);
    matrix_mul(&kf->K, &kf->y, &kf->vec);
    matrix_add(&kf->x_minus, &kf->vec, &kf->x);
}

/* 5. P = P⁻ − K H P⁻ = (I − K H) P⁻ */
void kalman_update_cov(KalmanFilter *kf)
{
    matrix_mul(&kf->K, &kf->H, &kf->tmp_a);
    matrix_mul(&kf->tmp_a, &kf->P_minus, &kf->tmp_b);
    matrix_sub(&kf->P_minus, &kf->tmp_b, &kf->P);
}

void kalman_update(KalmanFilter *kf)
{
    kalman_predict_state(kf);
    kalman_predict_cov(kf);
    kalman_compute_gain(kf);
    kalman_update_state(kf);
    kalman_update_cov(kf);
}
