/**
 * @file    test_mecanum.c
 * @brief   麦轮运动学的单元测试：前进、左移、原地转的已知轮速方向，正解 ∘ 逆解 = 恒等，
 *          四个轮速矛盾时正解取最小二乘
 */
#include "algorithm/kinematics/mecanum.h"

#include "unity.h"

static const MecanumConfig cfg = { .wheel_radius_m = 0.076f,
                                   .half_wheelbase_m = 0.2f,
                                   .half_track_m = 0.22f };

void setUp(void)
{
}

void tearDown(void)
{
}

static void check_wheels(const ChassisVel *v, const float expect[MECANUM_WHEELS])
{
    float w[MECANUM_WHEELS];
    mecanum_inverse(&cfg, v, w);
    for (unsigned i = 0u; i < MECANUM_WHEELS; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, expect[i], w[i]);
    }
}

/* 轮 0–3 = 左前、左后、右后、右前 */
static void test_forward_all_wheels_same(void)
{
    const float s = 1.0f / 0.076f;
    const float expect[MECANUM_WHEELS] = { s, s, s, s };
    check_wheels(&(ChassisVel){ .vx_m_s = 1.0f }, expect);
}

static void test_left_front_right_rear_reverse(void)
{
    const float s = 1.0f / 0.076f;
    const float expect[MECANUM_WHEELS] = { -s, s, -s, s };
    check_wheels(&(ChassisVel){ .vy_m_s = 1.0f }, expect);
}

static void test_ccw_left_backward_right_forward(void)
{
    const float s = (0.2f + 0.22f) / 0.076f;
    const float expect[MECANUM_WHEELS] = { -s, -s, s, s };
    check_wheels(&(ChassisVel){ .wz_rad_s = 1.0f }, expect);
}

static void test_forward_inverts_inverse(void)
{
    const ChassisVel cases[] = {
        { 1.0f, 0.0f, 0.0f },  { 0.0f, 1.0f, 0.0f },   { 0.0f, 0.0f, 1.0f },
        { 0.8f, -0.6f, 2.5f }, { -1.5f, 1.2f, -3.0f },
    };
    for (unsigned k = 0u; k < sizeof cases / sizeof cases[0]; k++)
    {
        float w[MECANUM_WHEELS];
        ChassisVel back;
        mecanum_inverse(&cfg, &cases[k], w);
        mecanum_forward(&cfg, w, &back);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, cases[k].vx_m_s, back.vx_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, cases[k].vy_m_s, back.vy_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, cases[k].wz_rad_s, back.wz_rad_s);
    }
}

/* 只有一个轮子在转：最小二乘残差与三个自由度方向（全 1、vy 符号、wz 符号）都正交 */
static void test_forward_least_squares_on_inconsistent_speeds(void)
{
    const float w[MECANUM_WHEELS] = { 0.0f, 0.0f, 8.0f, 0.0f };
    ChassisVel v;
    mecanum_forward(&cfg, w, &v);
    float fit[MECANUM_WHEELS];
    mecanum_inverse(&cfg, &v, fit);

    const float sign_vy[MECANUM_WHEELS] = { -1.0f, 1.0f, -1.0f, 1.0f };
    const float sign_wz[MECANUM_WHEELS] = { -1.0f, -1.0f, 1.0f, 1.0f };
    float dot_1 = 0.0f;
    float dot_vy = 0.0f;
    float dot_wz = 0.0f;
    for (unsigned i = 0u; i < MECANUM_WHEELS; i++)
    {
        const float r = w[i] - fit[i];
        dot_1 += r;
        dot_vy += sign_vy[i] * r;
        dot_wz += sign_wz[i] * r;
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, dot_1);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, dot_vy);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, dot_wz);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_forward_all_wheels_same);
    RUN_TEST(test_left_front_right_rear_reverse);
    RUN_TEST(test_ccw_left_backward_right_forward);
    RUN_TEST(test_forward_inverts_inverse);
    RUN_TEST(test_forward_least_squares_on_inconsistent_speeds);
    return UNITY_END();
}
