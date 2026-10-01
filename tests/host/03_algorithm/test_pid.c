/**
 * @file    test_pid.c
 * @brief   pid 的单元测试：各项计算、限幅、死区、微分滤波、增量式、复位、抗积分饱和；
 *          除抗积分饱和（ADR 0040）外，期望值按旧工程 PID.c 的公式手算
 */
#include "03_algorithm/control/pid.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static PidParam param(float kp, float ki, float kd)
{
    return (PidParam){
        .kp = kp, .ki = ki, .kd = kd, .integral_limit = 1000.0f, .output_limit = 1000.0f
    };
}

static void test_proportional_only(void)
{
    Pid pid;
    const PidParam p = param(2.0f, 0.0f, 0.0f);
    pid_init(&pid, PID_POSITION, &p);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, pid_calc(&pid, 5.0f, 2.0f));
    TEST_ASSERT_EQUAL_FLOAT(-4.0f, pid_calc(&pid, 0.0f, 2.0f));
}

/* 积分是误差逐次累加，累加值按 integral_limit 限幅 */
static void test_integral_accumulates_and_clamps(void)
{
    Pid pid;
    PidParam p = param(0.0f, 0.5f, 0.0f);
    p.integral_limit = 4.0f;
    pid_init(&pid, PID_POSITION, &p);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, pid_calc(&pid, 3.0f, 0.0f)); /* 累加 3 */
    TEST_ASSERT_EQUAL_FLOAT(2.0f, pid_calc(&pid, 3.0f, 0.0f)); /* 累加 6，限幅到 4 */
    TEST_ASSERT_EQUAL_FLOAT(2.0f, pid_calc(&pid, 3.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, pid_calc(&pid, -3.0f, 0.0f)); /* 4 - 3 = 1 */
}

/* 旧工程加热环的参数 integral_limit = 0：积分项恒为 0，保留这个行为 */
static void test_zero_integral_limit_disables_integral(void)
{
    Pid pid;
    PidParam p = param(0.0f, 20.0f, 0.0f);
    p.integral_limit = 0.0f;
    pid_init(&pid, PID_POSITION, &p);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid_calc(&pid, 40.0f, 30.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid_calc(&pid, 40.0f, 30.0f));
}

static void test_derivative_is_error_difference(void)
{
    Pid pid;
    const PidParam p = param(0.0f, 0.0f, 1.0f);
    pid_init(&pid, PID_POSITION, &p);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, pid_calc(&pid, 2.0f, 0.0f)); /* 2 - 0 */
    TEST_ASSERT_EQUAL_FLOAT(3.0f, pid_calc(&pid, 5.0f, 0.0f)); /* 5 - 2 */
}

/* 第一次微分值直接作为滤波输出，之后 y = a * y_prev + (1 - a) * x */
static void test_derivative_filter(void)
{
    Pid pid;
    PidParam p = param(0.0f, 0.0f, 1.0f);
    p.d_alpha = 0.5f;
    pid_init(&pid, PID_POSITION, &p);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, pid_calc(&pid, 2.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, pid_calc(&pid, 5.0f, 0.0f)); /* 0.5 * 2 + 0.5 * 3 */
}

static void test_output_clamped(void)
{
    Pid pid;
    PidParam p = param(10.0f, 0.0f, 0.0f);
    p.output_limit = 25.0f;
    pid_init(&pid, PID_POSITION, &p);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, pid_calc(&pid, 3.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(-25.0f, pid_calc(&pid, -3.0f, 0.0f));
}

/* 误差进入死区后输出保持上一次的值，不归零 */
/* 抗积分饱和：输出顶到限幅、误差同向时积分不再累加；误差一反向，输出立刻跟着反向（ADR 0040） */
static void test_integral_frozen_while_saturated(void)
{
    Pid pid;
    PidParam p = param(1.0f, 0.5f, 0.0f);
    p.output_limit = 10.0f;
    pid_init(&pid, PID_POSITION, &p);
    for (int i = 0; i < 50; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(10.0f, pid_calc(&pid, 100.0f, 0.0f)); /* P 项 100 已饱和 */
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.integral); /* 没有攒积分 */
    /* 冲过目标 2：旧行为积分已攒到上千，输出仍顶在 +10；现在 = P(-2) + I(0.5 × -2) */
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, pid_calc(&pid, 100.0f, 102.0f));
}

/* 没饱和时照常累加；会越限时只补到边界；误差反向（往回拉）时照常累加 */
static void test_integral_accumulates_when_unwinding(void)
{
    Pid pid;
    PidParam p = param(0.0f, 1.0f, 0.0f);
    p.output_limit = 5.0f;
    pid_init(&pid, PID_POSITION, &p);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, pid_calc(&pid, 4.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, pid_calc(&pid, 4.0f, 0.0f)); /* 累加到 8 会越限：只补到 5 */
    TEST_ASSERT_EQUAL_FLOAT(5.0f, pid.integral);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, pid_calc(&pid, 0.0f, 3.0f)); /* 误差 -3：5 - 3 = 2 */
}

static void test_deadband_holds_output(void)
{
    Pid pid;
    PidParam p = param(2.0f, 0.0f, 0.0f);
    p.deadband = 1.0f;
    pid_init(&pid, PID_POSITION, &p);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, pid_calc(&pid, 3.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, pid_calc(&pid, 0.5f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, pid_calc(&pid, 1.0f, 0.0f)); /* 等于死区时照常计算 */
}

/* 增量式：输出 += kp * Δe + ki * e + kd * Δ²e */
static void test_incremental(void)
{
    Pid pid;
    const PidParam p = param(1.0f, 0.5f, 0.0f);
    pid_init(&pid, PID_INCREMENTAL, &p);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, pid_calc(&pid, 2.0f, 0.0f)); /* 1 * 2 + 0.5 * 2 */
    TEST_ASSERT_EQUAL_FLOAT(4.0f, pid_calc(&pid, 2.0f, 0.0f)); /* 3 + 0 + 1 */
    TEST_ASSERT_EQUAL_FLOAT(2.0f, pid_calc(&pid, 0.0f, 0.0f)); /* 4 + 1 * (0 - 2) + 0.5 * 0 */
}

static void test_reset_clears_state(void)
{
    Pid pid;
    const PidParam p = param(1.0f, 1.0f, 1.0f);
    pid_init(&pid, PID_POSITION, &p);
    pid_calc(&pid, 5.0f, 0.0f);
    pid_calc(&pid, 5.0f, 0.0f);
    pid_reset(&pid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.output);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.integral);
    /* 复位后与刚初始化时的第一次计算相同：P 1 + I 1 + D 1 */
    TEST_ASSERT_EQUAL_FLOAT(3.0f, pid_calc(&pid, 1.0f, 0.0f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_proportional_only);
    RUN_TEST(test_integral_accumulates_and_clamps);
    RUN_TEST(test_zero_integral_limit_disables_integral);
    RUN_TEST(test_derivative_is_error_difference);
    RUN_TEST(test_derivative_filter);
    RUN_TEST(test_output_clamped);
    RUN_TEST(test_integral_frozen_while_saturated);
    RUN_TEST(test_integral_accumulates_when_unwinding);
    RUN_TEST(test_deadband_holds_output);
    RUN_TEST(test_incremental);
    RUN_TEST(test_reset_clears_state);
    return UNITY_END();
}
