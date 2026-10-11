/**
 * @file    test_half_steer.c
 * @brief   半舵半全向运动学的单元测试：哪条对角线是舵轮、平移、原地转、正解 ∘ 逆解 = 恒等（两种对角线）
 */
#include "03_algorithm/kinematics/half_steer.h"

#include "03_algorithm/math/math_const.h"

#include <math.h>

#include "unity.h"

/* 长方形底盘：前后 0.4 m、左右 0.3 m，舵轮半径 0.06 m、全向轮半径 0.076 m */
static const HalfSteerConfig lf_rb = { .steer_diagonal = HALF_STEER_LF_RB,
                                       .steer_radius_m = 0.06f,
                                       .omni_radius_m = 0.076f,
                                       .half_wheelbase_m = 0.2f,
                                       .half_track_m = 0.15f };
static const float zero_heading[HALF_STEER_WHEELS] = { 0.0f, 0.0f, 0.0f, 0.0f };

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_which_wheels_are_steer(void)
{
    HalfSteerConfig other = lf_rb;
    other.steer_diagonal = HALF_STEER_LB_RF;
    for (unsigned i = 0u; i < HALF_STEER_WHEELS; i++)
    {
        TEST_ASSERT_EQUAL(i % 2u == 0u, half_steer_is_steer(&lf_rb, i));
        TEST_ASSERT_EQUAL(i % 2u == 1u, half_steer_is_steer(&other, i));
    }
}

/* 向前平移：舵轮朝前、转速 v/r；全向轮只走切向分量 −y·v/R */
static void test_forward_translation(void)
{
    SteerWheel w[HALF_STEER_WHEELS];
    half_steer_inverse(&lf_rb, &(ChassisVel){ .vx_m_s = 0.5f }, zero_heading, w);
    const float r = sqrtf(0.2f * 0.2f + 0.15f * 0.15f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, w[0].heading_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f / 0.06f, w[0].speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f / 0.06f, w[2].speed_rad_s);
    /* 左后 (−0.2, 0.15)：−y·vx/R = −0.15·0.5/R；右前 (0.2, −0.15)：+0.15·0.5/R */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.15f * 0.5f / r / 0.076f, w[1].speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.15f * 0.5f / r / 0.076f, w[3].speed_rad_s);
}

/* 原地逆时针转：全向轮转速 = R·wz / r；舵轮沿切向（左前切向 (−0.15, 0.2) 超过 90°，所以反转轮子） */
static void test_rotation(void)
{
    SteerWheel w[HALF_STEER_WHEELS];
    half_steer_inverse(&lf_rb, &(ChassisVel){ .wz_rad_s = 2.0f }, zero_heading, w);
    const float r = sqrtf(0.2f * 0.2f + 0.15f * 0.15f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, r * 2.0f / 0.076f, w[1].speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, r * 2.0f / 0.076f, w[3].speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, atan2f(0.2f, -0.15f) - RM_PI, w[0].heading_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -r * 2.0f / 0.06f, w[0].speed_rad_s);
}

static void check_round_trip(const HalfSteerConfig *cfg, ChassisVel v)
{
    SteerWheel w[HALF_STEER_WHEELS];
    ChassisVel back;
    half_steer_inverse(cfg, &v, zero_heading, w);
    half_steer_forward(cfg, w, &back);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, v.vx_m_s, back.vx_m_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, v.vy_m_s, back.vy_m_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, v.wz_rad_s, back.wz_rad_s);
}

static void test_round_trip_both_diagonals(void)
{
    HalfSteerConfig other = lf_rb;
    other.steer_diagonal = HALF_STEER_LB_RF;
    const ChassisVel cases[] = { { 0.5f, 0.0f, 0.0f },
                                 { 0.0f, -0.4f, 0.0f },
                                 { 0.0f, 0.0f, 3.0f },
                                 { 0.3f, -0.2f, 1.5f },
                                 { -1.0f, 0.7f, -2.0f } };
    for (unsigned k = 0u; k < sizeof(cases) / sizeof(cases[0]); k++)
    {
        check_round_trip(&lf_rb, cases[k]);
        check_round_trip(&other, cases[k]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_which_wheels_are_steer);
    RUN_TEST(test_forward_translation);
    RUN_TEST(test_rotation);
    RUN_TEST(test_round_trip_both_diagonals);
    return UNITY_END();
}
