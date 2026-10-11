/**
 * @file    test_shoot.c
 * @brief   shoot 的单元测试，按节：热量限频、本地热量估算、卡弹检测与反转、开火判定、弹速自适应
 */
#include "03_algorithm/shoot/shoot.h"

#include <math.h>

#include "unity.h"

#define DT_S 0.001f

/* ---- 热量限频 ---- */

static const ShootRateConfig rate_cfg = { .heat_per_shot = 10.0f,
                                          .max_rate_hz = 20.0f,
                                          .full_heat = 100.0f,
                                          .stop_heat = 25.0f,
                                          .window_s = 1.0f };

static void test_rate_full_stop_and_middle(void)
{
    TEST_ASSERT_EQUAL_FLOAT(20.0f, shoot_rate_limit_hz(&rate_cfg, 150.0f, 40.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, shoot_rate_limit_hz(&rate_cfg, 25.0f, 40.0f));
    /* 剩余 65：冷却 40/10 = 4 发/s + (65 − 25)/(10 × 1) = 4 发/s */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 8.0f, shoot_rate_limit_hz(&rate_cfg, 65.0f, 40.0f));
}

/* 按限频连续打 20 s（裁判系统按每 0.1 s 结算一次冷却的方式模拟）：热量从不超过上限 */
static void test_rate_never_exceeds_heat_limit(void)
{
    const float limit = 200.0f, cooling = 40.0f;
    float heat = 0.0f, credit = 0.0f, max_heat = 0.0f;
    for (int k = 0; k < 20000; k++)
    {
        credit += shoot_rate_limit_hz(&rate_cfg, limit - heat, cooling) * DT_S;
        if (credit >= 1.0f)
        {
            credit -= 1.0f;
            heat += rate_cfg.heat_per_shot;
        }
        if (k % 100 == 0)
        {
            heat = fmaxf(heat - cooling * 0.1f, 0.0f);
        }
        max_heat = fmaxf(max_heat, heat);
    }
    TEST_ASSERT_TRUE(max_heat <= limit);
}

/* ---- 本地热量估算 ---- */

static void test_heat_estimate_and_sync(void)
{
    ShootHeat h = { 0 };
    shoot_heat_step(&h, 2u, 10.0f, 40.0f, DT_S);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f - 0.04f, h.heat);
    for (int k = 0; k < 1000; k++)
    {
        shoot_heat_step(&h, 0u, 10.0f, 40.0f, DT_S); /* 1 s 冷却 40：到 0 为止，不为负 */
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0f, h.heat);
    shoot_heat_sync(&h, 30.0f); /* 裁判值更大：取裁判值 */
    TEST_ASSERT_EQUAL_FLOAT(30.0f, h.heat);
    shoot_heat_step(&h, 1u, 10.0f, 0.0f, DT_S);
    shoot_heat_sync(&h, 30.0f); /* 估算值更大（裁判还没算到这一发）：保留估算值 */
    TEST_ASSERT_EQUAL_FLOAT(40.0f, h.heat);
}

/* ---- 卡弹检测与反转 ---- */

static const ShootJamConfig jam_cfg = { .torque_threshold_nm = 1.0f,
                                        .jam_time_s = 0.15f,
                                        .no_shot_time_s = 0.5f,
                                        .reverse_speed_rad_s = 10.0f,
                                        .reverse_min_s = 0.1f,
                                        .reverse_max_s = 0.15f,
                                        .release_torque_nm = 0.6f };

static void test_jam_by_torque_reverses_then_resumes(void)
{
    ShootJam j = { 0 };
    float out = 0.0f;
    for (int k = 0; k < 149; k++)
    {
        out = shoot_jam_step(&j, &jam_cfg, 20.0f, 1.2f, k % 50 == 0, DT_S);
    }
    TEST_ASSERT_EQUAL_FLOAT(20.0f, out); /* 不到 150 ms：还不算卡弹 */
    out = shoot_jam_step(&j, &jam_cfg, 20.0f, 1.2f, false, DT_S);
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, out);
    TEST_ASSERT_EQUAL_UINT(1u, j.jam_count);

    for (int k = 0; k < 99; k++) /* 至少反转 100 ms，力矩已经降下来也不提前结束 */
    {
        out = shoot_jam_step(&j, &jam_cfg, 20.0f, 0.1f, false, DT_S);
        TEST_ASSERT_EQUAL_FLOAT(-10.0f, out);
    }
    out = shoot_jam_step(&j, &jam_cfg, 20.0f, 0.1f, false, DT_S);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, out); /* 满 100 ms 且力矩已降：恢复 */
}

