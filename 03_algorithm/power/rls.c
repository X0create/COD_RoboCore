/**
 * @file    rls.c
 * @brief   递推最小二乘，见 rls.h
 */
#include "rls.h"

void rls_init(Rls *rls, uint8_t n, float lambda, float p0, const float w0[])
{
    rls->n = n;
    rls->lambda = lambda;
    for (uint32_t r = 0u; r < n; r++)
    {
        rls->w[r] = w0[r];
        for (uint32_t c = 0u; c < n; c++)
        {
            rls->p[r * n + c] = (r == c) ? p0 : 0.0f;
        }
    }
}

float rls_predict(const Rls *rls, const float x[])
{
    float y = 0.0f;
    for (uint32_t i = 0u; i < rls->n; i++)
    {
        y += rls->w[i] * x[i];
    }
    return y;
}

float rls_update(Rls *rls, const float x[], float y)
{
    const uint32_t n = rls->n;
    float px[RLS_MAX_N];
    float denom = rls->lambda;

    for (uint32_t r = 0u; r < n; r++)
    {
        px[r] = 0.0f;
        for (uint32_t c = 0u; c < n; c++)
        {
            px[r] += rls->p[r * n + c] * x[c];
        }
        denom += x[r] * px[r];
    }

    const float e = y - rls_predict(rls, x);
    for (uint32_t r = 0u; r < n; r++)
    {
        rls->w[r] += px[r] / denom * e; /* K = P x / denom */
    }
    /* P 对称，所以 xᵀP = (P x)ᵀ，P − K (P x)ᵀ = P − (P x)(P x)ᵀ / denom */
    for (uint32_t r = 0u; r < n; r++)
    {
        for (uint32_t c = 0u; c < n; c++)
        {
            rls->p[r * n + c] = (rls->p[r * n + c] - px[r] * px[c] / denom) / rls->lambda;
        }
    }
    return e;
}
