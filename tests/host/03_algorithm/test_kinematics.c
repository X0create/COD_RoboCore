/**
 * @file    test_kinematics.c
 * @brief   kinematics 的单元测试，按节：omni、mecanum、steer、half_steer
 */
#include "03_algorithm/kinematics/kinematics.h"

#include <math.h>

#include "03_algorithm/math/rm_math.h"

#include "unity.h"

/* ---- omni：全向轮运动学的单元测试：X 形布置的已知值、纯旋转、正解 ∘ 逆解 = 恒等（两种布置）、四个轮速矛盾时正解取最小二乘 ---- */

static const OmniConfig x_layout = { .wheel_radius_m = 0.08f,
                                     .center_dist_m = 0.25f,
                                     .first_wheel_rad = RM_PI / 4.0f };
static const OmniConfig plus_layout = { .wheel_radius_m = 0.06f,
                                        .center_dist_m = 0.3f,
                                        .first_wheel_rad = 0.0f };

/* X 形：轮 0–3 = 左前、左后、右后、右前。向前走时左侧两轮顺时针（负）、右侧两轮逆时针（正） */
static void test_omni_x_layout_forward_motion(void)
{
    Omni o;
    omni_init(&o, &x_layout);
    const ChassisVel v = { .vx_m_s = 1.0f };
    float w[OMNI_WHEELS];
    omni_inverse(&o, &v, w);

    const float expect = 0.70710678f / 0.08f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -expect, w[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -expect, w[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, expect, w[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, expect, w[3]);
}

/* 向左走：前两轮（左前、右前）一个方向，后两轮另一个方向 */
static void test_omni_x_layout_left_motion(void)
{
    Omni o;
    omni_init(&o, &x_layout);
    const ChassisVel v = { .vy_m_s = 1.0f };
    float w[OMNI_WHEELS];
    omni_inverse(&o, &v, w);

    const float expect = 0.70710678f / 0.08f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, expect, w[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -expect, w[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -expect, w[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, expect, w[3]);
}

/* 原地逆时针转：四个轮子同向同速，线速度 = R · wz */
static void test_omni_pure_rotation(void)
{
    Omni o;
    omni_init(&o, &x_layout);
    const ChassisVel v = { .wz_rad_s = 2.0f };
    float w[OMNI_WHEELS];
    omni_inverse(&o, &v, w);
    for (unsigned i = 0u; i < OMNI_WHEELS; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.25f * 2.0f / 0.08f, w[i]);
    }
}

static void omni_check_round_trip(const OmniConfig *cfg)
{
    Omni o;
    omni_init(&o, cfg);
    const ChassisVel cases[] = {
        { 1.0f, 0.0f, 0.0f },  { 0.0f, -1.5f, 0.0f },  { 0.0f, 0.0f, 3.0f },
        { 0.7f, -0.4f, 1.2f }, { -2.0f, 1.1f, -4.5f },
    };
    for (unsigned k = 0u; k < sizeof cases / sizeof cases[0]; k++)
    {
        float w[OMNI_WHEELS];
        ChassisVel back;
        omni_inverse(&o, &cases[k], w);
        omni_forward(&o, w, &back);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, cases[k].vx_m_s, back.vx_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, cases[k].vy_m_s, back.vy_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, cases[k].wz_rad_s, back.wz_rad_s);
    }
}

static void test_omni_forward_inverts_inverse_x_layout(void)
{
    omni_check_round_trip(&x_layout);
}

static void test_omni_forward_inverts_inverse_plus_layout(void)
{
    omni_check_round_trip(&plus_layout);
}

/* 四个轮速互相矛盾（只有一个轮子在转，相当于其余三个打滑）：正解是最小二乘解，再逆解回去与原轮速的
 * 残差和“可实现的轮速组合”正交——这里用结果验证：残差在每个自由度方向上的投影为 0 */
static void test_omni_forward_least_squares_on_inconsistent_speeds(void)
{
    Omni o;
    omni_init(&o, &x_layout);
    const float w[OMNI_WHEELS] = { 10.0f, 0.0f, 0.0f, 0.0f };
    ChassisVel v;
    omni_forward(&o, w, &v);
    float fit[OMNI_WHEELS];
    omni_inverse(&o, &v, fit);

    float dot_x = 0.0f;
    float dot_y = 0.0f;
    float dot_w = 0.0f;
    for (unsigned i = 0u; i < OMNI_WHEELS; i++)
    {
        const float r = w[i] - fit[i];
        dot_x += -o.sin_theta[i] * r;
        dot_y += o.cos_theta[i] * r;
        dot_w += r;
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, dot_x);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, dot_y);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, dot_w);
}

