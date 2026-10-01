/**
 * @file    test_bench_config.c
 * @brief   台架验证固件（app/bench）速度环参数换算的等价性：旧工程原始单位的 PID 与换算到国际单位的 PID，在同一个被控对象上闭环，
 *          每一步的输出（换回电流原始值）一致，包括积分限幅和输出限幅起作用的阶段
 */
#include "app/bench/config.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* 一个简单的一阶被控对象：转子 rpm 由电流原始值驱动（参数随意，只要两边相同） */
static float plant(float rpm, float current_raw)
{
    return rpm + 0.02f * current_raw - 0.01f * rpm;
}

static void test_si_pid_matches_old_raw_pid(void)
{
    const PidParam old_param = {
        .kp = 13.0f, .ki = 0.1f, .integral_limit = 5000.0f, .output_limit = 12000.0f
    };
    const PidParam si_param = BENCH_SPEED_PID_PARAM;
    Pid old_pid, si_pid;
    pid_init(&old_pid, PID_POSITION, &old_param);
    pid_init(&si_pid, PID_POSITION, &si_param);

    float rpm_old = 0.0f;
    float rpm_si = 0.0f;
    bool saturated = false;
    for (int k = 0; k < 3000; k++)
    {
        /* 遥控通道：满杆正转 → 回中 → 半杆反转 */
        const int16_t ch = (k < 1000) ? 660 : ((k < 2000) ? 0 : -330);

        const float out_old = pid_calc(&old_pid, (float)ch * 5.0f, rpm_old);
        const float out_si =
            pid_calc(&si_pid, (float)ch * BENCH_SPEED_PER_CH, rpm_si / BENCH_RPM_PER_RAD_S);
        const float out_si_raw = out_si * BENCH_RAW_PER_NM;

        TEST_ASSERT_FLOAT_WITHIN(1.0f, out_old, out_si_raw); /* 1 个原始单位 ≈ 量程的 0.006% */
        saturated = saturated || out_old >= 12000.0f;

        rpm_old = plant(rpm_old, out_old);
        rpm_si = plant(rpm_si, out_si_raw);
    }
    TEST_ASSERT_TRUE(saturated); /* 确实覆盖了输出限幅 */
}

static void test_converted_values(void)
{
    const PidParam p = BENCH_SPEED_PID_PARAM;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.8729f, p.kp);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 4.3945f, p.output_limit);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 27.27f, p.integral_limit);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.027266f, BENCH_SPEED_PER_CH);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_si_pid_matches_old_raw_pid);
    RUN_TEST(test_converted_values);
    return UNITY_END();
}
