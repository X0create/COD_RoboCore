/**
 * @file    test_kalman.c
 * @brief   kalman 的单元测试：存储大小宏、一维时与标量公式逐步一致、匀速模型估出速度、控制量
 */
#include "03_algorithm/filter/kalman.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* 最后一个矩阵正好用到宏算出的末尾，末尾之后的内存不被碰 */
static void test_storage_macro_matches_layout(void)
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
static void test_scalar_matches_textbook_formula(void)
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
static void test_constant_velocity_estimates_speed(void)
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
static void test_predict_with_control_input(void)
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_storage_macro_matches_layout);
    RUN_TEST(test_scalar_matches_textbook_formula);
    RUN_TEST(test_constant_velocity_estimates_speed);
    RUN_TEST(test_predict_with_control_input);
    return UNITY_END();
}
