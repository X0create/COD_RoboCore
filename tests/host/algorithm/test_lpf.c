/**
 * @file    test_lpf.c
 * @brief   lpf 的单元测试：首次输入填充、一阶递推、二阶直流增益（用旧工程 IMU 加速度滤波的系数）
 */
#include "algorithm/filter/lpf.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_lpf1_first_input_passes_through(void)
{
    Lpf1 lpf;
    lpf1_init(&lpf, 0.9f);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, lpf1_update(&lpf, 7.0f));
}

static void test_lpf1_recursion(void)
{
    Lpf1 lpf;
    lpf1_init(&lpf, 0.75f);
    lpf1_update(&lpf, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, lpf1_update(&lpf, 4.0f)); /* 0.75 * 0 + 0.25 * 4 */
    TEST_ASSERT_EQUAL_FLOAT(1.75f, lpf1_update(&lpf, 4.0f));
}

static void test_lpf1_init_restarts(void)
{
    Lpf1 lpf;
    lpf1_init(&lpf, 0.5f);
    lpf1_update(&lpf, 100.0f);
    lpf1_init(&lpf, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, lpf1_update(&lpf, -3.0f));
}

/*
 * 旧工程 INS_Task.c 的加速度二阶低通系数，三项之和为 1。
 * 单精度下三项相加并不严格等于 1，直流增益约 1.00001（旧工程同样如此），所以比较用相对误差 1e-4。
 */
static const float imu_accel_a[3] = {
    1.929454039488895f,
    -0.93178349823448126f,
    0.002329458745586203f,
};

static void test_lpf2_constant_input_stays_constant(void)
{
    Lpf2 lpf;
    lpf2_init(&lpf, imu_accel_a);
    for (int i = 0; i < 100; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(9.8f * 1e-4f, 9.8f, lpf2_update(&lpf, 9.8f));
    }
}

static void test_lpf2_step_converges(void)
{
    Lpf2 lpf;
    lpf2_init(&lpf, imu_accel_a);
    lpf2_update(&lpf, 0.0f);
    float y = 0.0f;
    for (int i = 0; i < 2000; i++)
    {
        y = lpf2_update(&lpf, 1.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, y);
}

/* 递推顺序：y = a0 * y[n-1] + a1 * y[n-2] + a2 * x */
static void test_lpf2_recursion_order(void)
{
    Lpf2 lpf;
    const float a[3] = { 0.5f, 0.25f, 1.0f };
    lpf2_init(&lpf, a);
    /* 首次输入把历史填成 1：0.5 * 1 + 0.25 * 1 + 1 * 1 */
    TEST_ASSERT_EQUAL_FLOAT(1.75f, lpf2_update(&lpf, 1.0f));
    /* 0.5 * 1.75 + 0.25 * 1；两个系数对调会得 0.9375 */
    TEST_ASSERT_EQUAL_FLOAT(1.125f, lpf2_update(&lpf, 0.0f));
    /* 0.5 * 1.125 + 0.25 * 1.75 */
    TEST_ASSERT_EQUAL_FLOAT(1.0f, lpf2_update(&lpf, 0.0f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lpf1_first_input_passes_through);
    RUN_TEST(test_lpf1_recursion);
    RUN_TEST(test_lpf1_init_restarts);
    RUN_TEST(test_lpf2_constant_input_stays_constant);
    RUN_TEST(test_lpf2_step_converges);
    RUN_TEST(test_lpf2_recursion_order);
    return UNITY_END();
}
