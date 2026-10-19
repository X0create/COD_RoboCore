/** @file motor_bench.h
 * 单电机定长速度试验，纯逻辑。输出轴 rad/s、N·m；前馈和 PI 共用总输出抗积分饱和。
 * 下→中→上启动一次；故障 / 完成后必须重新下→中。电脑只记录，不参与运动授权。
 */
#pragma once
#include "02_devices/motor/motor.h"
#include "02_devices/remote/dr16.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    float inertia_kg_m2, fc_pos_nm, fc_neg_nm, b_pos, b_neg;
    float tc_s[3];
    float torque_limit_nm;
    float low_rad_s, high_rad_s, accel_rad_s2, settle_s, hold_s, moving_min_rad_s;
    float arm_speed_rad_s, speed_limit_rad_s;
    float start_temperature_c, stop_temperature_c;
    float max_period_s, max_exec_s, max_tracking_error_rad_s;
} MotorBenchConfig;

/** USB 可调的本次参数。限幅单位 N·m，其余为输出轴 SI 单位；只允许收紧配置边界。 */
typedef struct
{
    float torque_limit_nm, low_rad_s, high_rad_s, accel_rad_s2, speed_limit_rad_s;
    float start_temperature_c, stop_temperature_c;
} MotorBenchParameters;

/** 由停止态参数接口整组修改；allow_motion 默认 false，启动仍须操作 DR16。 */
typedef struct
{
    bool allow_motion, feedforward;
    int32_t direction; /* +1 / -1，方向对应辨识数据，不更改设备的 direction */
    uint32_t gain;     /* tc_s 的下标，运行开始时冻结 */
} MotorBenchSettings;

typedef enum
{
    MOTOR_BENCH_LOCKED,
    MOTOR_BENCH_READY,
    MOTOR_BENCH_RUNNING,
    MOTOR_BENCH_DONE,
    MOTOR_BENCH_FAULT
} MotorBenchMode;
typedef enum
{
    MOTOR_BENCH_OK,
    MOTOR_BENCH_RC_LOST,
    MOTOR_BENCH_MOTOR_LOST,
    MOTOR_BENCH_DRIVER_ERROR,
    MOTOR_BENCH_OVERSPEED,
    MOTOR_BENCH_TEMPERATURE,
    MOTOR_BENCH_LATE,
    MOTOR_BENCH_TX_FAILED,
    MOTOR_BENCH_TRACKING,
    MOTOR_BENCH_BAD_INPUT,
    MOTOR_BENCH_BAD_SETTINGS
} MotorBenchFault;

typedef struct
{
    MotorFeedback feedback;
    bool rc_online, tx_failed;
    RcSwitch sw;
    float dt_s, previous_exec_s;
    MotorBenchSettings settings;
} MotorBenchInput;

typedef struct
{
    const MotorBenchConfig *cfg;
    const MotorBenchConfig *bounds;
    MotorBenchConfig config;
    MotorBenchSettings settings;
    MotorBenchMode mode;
    MotorBenchFault fault;
    MotorBenchSettings active;
    bool down_seen, ff_active, saturated;
    uint32_t phase;
    float elapsed_s, reference_rad_s, acceleration_rad_s2;
    float kp, ki, integral_nm, pi_nm, friction_nm, inertia_nm, requested_nm, torque_nm;
} MotorBench;

/** 配置边界检查；配置必须始终有效。上电锁定。 */
bool motor_bench_init(MotorBench *self, const MotorBenchConfig *cfg);
/** 仅控制任务调用。必须遥控下档、反馈新鲜且轴已停；运行中拒绝，失败不改变任何参数。 */
bool motor_bench_configure(MotorBench *self, const MotorBenchParameters *parameters,
                           const MotorBenchSettings *settings, const MotorBenchInput *input);
/** 读取当前实际参数；仅控制任务调用。 */
MotorBenchParameters motor_bench_parameters(const MotorBench *self);
/** @pre 设备快照已校验；每拍输入真实间隔及上一拍执行时间 / 提交失败。 */
void motor_bench_update(MotorBench *self, const MotorBenchInput *input);
/** 唯一电机输出；非运行态每拍明确请求零力矩，随后由调用者 motor_group_send。 */
void motor_bench_apply(const MotorBench *self, Motor *motor);

#ifdef __cplusplus
}
#endif
