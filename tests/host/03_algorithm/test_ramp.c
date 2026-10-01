/**
 * @file    test_ramp.c
 * @brief   ramp 的单元测试：按步长靠近、不越过目标、两个方向
 */
#include "03_algorithm/control/ramp.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_moves_by_step(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.0f, ramp_step(0.0f, 10.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, ramp_step(0.0f, -10.0f, 1.0f));
}

static void test_does_not_overshoot(void)
{
    TEST_ASSERT_EQUAL_FLOAT(10.0f, ramp_step(9.5f, 10.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, ramp_step(-9.5f, -10.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, ramp_step(3.0f, 3.0f, 1.0f));
}

static void test_reaches_target_in_expected_steps(void)
{
    float x = 0.0f;
    int steps = 0;
    while (x != 5.0f)
    {
        x = ramp_step(x, 5.0f, 0.5f);
        steps++;
    }
    TEST_ASSERT_EQUAL_INT(10, steps);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_moves_by_step);
    RUN_TEST(test_does_not_overshoot);
    RUN_TEST(test_reaches_target_in_expected_steps);
    return UNITY_END();
}
