/**
 * @file    robot.c
 * @brief   步兵（第一版：只有底盘）：有哪些设备、怎么初始化、每 1 ms 做什么
 * @note    control 任务每 1 ms 调用 robot_control_step()：
 *            1. 读遥控和 IMU
 *            2. 安全门判断能不能动（急停、遥控丢失、未解锁、IMU 未就绪 → 全车停）
 *            3. 摇杆 → 目标底盘速度 → 底盘子系统（四轮全向轮，subsystems/chassis）
 *            4. 全车停时把所有电机改成零力矩，然后统一发送
 *          第一版底盘直接读遥控（ADR 0043）。
 *          **这个固件会给电机发指令**：上板时车架空、轮子离地（docs/VERIFICATION_TODO.md V45 起）。
 *          解锁：右拨杆拨到下再拨到中或上；急停：右拨杆拨到下（ADR 0032）。
 *          状态灯、蜂鸣器、RTT 打印在 debug.c。
 */
#include "robot.h"

#include "comm_rx.h"
#include "config.h"
#include "control_task.h"
#include "core/log/log.h"
#include "core/os/delay.h"
#include "core/os/os.h"
#include "daemon.h"
#include "debug.h"
#include "devices/motor/motor_group.h"
#include "devices/remote/dr16.h"
#include "platform/adc.h"
#include "platform/time.h"
#include "safety_gate.h"
#include "subsystems/chassis/chassis.h"
#include "subsystems/ins/ins.h"

/* FreeRTOS 优先级，数字越大越高；本兵种全部任务的优先级只写在这里 */
enum
{
    PRIORITY_HEARTBEAT = 1,
    PRIORITY_DAEMON = 2,
    PRIORITY_CONTROL = 3,
    PRIORITY_COMM_RX = 4, /* 高于 control，保证控制周期读到最新反馈 */
    PRIORITY_INS = 5,     /* 最高：IMU 采样时刻要准 */
};

#define DBUS_UART       UART_5 /* DR16 接收机（接线沿用 COD-H7-Template） */
#define INS_STACK_WORDS 1024u /* EKF 的矩阵运算在栈上有临时变量，按预算表给 4 KB */
#define INS_RETRY_MS    1000u
#define CONTROL_DT_S    ((float)CONTROL_PERIOD_MS * 0.001f)

/* ---------------- 话题：谁发布、谁读取 ---------------- */
static RcStateTopic rc_state;   /* 发布：dr16  读取：control、debug */
static ImuStateTopic imu_state; /* 发布：ins   读取：control、debug */

/* ---------------- 设备 ---------------- */
static Dr16 dr16;

/* 四个驱动轮：顺序 = 轮 0–3（左前、左后、右后、右前），ID 按实车接线改 */
#define WHEEL(wheel_name, esc_id)                                                                  \
    {                                                                                              \
        .name = (wheel_name), .type = MOTOR_M3508, .can_bus = INFANTRY_WHEEL_CAN_BUS,              \
        .id = (esc_id), .direction = INFANTRY_WHEEL_DIRECTION, .gear_ratio = DJI_M3508_GEAR_RATIO, \
        .stop_action = SAFE_ACTION_ZERO_TORQUE                                                     \
    }
static const MotorConfig wheel_config[CHASSIS_WHEELS] = {
    WHEEL("wheel_lf", 1u),
    WHEEL("wheel_lb", 2u),
    WHEEL("wheel_rb", 3u),
    WHEEL("wheel_rf", 4u),
};
static Motor wheel[CHASSIS_WHEELS];
static MotorGroup motors;

/* ---------------- 子系统与安全门（只在 control 任务里写） ---------------- */
static SafetyGate gate;
static const ChassisConfig chassis_config = INFANTRY_CHASSIS_CONFIG;
static Chassis chassis;

static const InsConfig ins_config = { .install_rotation = INFANTRY_IMU_INSTALL_ROTATION };
static Ins ins;
static RmTask ins_task;
static StackType_t ins_stack[INS_STACK_WORDS];

static const DebugView debug_view = {
    .gate = &gate,
    .rc_state = &rc_state,
    .imu_state = &imu_state,
    .dr16 = &dr16,
    .chassis = &chassis,
    .wheel = wheel,
};

/* ================================================================== */
/* 每 1 ms：control 任务                                                */
/* ================================================================== */

/* 摇杆 → 目标底盘速度（通道对应见 config.h）。摇杆向右为正，底盘向左、逆时针为正，所以左右和旋转取反 */
static ChassisVel chassis_cmd_from_rc(const RcState *rc)
{
    const float k = 1.0f / (float)RC_CH_MAX;
    return (ChassisVel){ .vx_m_s = (float)rc->ch[3] * k * INFANTRY_MAX_VX_M_S,
                         .vy_m_s = -(float)rc->ch[2] * k * INFANTRY_MAX_VY_M_S,
                         .wz_rad_s = -(float)rc->ch[0] * k * INFANTRY_MAX_WZ_RAD_S };
}