/* ---- mecanum：麦轮运动学的单元测试：前进、左移、原地转的已知轮速方向，正解 ∘ 逆解 = 恒等，四个轮速矛盾时正解取最小二乘 ---- */

static const MecanumConfig mecanum_cfg = { .wheel_radius_m = 0.076f,
                                           .half_wheelbase_m = 0.2f,
                                           .half_track_m = 0.22f };

static void check_wheels(const ChassisVel *v, const float expect[MECANUM_WHEELS])
{
    float w[MECANUM_WHEELS];
    mecanum_inverse(&mecanum_cfg, v, w);
    for (unsigned i = 0u; i < MECANUM_WHEELS; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, expect[i], w[i]);
    }
}

/* 轮 0–3 = 左前、左后、右后、右前 */
static void test_mecanum_forward_all_wheels_same(void)
{
    const float s = 1.0f / 0.076f;
    const float expect[MECANUM_WHEELS] = { s, s, s, s };
    check_wheels(&(ChassisVel){ .vx_m_s = 1.0f }, expect);
}

static void test_mecanum_left_front_right_rear_reverse(void)
{
    const float s = 1.0f / 0.076f;
    const float expect[MECANUM_WHEELS] = { -s, s, -s, s };
    check_wheels(&(ChassisVel){ .vy_m_s = 1.0f }, expect);
}

static void test_mecanum_ccw_left_backward_right_forward(void)
{
    const float s = (0.2f + 0.22f) / 0.076f;
    const float expect[MECANUM_WHEELS] = { -s, -s, s, s };
    check_wheels(&(ChassisVel){ .wz_rad_s = 1.0f }, expect);
}

static void test_mecanum_forward_inverts_inverse(void)
{
    const ChassisVel cases[] = {
        { 1.0f, 0.0f, 0.0f },  { 0.0f, 1.0f, 0.0f },   { 0.0f, 0.0f, 1.0f },
        { 0.8f, -0.6f, 2.5f }, { -1.5f, 1.2f, -3.0f },
    };
    for (unsigned k = 0u; k < sizeof cases / sizeof cases[0]; k++)
    {
        float w[MECANUM_WHEELS];
        ChassisVel back;
        mecanum_inverse(&mecanum_cfg, &cases[k], w);
        mecanum_forward(&mecanum_cfg, w, &back);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, cases[k].vx_m_s, back.vx_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, cases[k].vy_m_s, back.vy_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, cases[k].wz_rad_s, back.wz_rad_s);
    }
}

/* 只有一个轮子在转：最小二乘残差与三个自由度方向（全 1、vy 符号、wz 符号）都正交 */
static void test_mecanum_forward_least_squares_on_inconsistent_speeds(void)
{
    const float w[MECANUM_WHEELS] = { 0.0f, 0.0f, 8.0f, 0.0f };
    ChassisVel v;
    mecanum_forward(&mecanum_cfg, w, &v);
    float fit[MECANUM_WHEELS];
    mecanum_inverse(&mecanum_cfg, &v, fit);

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

/* ---- steer：舵轮运动学的单元测试：平移时四轮同向同速、原地转时各轮沿切向、超过 90° 时反转轮子换另一侧、目标朝向在 ±π 附近连续、速度为 0 时保持朝向、正解 ∘ 逆解 = 恒等 ---- */

static const SteerConfig steer_cfg = { .wheel_radius_m = 0.06f,
                                       .half_wheelbase_m = 0.2f,
                                       .half_track_m = 0.2f };
static const float steer_zero_heading[STEER_WHEELS] = { 0.0f, 0.0f, 0.0f, 0.0f };

static void test_steer_translation_all_wheels_same(void)
{
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&steer_cfg, &(ChassisVel){ .vx_m_s = 0.3f, .vy_m_s = 0.3f }, steer_zero_heading,
                  w);
    for (unsigned i = 0u; i < STEER_WHEELS; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, RM_PI / 4.0f, w[i].heading_rad);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, sqrtf(0.18f) / 0.06f, w[i].speed_rad_s);
    }
}

