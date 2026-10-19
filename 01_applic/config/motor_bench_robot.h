/** MC02 + M3508/C620 单电机试验配置。仅 h723-m3508 选用。
 * 模型 / 候选来源：辨识仓库 docs/cn/results/m3508_control_candidate.md #72。
 * 运动默认禁止；限流 / 超期 / 超速为待现场审核的试验设置，不是经过验证的硬件能力。
 */
#pragma once
#include "01_applic/modules/motor_bench/motor_bench.h"
#include "01_applic/modules/motor_bench/motor_bench_link.h"

static const MotorConfig bench_motor_config = { .name = "m3508_bench",
                                                .type = MOTOR_M3508,
                                                .can_bus = CAN_BUS_1,
                                                .id = 1u,
                                                .direction = 1,
                                                .gear_ratio = DJI_M3508_GEAR_RATIO,
                                                .stop_action = STOP_ACTION_ZERO_TORQUE };
static const MotorBenchConfig motor_bench_config = {
    .inertia_kg_m2 = 0.00460461018f,
    .fc_pos_nm = 0.08464913f,
    .fc_neg_nm = 0.08962476f,
    .b_pos = 0.010802525f,
    .b_neg = 0.004435989f,
    .tc_s = { 0.05f, 0.10f, 0.20f },
    .torque_limit_nm = 1.5f * DJI_M3508_NM_PER_A,
    .low_rad_s = 4.0f,
    .high_rad_s = 6.0f,
    .accel_rad_s2 = 5.0f,
    .settle_s = 2.0f,
    .hold_s = 1.2f,
    .moving_min_rad_s = 3.0f,
    .arm_speed_rad_s = 0.1f,
    .speed_limit_rad_s = 10.0f,
    /* 用户 2026-10-19 确认的保护边界；模型温度覆盖仍仅见候选文档。 */
    .start_temperature_c = 50.0f,
    .stop_temperature_c = 70.0f,
    .max_period_s = 0.0015f,
    .max_exec_s = 0.001f,
    .max_tracking_error_rad_s = 2.0f
};
#define MOTOR_BENCH_SWITCH    1u
#define MOTOR_BENCH_PERIOD_MS 1u
#define MOTOR_BENCH_PERIOD_S  (0.001f * (float)MOTOR_BENCH_PERIOD_MS)
extern Motor bench_motor;
extern MotorGroup bench_motors;
extern Dr16 bench_rc;
extern MotorBench motor_bench;
extern MotorBenchLink bench_link;
bool motor_bench_log_init(void);
