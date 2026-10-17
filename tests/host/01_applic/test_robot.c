/**
 * @file    test_robot.c
 * @brief   驱动轮速度环参数换算的等价性：原始单位（rpm 进、电流原始值出）的 PID 与
 *          robot.h 里换算到国际单位的 PID，在同一个被控对象上闭环，每一步的输出（换回电流原始值）一致，
 *          包括积分限幅和输出限幅起作用的阶段
 */
#include "01_applic/config/robot.h"

#include <math.h>

#include "tests/host/fakes/fake_time.h"
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
    const PidParam si_param = chassis_config.drive_speed_pid;
    Pid old_pid, si_pid;
    pid_init(&old_pid, PID_POSITION, &old_param);
    pid_init(&si_pid, PID_POSITION, &si_param);

    float rpm_old = 0.0f;
    float rpm_si = 0.0f;
    bool saturated = false;
    for (int k = 0; k < 3000; k++)
    {
        /* 目标转子转速：正转 → 停 → 反转（单位 rpm，两边按同一比例换算） */
        const float target_rpm = (k < 1000) ? 3300.0f : ((k < 2000) ? 0.0f : -1650.0f);

        const float out_old = pid_step(&old_pid, target_rpm, rpm_old);
        const float out_si = pid_step(&si_pid, target_rpm / DJI_M3508_RPM_PER_RAD_S,
                                      rpm_si / DJI_M3508_RPM_PER_RAD_S);
        const float out_si_raw = out_si * DJI_M3508_RAW_PER_NM;

        TEST_ASSERT_FLOAT_WITHIN(1.0f, out_old, out_si_raw); /* 1 个原始单位 ≈ 量程的 0.006% */
        saturated = saturated || out_old >= 12000.0f;

        rpm_old = plant(rpm_old, out_old);
        rpm_si = plant(rpm_si, out_si_raw);
    }
    TEST_ASSERT_TRUE(saturated); /* 确实覆盖了输出限幅 */
}

static void test_converted_values(void)
{
    const PidParam p = chassis_config.drive_speed_pid;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.8729f, p.kp);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 4.3945f, p.output_limit);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 27.27f, p.integral_limit);
}

/* 使用固件真正 include 的配置，覆盖停、前进、横移和旋转，防止 type 和几何表再次错配 */
static void test_actual_chassis_config_produces_finite_outputs(void)
{
    static Motor motor[CHASSIS_WHEELS];
    Motor *drive[CHASSIS_WHEELS];
    MotorGroup group = { 0 };
    fake_time_set_us(1000000u);
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        const Motor *conflict;
        drive[i] = &motor[i];
        TEST_ASSERT_TRUE(motor_init(drive[i], &wheel_config[i], &group, &conflict));
        const CanFrame frame = { .id = 0x201u + i, .len = 8u, .stamp_us = 1000000u };
        TEST_ASSERT_TRUE(motor_group_receive(&group, wheel_config[i].can_bus, &frame));
    }
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &chassis_config, drive, NULL));
    const ChassisVel commands[] = {
        { 0 }, { .vx_m_s = 0.1f }, { .vy_m_s = -0.1f }, { .wz_rad_s = 0.2f }
    };
    for (unsigned k = 0u; k < sizeof(commands) / sizeof(commands[0]); k++)
    {
        chassis_measure_update(&c);
        chassis_target_update(&c, &commands[k], false);
        chassis_output_update(&c, 1.0f);
        for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
        {
            TEST_ASSERT_TRUE(isfinite(c.target.drive_speed_rad_s[i]));
            TEST_ASSERT_TRUE(drive[i]->torque_set);
            TEST_ASSERT_TRUE(isfinite(drive[i]->torque_cmd_nm));
        }
        motor_group_send(&group);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_actual_chassis_config_produces_finite_outputs);
    RUN_TEST(test_si_pid_matches_old_raw_pid);
    RUN_TEST(test_converted_values);
    return UNITY_END();
}
