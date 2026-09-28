/**
 * @file    test_rls.c
 * @brief   rls 的单元测试：辨识已知线性模型、返回更新前的误差、遗忘因子跟踪参数变化、功率模型用法
 */
#include "algorithm/power/rls.h"

#include <math.h>

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static const float zero2[2] = { 0.0f, 0.0f };

/* 激励：两路不相关的正弦，保证参数可辨识 */
static void input(int k, float x[2])
{
    x[0] = sinf(0.3f * (float)k);
    x[1] = cosf(0.7f * (float)k) + 1.0f;
}

static void test_identifies_linear_model(void)
{
    Rls rls;
    rls_init(&rls, 2u, 1.0f, 1000.0f, zero2);
    for (int k = 0; k < 200; k++)
    {
        float x[2];
        input(k, x);
        (void)rls_update(&rls, x, 2.0f * x[0] - 0.5f * x[1]);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2.0f, rls.w[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.5f, rls.w[1]);
    const float x[2] = { 0.3f, 1.5f };
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.6f - 0.75f, rls_predict(&rls, x));
}

static void test_returns_prior_error(void)
{
    Rls rls;
    const float w0[2] = { 1.0f, 1.0f };
    rls_init(&rls, 2u, 1.0f, 10.0f, w0);
    const float x[2] = { 2.0f, 3.0f };
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 7.0f - 5.0f, rls_update(&rls, x, 7.0f));
}

/* 遗忘因子 0.98：参数中途从 (2, -0.5) 变成 (1, 1)，能跟上 */
static void test_forgetting_tracks_change(void)
{
    Rls rls;
    rls_init(&rls, 2u, 0.98f, 1000.0f, zero2);
    for (int k = 0; k < 1000; k++)
    {
        float x[2];
        input(k, x);
        const float y = (k < 500) ? 2.0f * x[0] - 0.5f * x[1] : x[0] + x[1];
        (void)rls_update(&rls, x, y);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 1.0f, rls.w[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 1.0f, rls.w[1]);
}

/*
 * 功率模型用法：P = τω + k1|ω| + k2τ² + k3/n，已知 τω 和 k3，
 * 令 x = [|ω|, τ²]、y = P − τω − k3/n，辨识 k1 = 0.05、k2 = 1.2
 */
static void test_power_model_usage(void)
{
    Rls rls;
    rls_init(&rls, 2u, 0.999f, 100.0f, zero2);
    const float k1 = 0.05f, k2 = 1.2f, k3_per_wheel = 1.0f;
    for (int k = 0; k < 2000; k++)
    {
        const float omega = 30.0f * sinf(0.01f * (float)k);
        const float tau = 1.5f * cosf(0.023f * (float)k);
        const float p = tau * omega + k1 * fabsf(omega) + k2 * tau * tau + k3_per_wheel;
        const float x[2] = { fabsf(omega), tau * tau };
        (void)rls_update(&rls, x, p - tau * omega - k3_per_wheel);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, k1, rls.w[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, k2, rls.w[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_identifies_linear_model);
    RUN_TEST(test_returns_prior_error);
    RUN_TEST(test_forgetting_tracks_change);
    RUN_TEST(test_power_model_usage);
    return UNITY_END();
}