void robot_control_step(void)
{
    const uint64_t now_us = rm_time_now_us();

    /* 1. 读遥控和 IMU */
    RcState rc;
    const bool rc_online = rc_state_read(&rc_state, &rc, RC_LOST_TIMEOUT_MS);
    ImuState imu;
    const bool imu_ready = imu_state_read(&imu_state, &imu, IMU_STALE_MS);

    /* 2. 安全门：能不能动 */
    const SafetyDecision gate_out =
        safety_gate_update(&gate, rc_online ? &rc : NULL, imu_ready, now_us);

    /* 3. 底盘。能动时遥控一定在线（安全门保证），rc 有效；全车停时底盘不读目标 */
    const ChassisVel cmd = gate_out.stop_all ? (ChassisVel){ 0 } : chassis_cmd_from_rc(&rc);
    chassis_step(&chassis, &cmd, gate_out.stop_all, safety_gate_output_scale(&gate, now_us),
                 CONTROL_DT_S);

    /* 4. 全车停在发送出口统一执行：即使上面漏判，也不会发出运动指令 */
    if (gate_out.stop_all)
    {
        motor_group_apply_stop_all(&motors);
    }
    motor_group_flush(&motors);
}

/* ================================================================== */
/* 初始化与任务                                                         */
/* ================================================================== */

static void on_dbus_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    dr16_on_bytes((Dr16 *)ctx, data, len, now_us);
}

bool robot_init(void)
{
    if (!dr16_init(&dr16, &rc_state) || !comm_rx_add_uart(DBUS_UART, on_dbus_bytes, &dr16))
    {
        RM_LOG_E("dr16 init failed");
        return false;
    }

    Motor *drive[CHASSIS_WHEELS];
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        const Motor *conflict;
        if (!motor_init(&wheel[i], &wheel_config[i], &motors, &conflict))
        {
            RM_LOG_E("motor %s init failed%s%s", wheel_config[i].name,
                     conflict != NULL ? ": id conflict with " : "",
                     conflict != NULL ? conflict->cfg->name : "");
            return false;
        }
        drive[i] = &wheel[i];
    }
    if (!chassis_init(&chassis, &chassis_config, drive, NULL))
    {
        RM_LOG_E("chassis init failed: motor without torque command");
        return false;
    }

    if (!ins_init(&ins, &ins_config, &imu_state))
    {
        RM_LOG_E("ins init failed");
        return false;
    }
    safety_gate_init(&gate, INFANTRY_ARM_SWITCH);
    return true;
}

void robot_start(void)
{
    if (!adc_start())
    {
        RM_LOG_E("adc start failed"); /* 只影响低电量提示，不阻止解锁 */
    }
    safety_gate_set_system_ready(&gate);
}

static void log_ins_event(InsEvent ev, bool *failing)
{
    switch (ev)
    {
        case INS_EVENT_NONE:
            *failing = false;
            break;
        case INS_EVENT_READ_FAILED:
            if (!*failing)
            {
                RM_LOG_W("imu read failed"); /* 只在从正常变为失败时打印一次 */
            }
            *failing = true;
            break;
        case INS_EVENT_CALIBRATED:
            RM_LOG_I("gyro calibrated, imu ready");
            break;
        case INS_EVENT_CALIB_NOT_STILL:
            RM_LOG_W("gyro calibration rejected: moving, retry (keep the robot still)");
            break;
        case INS_EVENT_CALIB_BIAS_TOO_LARGE:
            RM_LOG_W("gyro calibration rejected: bias too large, retry");
            break;
    }
}

/* ins 任务 1 kHz：BMI088 → 零偏标定 → EKF → 发布 imu_state（标定完成前不发布，安全门据此全车停） */
static void ins_entry(void *arg)
{
    (void)arg;
    Bmi088Status status;
    while ((status = ins_start(&ins)) != BMI088_OK)
    {
        RM_LOG_E("bmi088 init failed (%d), retry", (int)status);
        rm_delay_ms(INS_RETRY_MS);
    }
    RM_LOG_I("bmi088 ready, calibrating gyro (keep still %u ms)", (unsigned)INS_CALIB_SAMPLES);

    bool failing = false;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        log_ins_event(ins_step(&ins), &failing);
        rm_task_delay_until(&last_wake, 1u);
    }
}

void robot_create_tasks(void)
{
    if (!rm_task_create(&ins_task, "ins", ins_entry, NULL, PRIORITY_INS, ins_stack,
                        INS_STACK_WORDS))
    {
        RM_LOG_E("create ins task failed");
    }
    comm_rx_create_task(PRIORITY_COMM_RX);
    control_task_create(PRIORITY_CONTROL);
    daemon_create_task(PRIORITY_DAEMON);
    debug_create_task(PRIORITY_HEARTBEAT, &debug_view);
}