/* 反转期间力矩一直很大：最多反转 150 ms 也恢复，不一直反转 */
static void test_jam_reverse_has_max_time(void)
{
    ShootJam j = { .reversing = true };
    float out = 0.0f;
    for (int k = 0; k < 150; k++)
    {
        out = shoot_jam_step(&j, &jam_cfg, 20.0f, 2.0f, false, DT_S);
    }
    TEST_ASSERT_EQUAL_FLOAT(20.0f, out);
}

/* 一直在拨弹却 0.5 s 没出弹：也判卡弹；不拨弹时不计时 */
static void test_jam_by_no_shot(void)
{
    ShootJam j = { 0 };
    for (int k = 0; k < 2000; k++)
    {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, shoot_jam_step(&j, &jam_cfg, 0.0f, 0.0f, false, DT_S));
    }
    float out = 0.0f;
    for (int k = 0; k < 450; k++)
    {
        out = shoot_jam_step(&j, &jam_cfg, 20.0f, 0.2f, false, DT_S);
    }
    TEST_ASSERT_EQUAL_FLOAT(20.0f, out); /* 450 ms：还没到 0.5 s */
    for (int k = 0; k < 60; k++) /* 过了 0.5 s（浮点累加有误差，不卡在正好 500 拍上） */
    {
        out = shoot_jam_step(&j, &jam_cfg, 20.0f, 0.2f, false, DT_S);
    }
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, out);
}

/* ---- 开火判定 ---- */

static void test_on_target_by_armor_angle(void)
{
    /* 3 m 外、半宽 0.0825 m：张角约 0.0275 rad */
    TEST_ASSERT_TRUE(shoot_on_target(0.02f, 0.0f, 3.0f, 0.0825f, 1.5f));
    TEST_ASSERT_FALSE(shoot_on_target(0.03f, 0.0f, 3.0f, 0.0825f, 1.5f));
    TEST_ASSERT_FALSE(shoot_on_target(-0.03f, 0.0f, 3.0f, 0.0825f, 1.5f));
    TEST_ASSERT_TRUE(shoot_on_target(0.03f, 0.0f, 2.0f, 0.0825f, 1.5f)); /* 近了张角变大 */
}

/* ---- 弹速自适应 ---- */

static const ShootSpeedConfig speed_cfg = { .speed_low_m_s = 22.5f,
                                            .speed_high_m_s = 23.0f,
                                            .speed_valid_m_s = 8.0f,
                                            .step_up_rad_s = 2.0f,
                                            .step_down_rad_s = 4.0f,
                                            .target_min_rad_s = 600.0f,
                                            .target_max_rad_s = 640.0f };

static void test_speed_adapt(void)
{
    TEST_ASSERT_EQUAL_FLOAT(622.0f, shoot_speed_adapt(&speed_cfg, 620.0f, 22.0f)); /* 偏低：调高 */
    TEST_ASSERT_EQUAL_FLOAT(616.0f, shoot_speed_adapt(&speed_cfg, 620.0f, 23.5f)); /* 偏高：调低 */
    TEST_ASSERT_EQUAL_FLOAT(620.0f,
                            shoot_speed_adapt(&speed_cfg, 620.0f, 22.8f)); /* 在范围内：不动 */
    TEST_ASSERT_EQUAL_FLOAT(620.0f,
                            shoot_speed_adapt(&speed_cfg, 620.0f, 5.0f)); /* 读数无效：不动 */
    TEST_ASSERT_EQUAL_FLOAT(640.0f, shoot_speed_adapt(&speed_cfg, 639.0f, 20.0f)); /* 限幅 */
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
    RUN_TEST(test_rate_full_stop_and_middle);
    RUN_TEST(test_rate_never_exceeds_heat_limit);
    RUN_TEST(test_heat_estimate_and_sync);
    RUN_TEST(test_jam_by_torque_reverses_then_resumes);
    RUN_TEST(test_jam_reverse_has_max_time);
    RUN_TEST(test_jam_by_no_shot);
    RUN_TEST(test_on_target_by_armor_angle);
    RUN_TEST(test_speed_adapt);
    return UNITY_END();
}
