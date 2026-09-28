/**
 * @file    matrix.c
 * @brief   小矩阵运算，见 matrix.h
 */
#include "matrix.h"

#include <math.h>

void mat_init(Mat *m, uint8_t rows, uint8_t cols, float *data)
{
    m->rows = rows;
    m->cols = cols;
    m->data = data;
}

void mat_zero(Mat *m)
{
    const uint32_t count = (uint32_t)m->rows * m->cols;
    for (uint32_t i = 0u; i < count; i++)
    {
        m->data[i] = 0.0f;
    }
}

void mat_add(const Mat *a, const Mat *b, Mat *out)
{
    const uint32_t count = (uint32_t)a->rows * a->cols;
    out->rows = a->rows;
    out->cols = a->cols;
    for (uint32_t i = 0u; i < count; i++)
    {
        out->data[i] = a->data[i] + b->data[i];
    }
}

void mat_sub(const Mat *a, const Mat *b, Mat *out)
{
    const uint32_t count = (uint32_t)a->rows * a->cols;
    out->rows = a->rows;
    out->cols = a->cols;
    for (uint32_t i = 0u; i < count; i++)
    {
        out->data[i] = a->data[i] - b->data[i];
    }
}

void mat_mul(const Mat *a, const Mat *b, Mat *out)
{
    out->rows = a->rows;
    out->cols = b->cols;
    for (uint32_t r = 0u; r < a->rows; r++)
    {
        for (uint32_t c = 0u; c < b->cols; c++)
        {
            float sum = 0.0f;
            for (uint32_t k = 0u; k < a->cols; k++)
            {
                sum += a->data[r * a->cols + k] * b->data[k * b->cols + c];
            }
            out->data[r * out->cols + c] = sum;
        }
    }
}

void mat_trans(const Mat *a, Mat *out)
{
    out->rows = a->cols;
    out->cols = a->rows;
    for (uint32_t r = 0u; r < a->rows; r++)
    {
        for (uint32_t c = 0u; c < a->cols; c++)
        {
            out->data[c * out->cols + r] = a->data[r * a->cols + c];
        }
    }
}

static void swap_rows(Mat *m, uint32_t r1, uint32_t r2)
{
    for (uint32_t c = 0u; c < m->cols; c++)
    {
        const float t = m->data[r1 * m->cols + c];
        m->data[r1 * m->cols + c] = m->data[r2 * m->cols + c];
        m->data[r2 * m->cols + c] = t;
    }
}

bool mat_inv(Mat *a, Mat *out)
{
    const uint32_t n = a->rows;
    out->rows = a->rows;
    out->cols = a->cols;
    for (uint32_t i = 0u; i < n * n; i++)
    {
        out->data[i] = (i % (n + 1u) == 0u) ? 1.0f : 0.0f; /* 单位阵 */
    }

    /* 对 a 做行变换化成单位阵，同样的变换作用在 out 上，out 就成了 a⁻¹ */
    for (uint32_t col = 0u; col < n; col++)
    {
        /* 列主元：选这一列绝对值最大的行，减小舍入误差 */
        uint32_t pivot = col;
        for (uint32_t r = col + 1u; r < n; r++)
        {
            if (fabsf(a->data[r * n + col]) > fabsf(a->data[pivot * n + col]))
            {
                pivot = r;
            }
        }
        if (a->data[pivot * n + col] == 0.0f)
        {
            return false;
        }
        if (pivot != col)
        {
            swap_rows(a, pivot, col);
            swap_rows(out, pivot, col);
        }

        const float inv_pivot = 1.0f / a->data[col * n + col];
        for (uint32_t c = 0u; c < n; c++)
        {
            a->data[col * n + c] *= inv_pivot;
            out->data[col * n + c] *= inv_pivot;
        }

        for (uint32_t r = 0u; r < n; r++)
        {
            const float factor = a->data[r * n + col];
            if (r == col || factor == 0.0f)
            {
                continue;
            }
            for (uint32_t c = 0u; c < n; c++)
            {
                a->data[r * n + c] -= factor * a->data[col * n + c];
                out->data[r * n + c] -= factor * out->data[col * n + c];
            }
        }
    }
    return true;
}
