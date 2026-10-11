/**
 * @file    test_filter.c
 * @brief   filter 的单元测试，按节：lpf、kalman
 */
#include "03_algorithm/filter/filter.h"

#include "unity.h"

/* ---- lpf：lpf 的单元测试：首次输入填充、一阶递推、二阶直流增益（用旧工程 IMU 加速度滤波的系数） ---- */

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

/* ---- kalman：kalman 的单元测试：存储大小宏、一维时与标量公式逐步一致、匀速模型估出速度、控制量 ---- */

/* 最后一个矩阵正好用到宏算出的末尾，末尾之后的内存不被碰 */
static void test_kalman_storage_macro_matches_layout(void)
{
    enum
    {
        N = 6,
        M = 3,
        U = 2,
        SIZE = KALMAN_STORAGE_FLOATS(N, M, U)
    };
    float storage[SIZE + 4];
    for (int i = 0; i < SIZE + 4; i++)
    {
        storage[i] = 123.0f;
    }
    KalmanFilter kf;
    kalman_init(&kf, N, M, U, storage);
    TEST_ASSERT_EQUAL_PTR(storage + SIZE, kf.vec.data + N);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, storage[SIZE - 1]);
    TEST_ASSERT_EQUAL_FLOAT(123.0f, storage[SIZE]);
}

/* 一维（A = H = 1）时卡尔曼就是几行标量公式，逐步比对增益、新息、估计和协方差 */
static void test_kalman_scalar_matches_textbook_formula(void)
{
    static float storage[KALMAN_STORAGE_FLOATS(1, 1, 0)];
    KalmanFilter kf;
    kalman_init(&kf, 1, 1, 0, storage);
    const float q = 0.1f;
    const float r = 1.0f;
    kf.A.data[0] = 1.0f;
    kf.H.data[0] = 1.0f;
    kf.Q.data[0] = q;
    kf.R.data[0] = r;
    kf.P.data[0] = 1.0f;

    float x = 0.0f;
    float p = 1.0f;
    for (int k = 0; k < 20; k++)
    {
        const float z = (k % 2 == 0) ? 2.0f : 1.5f;
        kf.z.data[0] = z;
        kalman_update(&kf);

        const float p_minus = p + q;
        const float gain = p_minus / (p_minus + r);
        const float innovation = z - x;
        x = x + gain * innovation;
        p = p_minus - gain * p_minus;

        TEST_ASSERT_FLOAT_WITHIN(1e-6f, gain, kf.K.data[0]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, innovation, kf.y.data[0]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, x, kf.x.data[0]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, p, kf.P.data[0]);
    }
}

/* 匀速模型：状态 [位置, 速度]，只测位置；真实运动 x = 1 + 2t，滤波器应估出速度 2 */
static void test_kalman_constant_velocity_estimates_speed(void)
{
    static float storage[KALMAN_STORAGE_FLOATS(2, 1, 0)];
    KalmanFilter kf;
    kalman_init(&kf, 2, 1, 0, storage);
    const float dt = 0.01f;
    const float a[4] = { 1.0f, dt, 0.0f, 1.0f };
    for (int i = 0; i < 4; i++)
    {
        kf.A.data[i] = a[i];
    }
    kf.H.data[0] = 1.0f;
    kf.Q.data[0] = 1e-6f;
    kf.Q.data[3] = 1e-4f;
    kf.R.data[0] = 1e-2f;
    kf.P.data[0] = 1.0f;
    kf.P.data[3] = 10.0f;

    for (int k = 1; k <= 500; k++)
    {
        kf.z.data[0] = 1.0f + 2.0f * dt * (float)k;
        kalman_update(&kf);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 1.0f + 2.0f * dt * 500.0f, kf.x.data[0]);
    TEST_ASSERT_FLOAT_WITHIN(2e-2f, 2.0f, kf.x.data[1]);
}

/* 有控制量时状态预测为 x⁻ = A x + B u */
static void test_kalman_predict_with_control_input(void)
{
    static float storage[KALMAN_STORAGE_FLOATS(2, 1, 1)];
    KalmanFilter kf;
    kalman_init(&kf, 2, 1, 1, storage);
    const float a[4] = { 1.0f, 0.5f, 0.0f, 1.0f };
    for (int i = 0; i < 4; i++)
    {
        kf.A.data[i] = a[i];
    }
    kf.B.data[0] = 0.125f;
    kf.B.data[1] = 0.5f;
    kf.x.data[0] = 1.0f;
    kf.x.data[1] = 2.0f;
    kf.u_vec.data[0] = 4.0f;
    kalman_predict_state(&kf);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, kf.x_minus.data[0]); /* 1 + 0.5 * 2 + 0.125 * 4 */
    TEST_ASSERT_EQUAL_FLOAT(4.0f, kf.x_minus.data[1]); /* 2 + 0.5 * 4 */
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
    RUN_TEST(test_lpf1_first_input_passes_through);
    RUN_TEST(test_lpf1_recursion);
    RUN_TEST(test_lpf1_init_restarts);
    RUN_TEST(test_lpf2_constant_input_stays_constant);
    RUN_TEST(test_lpf2_step_converges);
    RUN_TEST(test_lpf2_recursion_order);
    RUN_TEST(test_kalman_storage_macro_matches_layout);
    RUN_TEST(test_kalman_scalar_matches_textbook_formula);
    RUN_TEST(test_kalman_constant_velocity_estimates_speed);
    RUN_TEST(test_kalman_predict_with_control_input);
    return UNITY_END();
}
