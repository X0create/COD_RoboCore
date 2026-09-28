/**
 * @file    test_gyro_bias.c
 * @brief   gyro_bias 的单元测试：均值与标准差、各种拒绝情况、判据与样本数无关
 */
#include "algorithm/attitude/gyro_bias.h"

#include "unity.h"

static GyroBias gb;

void setUp(void)
{
    gyro_bias_reset(&gb);
}

void tearDown(void)
{
}

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

static void test_known_sequence(void)
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

static void test_still_board_gives_mean(void)
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

static void test_rejections(void)
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
static void test_threshold_independent_of_sample_count(void)
{
    const float truth[3] = { 0.001f, 0.001f, 0.001f };
    float bias[3];
    feed(100u, truth, 0.01f);
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_OK, gyro_bias_result(&gb, 100u, 0.02f, 0.15f, bias));
    gyro_bias_reset(&gb);
    feed(20000u, truth, 0.01f);
    TEST_ASSERT_EQUAL_INT(GYRO_BIAS_OK, gyro_bias_result(&gb, 100u, 0.02f, 0.15f, bias));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_sequence);
    RUN_TEST(test_still_board_gives_mean);
    RUN_TEST(test_rejections);
    RUN_TEST(test_threshold_independent_of_sample_count);
    return UNITY_END();
}
