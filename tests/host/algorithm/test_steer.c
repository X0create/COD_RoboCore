/**
 * @file    test_steer.c
 * @brief   舵轮运动学的单元测试：平移时四轮同向同速、原地转时各轮沿切向、超过 90° 时反转轮子换另一侧、
 *          目标朝向在 ±π 附近连续、速度为 0 时保持朝向、正解 ∘ 逆解 = 恒等
 */
#include "algorithm/kinematics/steer.h"

#include <math.h>

#include "unity.h"

#define PI_F 3.14159265359f

static const SteerConfig cfg = { .wheel_radius_m = 0.06f,
                                 .half_wheelbase_m = 0.2f,
                                 .half_track_m = 0.2f };
static const float zero_heading[STEER_WHEELS] = { 0.0f, 0.0f, 0.0f, 0.0f };

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_translation_all_wheels_same(void)
{
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&cfg, &(ChassisVel){ .vx_m_s = 0.3f, .vy_m_s = 0.3f }, zero_heading, w);
    for (unsigned i = 0u; i < STEER_WHEELS; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, PI_F / 4.0f, w[i].heading_rad);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, sqrtf(0.18f) / 0.06f, w[i].speed_rad_s);
    }
}

/* 原地逆时针转：左前轮 (0.2, 0.2) 的速度方向是 135°，离当前 0° 超过 90°，所以朝向 −45°、轮子反转 */
static void test_rotation_tangential_and_flipped(void)
{
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&cfg, &(ChassisVel){ .wz_rad_s = 1.0f }, zero_heading, w);
    const float speed = sqrtf(0.08f) / 0.06f;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -PI_F / 4.0f, w[0].heading_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -speed, w[0].speed_rad_s);
    /* 右前轮 (0.2, −0.2)：速度方向 45°，不用翻转 */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, PI_F / 4.0f, w[3].heading_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, speed, w[3].speed_rad_s);
}

/* 当前朝向 170°，要去 −170°：只转 20°，结果是连续的 190°，不绕一大圈 */
static void test_target_heading_is_continuous_across_pi(void)
{
    const float deg = PI_F / 180.0f;
    const float heading[STEER_WHEELS] = { 170.0f * deg, 170.0f * deg, 170.0f * deg, 170.0f * deg };
    const ChassisVel v = { .vx_m_s = cosf(-170.0f * deg), .vy_m_s = sinf(-170.0f * deg) };
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&cfg, &v, heading, w);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 190.0f * deg, w[0].heading_rad);
    TEST_ASSERT_TRUE(w[0].speed_rad_s > 0.0f);
}

/* 多圈的当前朝向（转过两圈）也按最近的方向算 */
static void test_multi_turn_heading(void)
{
    const float heading[STEER_WHEELS] = { 4.0f * PI_F, 4.0f * PI_F, 4.0f * PI_F, 4.0f * PI_F };
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&cfg, &(ChassisVel){ .vy_m_s = 1.0f }, heading, w);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.0f * PI_F + PI_F / 2.0f, w[0].heading_rad);
}

static void test_zero_speed_keeps_heading(void)
{
    const float heading[STEER_WHEELS] = { 0.3f, -1.0f, 2.0f, 7.0f };
    SteerWheel w[STEER_WHEELS];
    steer_inverse(&cfg, &(ChassisVel){ 0 }, heading, w);
    for (unsigned i = 0u; i < STEER_WHEELS; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(heading[i], w[i].heading_rad);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, w[i].speed_rad_s);
    }
}

static void test_forward_inverts_inverse(void)
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
        steer_inverse(&cfg, &cases[k], heading, w);
        steer_forward(&cfg, w, &back);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, cases[k].vx_m_s, back.vx_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, cases[k].vy_m_s, back.vy_m_s);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, cases[k].wz_rad_s, back.wz_rad_s);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_translation_all_wheels_same);
    RUN_TEST(test_rotation_tangential_and_flipped);
    RUN_TEST(test_target_heading_is_continuous_across_pi);
    RUN_TEST(test_multi_turn_heading);
    RUN_TEST(test_zero_speed_keeps_heading);
    RUN_TEST(test_forward_inverts_inverse);
    return UNITY_END();
}