/* 原地逆时针转：左前轮 (0.2, 0.2) 的速度方向是 135°，离当前 0° 超过 90°，所以朝向 −45°、轮子反转 */
static void test_steer_rotation_tangential_and_flipped(void)
{
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&steer_cfg, &(ChassisVel){ .wz_rad_s = 1.0f }, steer_zero_heading, w);
    const float speed = sqrtf(0.08f) / 0.06f;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -RM_PI / 4.0f, w[0].heading_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -speed, w[0].speed_rad_s);
    /* 右前轮 (0.2, −0.2)：速度方向 45°，不用翻转 */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, RM_PI / 4.0f, w[3].heading_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, speed, w[3].speed_rad_s);
}

/* 当前朝向 170°，要去 −170°：只转 20°，结果是连续的 190°，不绕一大圈 */
static void test_steer_target_heading_is_continuous_across_pi(void)
{
    const float deg = RM_PI / 180.0f;
    const float heading[STEER_WHEELS] = { 170.0f * deg, 170.0f * deg, 170.0f * deg, 170.0f * deg };
    const ChassisVel v = { .vx_m_s = cosf(-170.0f * deg), .vy_m_s = sinf(-170.0f * deg) };
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&steer_cfg, &v, heading, w);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 190.0f * deg, w[0].heading_rad);
    TEST_ASSERT_TRUE(w[0].speed_rad_s > 0.0f);
}

/* 多圈的当前朝向（转过两圈）也按最近的方向算 */
static void test_steer_multi_turn_heading(void)
{
    const float heading[STEER_WHEELS] = { 4.0f * RM_PI, 4.0f * RM_PI, 4.0f * RM_PI, 4.0f * RM_PI };
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&steer_cfg, &(ChassisVel){ .vy_m_s = 1.0f }, heading, w);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.0f * RM_PI + RM_PI / 2.0f, w[0].heading_rad);
}

static void test_steer_zero_speed_keeps_heading(void)
{
    const float heading[STEER_WHEELS] = { 0.3f, -1.0f, 2.0f, 7.0f };
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&steer_cfg, &(ChassisVel){ 0 }, heading, w);
    for (unsigned i = 0u; i < STEER_WHEELS; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(heading[i], w[i].heading_rad);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, w[i].speed_rad_s);
    }
}

static void test_steer_forward_inverts_inverse(void)
{
    const ChassisVel cases[] = {
        { 1.0f, 0.0f, 0.0f },  { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 2.0f },
        { 0.5f, 0.7f, -1.5f }, { -1.2f, 0.4f, 3.0f },
    };
    const float heading[STEER_WHEELS] = { 0.5f, -2.0f, 3.0f, 10.0f };
    for (unsigned k = 0u; k < sizeof cases / sizeof cases[0]; k++)
    {
        SteerWheel w[STEER_WHEELS];
        ChassisVel back;
        steer_inverse(&steer_cfg, &cases[k], heading, w);
        steer_forward(&steer_cfg, w, &back);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, cases[k].vx_m_s, back.vx_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, cases[k].vy_m_s, back.vy_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, cases[k].wz_rad_s, back.wz_rad_s);
    }
}

/* ---- half_steer：半舵半全向运动学的单元测试：哪条对角线是舵轮、平移、原地转、正解 ∘ 逆解 = 恒等（两种对角线） ---- */

/* 长方形底盘：前后 0.4 m、左右 0.3 m，舵轮半径 0.06 m、全向轮半径 0.076 m */
static const HalfSteerConfig lf_rb = { .steer_diagonal = HALF_STEER_LF_RB,
                                       .steer_radius_m = 0.06f,
                                       .omni_radius_m = 0.076f,
                                       .half_wheelbase_m = 0.2f,
                                       .half_track_m = 0.15f };
