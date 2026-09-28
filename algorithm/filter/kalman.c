/**
 * @file    kalman.c
 * @brief   线性卡尔曼滤波，见 kalman.h
 */
#include "kalman.h"

/* 从 storage 里切出 rows×cols 个 float 给 m */
static void take(Mat *m, uint8_t rows, uint8_t cols, float **storage)
{
    mat_init(m, rows, cols, *storage);
    mat_zero(m);
    *storage += (uint32_t)rows * cols;
}

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

void kalman_predict_state(KalmanFilter *kf)
{
    mat_mul(&kf->A, &kf->x, &kf->x_minus);
    if (kf->u > 0u)
    {
        mat_mul(&kf->B, &kf->u_vec, &kf->vec);
        mat_add(&kf->x_minus, &kf->vec, &kf->x_minus);
    }
}

void kalman_predict_cov(KalmanFilter *kf)
{
    mat_mul(&kf->A, &kf->P, &kf->tmp_a);
    mat_trans(&kf->A, &kf->tmp_b);
    mat_mul(&kf->tmp_a, &kf->tmp_b, &kf->P_minus);
    mat_add(&kf->P_minus, &kf->Q, &kf->P_minus);
}

void kalman_compute_gain(KalmanFilter *kf)
{
    mat_trans(&kf->H, &kf->Ht);
    mat_mul(&kf->H, &kf->P_minus, &kf->tmp_a);
    mat_mul(&kf->tmp_a, &kf->Ht, &kf->S);
    mat_add(&kf->S, &kf->R, &kf->S);
    /* R 正定时 S 正定、一定可逆（kalman.h 的前提），不处理返回值 */
    (void)mat_inv(&kf->S, &kf->S_inv);
    mat_mul(&kf->P_minus, &kf->Ht, &kf->tmp_a);
    mat_mul(&kf->tmp_a, &kf->S_inv, &kf->K);
}

void kalman_update_state(KalmanFilter *kf)
{
    mat_mul(&kf->H, &kf->x_minus, &kf->vec);
    mat_sub(&kf->z, &kf->vec, &kf->y);
    mat_mul(&kf->K, &kf->y, &kf->vec);
    mat_add(&kf->x_minus, &kf->vec, &kf->x);
}

void kalman_update_cov(KalmanFilter *kf)
{
    mat_mul(&kf->K, &kf->H, &kf->tmp_a);
    mat_mul(&kf->tmp_a, &kf->P_minus, &kf->tmp_b);
    mat_sub(&kf->P_minus, &kf->tmp_b, &kf->P);
}

void kalman_update(KalmanFilter *kf)
{
    kalman_predict_state(kf);
    kalman_predict_cov(kf);
    kalman_compute_gain(kf);
    kalman_update_state(kf);
    kalman_update_cov(kf);
}
