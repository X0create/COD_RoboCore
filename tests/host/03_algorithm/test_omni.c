/**
 * @file    test_omni.c
 * @brief   全向轮运动学的单元测试：X 形布置的已知值、纯旋转、正解 ∘ 逆解 = 恒等（两种布置）、
 *          四个轮速矛盾时正解取最小二乘
 */
#include "03_algorithm/kinematics/omni.h"

#include "03_algorithm/math/math_const.h"

#include "unity.h"

static const OmniConfig x_layout = { .wheel_radius_m = 0.08f,
                                     .center_dist_m = 0.25f,
                                     .first_wheel_rad = RM_PI / 4.0f };
static const OmniConfig plus_layout = { .wheel_radius_m = 0.06f,
                                        .center_dist_m = 0.3f,
                                        .first_wheel_rad = 0.0f };

void setUp(void)
{
}

void tearDown(void)
{
}

/* X 形：轮 0–3 = 左前、左后、右后、右前。向前走时左侧两轮顺时针（负）、右侧两轮逆时针（正） */
static void test_x_layout_forward_motion(void)
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
static void test_x_layout_left_motion(void)
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
static void test_pure_rotation(void)
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

static void check_round_trip(const OmniConfig *cfg)
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

static void test_forward_inverts_inverse_x_layout(void)
{
    check_round_trip(&x_layout);
}

static void test_forward_inverts_inverse_plus_layout(void)
{
    check_round_trip(&plus_layout);
}

/* 四个轮速互相矛盾（只有一个轮子在转，相当于其余三个打滑）：正解是最小二乘解，再逆解回去与原轮速的
 * 残差和“可实现的轮速组合”正交——这里用结果验证：残差在每个自由度方向上的投影为 0 */
static void test_forward_least_squares_on_inconsistent_speeds(void)
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_x_layout_forward_motion);
    RUN_TEST(test_x_layout_left_motion);
    RUN_TEST(test_pure_rotation);
    RUN_TEST(test_forward_inverts_inverse_x_layout);
    RUN_TEST(test_forward_inverts_inverse_plus_layout);
    RUN_TEST(test_forward_least_squares_on_inconsistent_speeds);
    return UNITY_END();
}