static const float half_steer_zero_heading[HALF_STEER_WHEELS] = { 0.0f, 0.0f, 0.0f, 0.0f };

static void test_half_steer_which_wheels_are_steer(void)
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
static void test_half_steer_forward_translation(void)
{
    SteerWheel w[HALF_STEER_WHEELS];
    half_steer_inverse(&lf_rb, &(ChassisVel){ .vx_m_s = 0.5f }, half_steer_zero_heading, w);
    const float r = sqrtf(0.2f * 0.2f + 0.15f * 0.15f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, w[0].heading_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f / 0.06f, w[0].speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f / 0.06f, w[2].speed_rad_s);
    /* 左后 (−0.2, 0.15)：−y·vx/R = −0.15·0.5/R；右前 (0.2, −0.15)：+0.15·0.5/R */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.15f * 0.5f / r / 0.076f, w[1].speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.15f * 0.5f / r / 0.076f, w[3].speed_rad_s);
}

/* 原地逆时针转：全向轮转速 = R·wz / r；舵轮沿切向（左前切向 (−0.15, 0.2) 超过 90°，所以反转轮子） */
static void test_half_steer_rotation(void)
{
    SteerWheel w[HALF_STEER_WHEELS];
    half_steer_inverse(&lf_rb, &(ChassisVel){ .wz_rad_s = 2.0f }, half_steer_zero_heading, w);
    const float r = sqrtf(0.2f * 0.2f + 0.15f * 0.15f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, r * 2.0f / 0.076f, w[1].speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, r * 2.0f / 0.076f, w[3].speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, atan2f(0.2f, -0.15f) - RM_PI, w[0].heading_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -r * 2.0f / 0.06f, w[0].speed_rad_s);
}

static void half_steer_check_round_trip(const HalfSteerConfig *cfg, ChassisVel v)
{
    SteerWheel w[HALF_STEER_WHEELS];
    ChassisVel back;
    half_steer_inverse(cfg, &v, half_steer_zero_heading, w);
    half_steer_forward(cfg, w, &back);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, v.vx_m_s, back.vx_m_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, v.vy_m_s, back.vy_m_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, v.wz_rad_s, back.wz_rad_s);
}

static void test_half_steer_round_trip_both_diagonals(void)
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
        half_steer_check_round_trip(&lf_rb, cases[k]);
        half_steer_check_round_trip(&other, cases[k]);
    }
}

void setUp(void)
{
}

void tearDown(void)
{
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_omni_x_layout_forward_motion);
    RUN_TEST(test_omni_x_layout_left_motion);
    RUN_TEST(test_omni_pure_rotation);
    RUN_TEST(test_omni_forward_inverts_inverse_x_layout);
    RUN_TEST(test_omni_forward_inverts_inverse_plus_layout);
    RUN_TEST(test_omni_forward_least_squares_on_inconsistent_speeds);
    RUN_TEST(test_mecanum_forward_all_wheels_same);
    RUN_TEST(test_mecanum_left_front_right_rear_reverse);
    RUN_TEST(test_mecanum_ccw_left_backward_right_forward);
    RUN_TEST(test_mecanum_forward_inverts_inverse);
    RUN_TEST(test_mecanum_forward_least_squares_on_inconsistent_speeds);
    RUN_TEST(test_steer_translation_all_wheels_same);
    RUN_TEST(test_steer_rotation_tangential_and_flipped);
    RUN_TEST(test_steer_target_heading_is_continuous_across_pi);
    RUN_TEST(test_steer_multi_turn_heading);
    RUN_TEST(test_steer_zero_speed_keeps_heading);
    RUN_TEST(test_steer_forward_inverts_inverse);
    RUN_TEST(test_half_steer_which_wheels_are_steer);
    RUN_TEST(test_half_steer_forward_translation);
    RUN_TEST(test_half_steer_rotation);
    RUN_TEST(test_half_steer_round_trip_both_diagonals);
    return UNITY_END();
}
