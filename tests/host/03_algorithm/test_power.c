/**
 * @file    test_power.c
 * @brief   power 的单元测试，按节：RLS（辨识已知线性模型、返回更新前的误差、遗忘因子跟踪参数变化）、
 *          功率模型（在线辨识 k1、k2）、功率分配（不超限不改、超限压到上限、回馈不压、离线为 0、只压不放大、无实根、不反向）
 */
#include "03_algorithm/power/power.h"

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

/* ---- 功率模型：4 台电机、已知 k1 = 0.05、k2 = 1.2、k3 = 4 W，用“实测”总功率在线辨识 ---- */
static void test_identify_k1_k2_from_measured_power(void)
{
    const PowerModel truth = { .k1 = 0.05f, .k2 = 1.2f, .k3 = 4.0f };
    PowerModel model = { .k1 = 0.2f, .k2 = 0.5f, .k3 = 4.0f }; /* 初值故意给偏 */
    Rls rls;
    const float w0[2] = { model.k1, model.k2 };
    rls_init(&rls, 2u, 0.999f, 100.0f, w0);
    for (int k = 0; k < 3000; k++)
    {
        float tau[4], omega[4], measured = 0.0f;
        for (unsigned i = 0u; i < 4u; i++)
        {
            omega[i] = 30.0f * sinf(0.01f * (float)k + (float)i);
            tau[i] = 1.5f * cosf(0.023f * (float)k + 0.5f * (float)i);
            measured += power_model_motor(&truth, tau[i], omega[i], 4u);
        }
        power_model_identify(&rls, &model, tau, omega, 4u, measured);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, truth.k1, model.k1);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, truth.k2, model.k2);
}

/* ---- 功率分配 ---- */

static const PowerModel model = { .k1 = 0.1f, .k2 = 2.0f, .k3 = 4.0f };
static const PowerAllocConfig alloc = { .err_low_rad_s = 5.0f, .err_high_rad_s = 20.0f };

static float total_power(const PowerMotor m[], const float torque[], unsigned n)
{
    float sum = 0.0f;
    for (unsigned i = 0u; i < n; i++)
    {
        if (m[i].online)
        {
            sum += power_model_motor(&model, torque[i], m[i].speed_rad_s, n);
        }
    }
    return sum;
}

static void test_below_limit_unchanged(void)
{
    const PowerMotor m[4] = { { true, 0.5f, 10.0f, 1.0f },
                              { true, 0.5f, 10.0f, 1.0f },
                              { true, 0.5f, 10.0f, 1.0f },
                              { true, 0.5f, 10.0f, 1.0f } };
    float out[4];
    const float cmd = power_limit(&model, &alloc, m, 4u, 1000.0f, out);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, total_power(m, (const float[]){ 0.5f, 0.5f, 0.5f, 0.5f }, 4u),
                             cmd);
    for (unsigned i = 0u; i < 4u; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(0.5f, out[i]);
    }
}

/* 超上限、误差小（按指令功率比例分）：压缩后模型总功率等于上限，方向不变、大小不超过原指令 */
static void test_over_limit_scaled_to_max(void)
{
    const PowerMotor m[4] = { { true, 3.0f, 20.0f, 1.0f },
                              { true, 2.0f, 15.0f, 1.0f },
                              { true, -3.0f, -20.0f, 1.0f },
                              { true, -2.0f, -15.0f, 1.0f } };
    float out[4];
    const float cmd = power_limit(&model, &alloc, m, 4u, 80.0f, out);
    TEST_ASSERT_TRUE(cmd > 80.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 80.0f, total_power(m, out, 4u));
    for (unsigned i = 0u; i < 4u; i++)
    {
        TEST_ASSERT_TRUE(out[i] * m[i].torque_nm > 0.0f);
        TEST_ASSERT_TRUE(fabsf(out[i]) <= fabsf(m[i].torque_nm));
    }
}

/* 正在回馈的电机（τω < 0、模型功率为负）不压缩，它回馈的功率分给别的电机；离线的输出 0 */
static void test_regenerating_kept_and_offline_zero(void)
{
    const PowerMotor m[3] = { { true, 3.0f, 20.0f, 1.0f },
                              { true, -2.0f, 20.0f, 1.0f }, /* 在制动：回馈 */
                              { false, 3.0f, 20.0f, 1.0f } };
    float out[3];
    (void)power_limit(&model, &alloc, m, 3u, 20.0f, out);
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, out[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out[2]);
    TEST_ASSERT_TRUE(out[0] > 0.0f && out[0] < 3.0f);
    /* 能分给 0 号电机的 = 上限 + 回馈的功率 */
    const float regen = -power_model_motor(&model, -2.0f, 20.0f, 3u);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 20.0f + regen, power_model_motor(&model, out[0], 20.0f, 3u));
}

/* 误差大（按误差比例分）：误差大的电机分到更多功率；分到的超过原指令时用原指令（只压不放大） */
static void test_error_weighting_and_no_amplify(void)
{
    const PowerMotor m[2] = { { true, 0.2f, 20.0f, 40.0f }, /* 误差大，但原指令小 */
                              { true, 3.0f, 20.0f, 1.0f } };
    float out[2];
    (void)power_limit(&model, &alloc, m, 2u, 30.0f, out);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, out[0]); /* 分到的功率比它要的多：不放大 */
    TEST_ASSERT_TRUE(out[1] < 3.0f);
}

/* 无实根（分到的比功率最小点还少）：取 τ = −ω / (2k2)；与原指令同号时用它 */
static void test_no_real_root_takes_min_power_torque(void)
{
    const PowerMotor m[1] = { { true, -3.0f, 1.0f, 1.0f } };
    float out[1];
    (void)power_limit(&model, &alloc, m, 1u, 0.0f, out);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.0f / (2.0f * model.k2), out[0]);
}

/* 分到的功率比空转损耗还小：解出的是反向制动力矩，不反向，输出 0 */
static void test_never_reverses_command(void)
{
    const PowerMotor a[1] = { { true, 3.0f, 20.0f, 1.0f } }; /* 有实根，但根为负 */
    const PowerMotor b[1] = { { true, 3.0f, 1.0f, 1.0f } };  /* 无实根，−ω/(2k2) 为负 */
    float out[1];
    (void)power_limit(&model, &alloc, a, 1u, 0.0f, out);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out[0]);
    (void)power_limit(&model, &alloc, b, 1u, 0.0f, out);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out[0]);
}

static void test_steer_budget(void)
{
    TEST_ASSERT_EQUAL_FLOAT(30.0f, power_steer_budget(30.0f, 100.0f));
    TEST_ASSERT_EQUAL_FLOAT(80.0f, power_steer_budget(95.0f, 100.0f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_identifies_linear_model);
    RUN_TEST(test_returns_prior_error);
    RUN_TEST(test_forgetting_tracks_change);
    RUN_TEST(test_identify_k1_k2_from_measured_power);
    RUN_TEST(test_below_limit_unchanged);
    RUN_TEST(test_over_limit_scaled_to_max);
    RUN_TEST(test_regenerating_kept_and_offline_zero);
    RUN_TEST(test_error_weighting_and_no_amplify);
    RUN_TEST(test_no_real_root_takes_min_power_torque);
    RUN_TEST(test_never_reverses_command);
    RUN_TEST(test_steer_budget);
    return UNITY_END();
}
