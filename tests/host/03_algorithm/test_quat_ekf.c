/**
 * @file    test_quat_ekf.c
 * @brief   quat_ekf 的单元测试：静止水平、陀螺积分航向、倾斜收敛、卡方拒绝单个异常值、零偏估计、欧拉角
 * @note    没有旧工程的对照数据（Python 对照数据缺失，见 PROJECT_CONTEXT），期望值由几何关系得出：
 *          横滚 φ 时重力在机体系中为 (0, g·sinφ, g·cosφ)，俯仰 θ 时为 (−g·sinθ, 0, g·cosθ)
 */
#include "03_algorithm/attitude/quat_ekf.h"

#include <math.h>

#include "unity.h"

#define G    9.8f
#define DT_S 0.001f

static QuatEkf ekf;

void setUp(void)
{
    quat_ekf_init(&ekf, 10.0f, 0.001f, 1000000.0f); /* 旧工程参数 */
}

void tearDown(void)
{
}

static void run(int steps, const float gyro[3], const float accel[3])
{
    for (int i = 0; i < steps; i++)
    {
        quat_ekf_update(&ekf, gyro, accel, DT_S);
    }
}

static void euler(float *yaw, float *pitch, float *roll)
{
    quat_to_euler(ekf.q, yaw, pitch, roll);
}

static const float zero[3] = { 0.0f, 0.0f, 0.0f };
static const float level[3] = { 0.0f, 0.0f, G };

static void test_still_level_stays_identity(void)
{
    run(2000, zero, level);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, ekf.q[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, ekf.q[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, ekf.q[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, ekf.q[3]);
}

/* 加速度看不出航向：绕 z 以 1 rad/s 转 1 s，航向约 1 rad，全靠陀螺积分 */
static void test_yaw_from_gyro_integration(void)
{
    const float gz[3] = { 0.0f, 0.0f, 1.0f };
    run(1000, gz, level);
    float yaw, pitch, roll;
    euler(&yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(5e-3f, 1.0f, yaw);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, pitch);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, roll);
}

/* 从水平初值开始，加速度显示横滚 0.3 rad：收敛到 0.3 */
static void test_converges_to_roll(void)
{
    const float phi = 0.3f;
    const float accel[3] = { 0.0f, G * sinf(phi), G * cosf(phi) };
    run(10000, zero, accel);
    float yaw, pitch, roll;
    euler(&yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, phi, roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 0.0f, pitch);
}

static void test_converges_to_pitch(void)
{
    const float theta = -0.2f;
    const float accel[3] = { -G * sinf(theta), 0.0f, G * cosf(theta) };
    run(10000, zero, accel);
    float yaw, pitch, roll;
    euler(&yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, theta, pitch);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 0.0f, roll);
}

/* 已收敛后来一个方向偏 0.5 rad 的加速度（如一次冲击）：卡方检验跳过这次修正，姿态不动 */
static void test_chi_square_rejects_single_outlier(void)
{
    run(3000, zero, level);
    float yaw0, pitch0, roll0;
    euler(&yaw0, &pitch0, &roll0);
    const float outlier[3] = { 0.0f, G * sinf(0.5f), G * cosf(0.5f) };
    quat_ekf_update(&ekf, zero, outlier, DT_S);
    float yaw1, pitch1, roll1;
    euler(&yaw1, &pitch1, &roll1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, roll0, roll1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, pitch0, pitch1);
}

/* 静止水平但陀螺 x 轴有 0.01 rad/s 零偏：零偏被估计出来，横滚不随时间漂走 */
static void test_estimates_x_bias(void)
{
    const float biased[3] = { 0.01f, 0.0f, 0.0f };
    run(60000, biased, level);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.01f, ekf.gyro_bias[0]);
    float yaw, pitch, roll;
    euler(&yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(2e-2f, 0.0f, roll);
}

static void test_output_normalized(void)
{
    const float g[3] = { 0.3f, -0.2f, 0.5f };
    run(500, g, level);
    const float n =
        ekf.q[0] * ekf.q[0] + ekf.q[1] * ekf.q[1] + ekf.q[2] * ekf.q[2] + ekf.q[3] * ekf.q[3];
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, n);
}

static void test_euler_of_known_quaternions(void)
{
    const float h = 0.70710678f;
    float yaw, pitch, roll;
    const float yaw90[4] = { h, 0.0f, 0.0f, h };
    quat_to_euler(yaw90, &yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.5707963f, yaw);
    const float roll90[4] = { h, h, 0.0f, 0.0f };
    quat_to_euler(roll90, &yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.5707963f, roll);
    const float pitch90[4] = { h, 0.0f, h, 0.0f }; /* 奇异点：asin 的参数被截到 1，不是 NaN */
    quat_to_euler(pitch90, &yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.5707963f, pitch);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_still_level_stays_identity);
    RUN_TEST(test_yaw_from_gyro_integration);
    RUN_TEST(test_converges_to_roll);
    RUN_TEST(test_converges_to_pitch);
    RUN_TEST(test_chi_square_rejects_single_outlier);
    RUN_TEST(test_estimates_x_bias);
    RUN_TEST(test_output_normalized);
    RUN_TEST(test_euler_of_known_quaternions);
    return UNITY_END();
}
