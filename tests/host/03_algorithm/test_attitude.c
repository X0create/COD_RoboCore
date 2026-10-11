/**
 * @file    test_attitude.c
 * @brief   attitude 的单元测试，按节：gyro_bias、quat_ekf
 */
#include "03_algorithm/attitude/attitude.h"

#include <math.h>

#include "unity.h"

/* ---- gyro_bias：gyro_bias 的单元测试：均值与标准差、各种拒绝情况、判据与样本数无关 ---- */

static GyroBias gb;

/* 均值为 bias、正负交替 noise 的样本：标准差约为 noise */
static void feed(uint32_t n, const float bias[3], float noise)
{
    for (uint32_t k = 0u; k < n; k++)
    {
        const float s = (k % 2u == 0u) ? noise : -noise;
        const float g[3] = { bias[0] + s, bias[1] - s, bias[2] + s };
        gyro_bias_add(&gb, g);
    }
}

static void test_gyro_bias_known_sequence(void)
{
    const float xs[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    for (int i = 0; i < 4; i++)
    {
        const float g[3] = { xs[i], 0.0f, 0.0f };
        gyro_bias_add(&gb, g);
    }
    float bias[3];
    /* 样本标准差 1.29099 */
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_NOT_STILL, gyro_bias_result(&gb, 4u, 1.29f, 10.0f, bias));
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_OK, gyro_bias_result(&gb, 4u, 1.30f, 10.0f, bias));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.5f, bias[0]);
}

static void test_gyro_bias_still_board_gives_mean(void)
{
    const float truth[3] = { 0.004f, -0.003f, 0.0001f };
    feed(2000u, truth, 0.002f);
    float bias[3];
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_OK, gyro_bias_result(&gb, 1000u, 0.05f, 0.15f, bias));
    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, truth[i], bias[i]);
    }
}

static void test_gyro_bias_rejections(void)
{
    const float small[3] = { 0.0f, 0.0f, 0.0f };
    float bias[3];
    feed(10u, small, 0.001f);
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_TOO_FEW, gyro_bias_result(&gb, 100u, 0.05f, 0.15f, bias));

    gyro_bias_reset(&gb);
    feed(1000u, small, 0.2f); /* 在动 */
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_NOT_STILL, gyro_bias_result(&gb, 100u, 0.05f, 0.15f, bias));

    gyro_bias_reset(&gb);
    const float large[3] = { 0.0f, 0.3f, 0.0f }; /* 很稳但均值过大：匀速转或放歪 */
    feed(1000u, large, 0.001f);
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_TOO_LARGE, gyro_bias_result(&gb, 100u, 0.05f, 0.15f, bias));
}

/* 同样的噪声，100 个和 20000 个样本都判为静止（峰峰值判据在样本多时会失败，UniC 踩过） */
static void test_gyro_bias_threshold_independent_of_sample_count(void)
{
    const float truth[3] = { 0.001f, 0.001f, 0.001f };
    float bias[3];
    feed(100u, truth, 0.01f);
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_OK, gyro_bias_result(&gb, 100u, 0.02f, 0.15f, bias));
    gyro_bias_reset(&gb);
    feed(20000u, truth, 0.01f);
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_OK, gyro_bias_result(&gb, 100u, 0.02f, 0.15f, bias));
}

/* ---- quat_ekf：quat_ekf 的单元测试：静止水平、陀螺积分航向、倾斜收敛、卡方拒绝单个异常值、零偏估计、欧拉角@note    没有旧工程的对照数据（Python 对照数据缺失，见 PROJECT_CONTEXT），期望值由几何关系得出：横滚 φ 时重力在机体系中为 (0, g·sinφ, g·cosφ)，俯仰 θ 时为 (−g·sinθ, 0, g·cosθ) ---- */

#define G    9.8f
#define DT_S 0.001f

static QuatEkf ekf;

static void run(int steps, const float gyro[3], const float accel[3])
{
    for (int i = 0; i < steps; i++)
    {
        quat_ekf_update(&ekf, gyro, accel, DT_S);
    }
}

static void euler(float *yaw, float *pitch, float *roll)
{
    quat_ekf_to_euler(ekf.q, yaw, pitch, roll);
}

static const float zero[3] = { 0.0f, 0.0f, 0.0f };
static const float level[3] = { 0.0f, 0.0f, G };

static void test_quat_ekf_still_level_stays_identity(void)
{
    run(2000, zero, level);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, ekf.q[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, ekf.q[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, ekf.q[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, ekf.q[3]);
}

/* 加速度看不出航向：绕 z 以 1 rad/s 转 1 s，航向约 1 rad，全靠陀螺积分 */
static void test_quat_ekf_yaw_from_gyro_integration(void)
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
static void test_quat_ekf_converges_to_roll(void)
{
    const float phi = 0.3f;
    const float accel[3] = { 0.0f, G * sinf(phi), G * cosf(phi) };
    run(10000, zero, accel);
    float yaw, pitch, roll;
    euler(&yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, phi, roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 0.0f, pitch);
}

static void test_quat_ekf_converges_to_pitch(void)
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
static void test_quat_ekf_chi_square_rejects_single_outlier(void)
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
static void test_quat_ekf_estimates_x_bias(void)
{
    const float biased[3] = { 0.01f, 0.0f, 0.0f };
    run(60000, biased, level);
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.01f, ekf.gyro_bias[0]);
    float yaw, pitch, roll;
    euler(&yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(2e-2f, 0.0f, roll);
}

static void test_quat_ekf_output_normalized(void)
{
    const float g[3] = { 0.3f, -0.2f, 0.5f };
    run(500, g, level);
    const float n =
        ekf.q[0] * ekf.q[0] + ekf.q[1] * ekf.q[1] + ekf.q[2] * ekf.q[2] + ekf.q[3] * ekf.q[3];
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, n);
}

static void test_quat_ekf_euler_of_known_quaternions(void)
{
    const float h = 0.70710678f;
    float yaw, pitch, roll;
    const float yaw90[4] = { h, 0.0f, 0.0f, h };
    quat_ekf_to_euler(yaw90, &yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.5707963f, yaw);
    const float roll90[4] = { h, h, 0.0f, 0.0f };
    quat_ekf_to_euler(roll90, &yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.5707963f, roll);
    const float pitch90[4] = { h, 0.0f, h, 0.0f }; /* 奇异点：asin 的参数被截到 1，不是 NaN */
    quat_ekf_to_euler(pitch90, &yaw, &pitch, &roll);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.5707963f, pitch);
}

void setUp(void)
{
    gyro_bias_reset(&gb);
    quat_ekf_init(&ekf, 10.0f, 0.001f, 1000000.0f); /* 旧工程参数 */
}

void tearDown(void)
{
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gyro_bias_known_sequence);
    RUN_TEST(test_gyro_bias_still_board_gives_mean);
    RUN_TEST(test_gyro_bias_rejections);
    RUN_TEST(test_gyro_bias_threshold_independent_of_sample_count);
    RUN_TEST(test_quat_ekf_still_level_stays_identity);
    RUN_TEST(test_quat_ekf_yaw_from_gyro_integration);
    RUN_TEST(test_quat_ekf_converges_to_roll);
    RUN_TEST(test_quat_ekf_converges_to_pitch);
    RUN_TEST(test_quat_ekf_chi_square_rejects_single_outlier);
    RUN_TEST(test_quat_ekf_estimates_x_bias);
    RUN_TEST(test_quat_ekf_output_normalized);
    RUN_TEST(test_quat_ekf_euler_of_known_quaternions);
    return UNITY_END();
}
