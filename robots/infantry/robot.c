/**
 * @file    robot.c
 * @brief   步兵（第一版：只有底盘）：遥控 → 底盘速度 → subsystems/chassis（四轮全向轮）+ 安全门
 * @note    control 任务 1 kHz：读遥控 → 安全门 → 底盘 → 全车停改写 → 电机组发送。
 *          第一版底盘直接读遥控（ADR 0043）；OperatorInput / RobotCmd / command 任务等云台加入时再做（ADR 0041）。
 *          **这个固件会给电机发指令**：未解锁时持续发 0 电流；解锁后按摇杆驱动四个轮子。
 *          上板按台架条件：车架空、轮子离地（docs/VERIFICATION_TODO.md“接电机”）。
 *          解锁：右拨杆拨到下再拨到中或上；急停：右拨杆拨到下（config.h，ADR 0032）。
 *          ins 任务 1 kHz：subsystems/ins，标定完成后才发布 imu_state；IMU 未就绪时安全门全车停。
 *          心跳任务驱动状态灯和蜂鸣器，检查电池电压，每秒通过 RTT 打印模式、遥控、四个轮子、底盘目标、IMU 和电池。
 */
#include "robot.h"

#include "comm_rx.h"
#include "config.h"
#include "control_task.h"
#include "core/log/log.h"
#include "core/os/delay.h"
#include "core/os/os.h"
#include "daemon.h"
#include "devices/battery/battery.h"
#include "devices/buzzer/buzzer.h"
#include "devices/motor/motor.h"
#include "devices/motor/motor_group.h"
#include "devices/remote/dr16.h"
#include "msgs/imu_state.h"
#include "msgs/rc_state.h"
#include "platform/adc.h"
#include "platform/can.h"
#include "platform/status_led.h"
#include "platform/time.h"
#include "platform/uart.h"
#include "safety_gate.h"
#include "subsystems/chassis/chassis.h"
#include "subsystems/ins/ins.h"

/* FreeRTOS 优先级，数字越大越高；本兵种全部任务的优先级只写在这里 */
enum
{
    PRIORITY_HEARTBEAT = 1,
    PRIORITY_DAEMON = 2,
    PRIORITY_CONTROL = 3,
    PRIORITY_COMM_RX = 4, /* 高于 control，保证控制周期读到最新反馈（《架构设计》任务划分） */
    PRIORITY_INS = 5, /* 最高：IMU 采样时刻要准（《架构设计》任务划分） */
};

#define HEARTBEAT_STEP_MS      25u
#define HEARTBEAT_STEPS        40u /* 40 × 25 ms = 1 s 一拍 */
#define HEARTBEAT_STACK_WORDS  256u
#define LED_GREEN_LEVEL        0x20u /* WS2812 满亮度很刺眼，1/8 亮度足够看清 */
#define INS_STACK_WORDS        1024u /* EKF 的矩阵运算在栈上有临时变量，按预算表给 4 KB */
#define INS_RETRY_MS           1000u
#define LOW_BATTERY_BEEP_STEPS 80u /* 80 × 25 ms = 2 s 响一次 */
#define CONTROL_DT_S           ((float)CONTROL_PERIOD_MS * 0.001f)

/* DR16 接收机接 UART5（接线沿用 COD-H7-Template） */
#define DBUS_UART UART_5

/* 话题实例 */
static RcStateTopic rc_state; /* 发布：dr16  读取：control（安全门、底盘目标）、心跳任务 */
static ImuStateTopic imu_state; /* 发布：ins   读取：control（安全门）、心跳任务 */

static Dr16 dr16;

/* 驱动轮电机：配置是 const，运行状态单独存放（《架构设计》“配置和运行状态分开存放”）；顺序 = 轮 0–3 */
#define WHEEL_CONFIG(wheel_name, esc_id)                                                           \
    {                                                                                              \
        .name = (wheel_name), .type = MOTOR_M3508, .can_bus = INFANTRY_WHEEL_CAN_BUS,              \
        .id = (esc_id), .direction = INFANTRY_WHEEL_DIRECTION, .gear_ratio = DJI_M3508_GEAR_RATIO, \
        .stop_action = SAFE_ACTION_ZERO_TORQUE                                                     \
    }
static const MotorConfig wheel_config[CHASSIS_WHEELS] = {
    WHEEL_CONFIG("wheel_lf", 1u),
    WHEEL_CONFIG("wheel_lb", 2u),
    WHEEL_CONFIG("wheel_rb", 3u),
    WHEEL_CONFIG("wheel_rf", 4u),
};
static Motor wheel_motor[CHASSIS_WHEELS];
static MotorGroup motors;

/* 以下只在 control 任务里读写（gate.mode、chassis 的调试字段另由心跳任务读来打印） */
static SafetyGate gate;
static const ChassisConfig chassis_config = INFANTRY_CHASSIS_CONFIG;
static Chassis chassis;
static const InsConfig ins_config = { .install_rotation = INFANTRY_IMU_INSTALL_ROTATION };
static Ins ins;
static RmTask ins_task;
static StackType_t ins_stack[INS_STACK_WORDS];

static const BatteryConfig battery_config = INFANTRY_BATTERY_CONFIG;
static Battery battery; /* 以下两个只在心跳任务里用 */
static Buzzer buzzer;

static RmTask heartbeat_task;
static StackType_t heartbeat_stack[HEARTBEAT_STACK_WORDS];

static void on_dbus_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    dr16_on_bytes((Dr16 *)ctx, data, len, now_us);
}

/* 绿色每拍闪两下表示正常：0–50 ms 亮一次，200–225 ms 再亮一次（与样板相同，用户 2026-09-28 指定） */
static bool led_on_at(uint32_t step)
{
    return step < 2u || step == 8u;
}

static void log_rc(void)
{
    RcState rc;
    if (!rc_state_read(&rc_state, &rc, RC_LOST_TIMEOUT_MS))
    {
        RM_LOG_I("rc lost (bad frames %u)", (unsigned)dr16.bad_frames);
        return;
    }
    RM_LOG_I("rc ch %d %d %d %d %d, sw %d %d", rc.ch[0], rc.ch[1], rc.ch[2], rc.ch[3], rc.ch[4],
             (int)rc.sw[0], (int)rc.sw[1]);
}

/* 浮点用整数打印（RTT 的 printf 不支持 %f）：毫弧度/秒、毫米/秒、毫牛·米 */
static void log_chassis(void)
{
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        MotorFeedback fb;
        if (!motor_read_feedback(&wheel_motor[i], &fb))
        {
            RM_LOG_I("%s offline", wheel_config[i].name);
            continue;
        }
        RM_LOG_I("%s speed %d mrad/s (ref %d), torque %d mNm, %d C", wheel_config[i].name,
                 (int)(fb.speed_rad_s * 1000.0f), (int)(chassis.drive_ref_rad_s[i] * 1000.0f),
                 (int)(fb.torque_nm * 1000.0f), (int)fb.temperature_c);
    }
    RM_LOG_I("chassis ref vx %d vy %d mm/s, wz %d mrad/s%s", (int)(chassis.ref.vx_m_s * 1000.0f),
             (int)(chassis.ref.vy_m_s * 1000.0f), (int)(chassis.ref.wz_rad_s * 1000.0f),
             chassis.all_online ? "" : " (motor offline: stopping)");
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

static void log_imu(void)
{
    ImuState st;
    if (!imu_state_read(&imu_state, &st, IMU_STALE_MS))
    {
        RM_LOG_I("imu not ready");
        return;
    }
    RM_LOG_I("imu yaw %d pitch %d roll %d mrad, %d mC", (int)(st.yaw_rad * 1000.0f),
             (int)(st.pitch_rad * 1000.0f), (int)(st.roll_rad * 1000.0f),
             (int)(st.temperature_c * 1000.0f));
}

static const char *mode_name(RobotMode mode)
{
    switch (mode)
    {
        case ROBOT_MODE_INIT:
            return "init";
        case ROBOT_MODE_SAFE:
            return "safe";
        case ROBOT_MODE_MANUAL:
            return "manual";
    }
    return "?";
}

/* 电池：低电量期间每 2 s 响一次；状态变化时打印一次 */
static void check_battery(uint32_t tick)
{
    const bool was_low = battery.low;
    const bool low = battery_update(&battery, adc_read_volts(ADC_BATTERY), rm_time_now_us());
    if (low != was_low)
    {
        if (low)
        {
            RM_LOG_W("battery low: %d mV", (int)(battery.voltage_v * 1000.0f));
        }
        else
        {
            RM_LOG_I("battery ok: %d mV", (int)(battery.voltage_v * 1000.0f));
        }
    }
    if (low && tick % LOW_BATTERY_BEEP_STEPS == 0u)
    {
        buzzer_play(&buzzer, BUZZER_LOW_BATTERY);
    }
}

/* 模式变化时的提示音：进入 Manual 为解锁音，离开为上锁音 */
static void beep_on_mode_change(RobotMode *last)
{
    const RobotMode mode = gate.mode;
    if (mode != *last)
    {
        if (mode == ROBOT_MODE_MANUAL)
        {
            buzzer_play(&buzzer, BUZZER_ARM);
        }
        else if (*last == ROBOT_MODE_MANUAL)
        {
            buzzer_play(&buzzer, BUZZER_DISARM);
        }
        *last = mode;
    }
}

static void heartbeat_entry(void *arg)
{
    (void)arg;
    uint32_t beat = 0u;
    uint32_t step = 0u;
    uint32_t tick = 0u;
    RobotMode last_mode = gate.mode;

    if (buzzer_init(&buzzer))
    {
        buzzer_play(&buzzer, BUZZER_STARTUP);
    }
    else
    {
        RM_LOG_E("buzzer pwm start failed");
    }

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        rm_status_led_set(0u, led_on_at(step) ? LED_GREEN_LEVEL : 0u, 0u);
        check_battery(tick);
        beep_on_mode_change(&last_mode);
        buzzer_step(&buzzer, HEARTBEAT_STEP_MS);
        tick++;

        if (step == 0u)
        {
            /* 每次打印都会读一次时间，同时保证 DWT 扩展计数至少每 7.8 s 更新一次（time.h 的 @pre） */
            RM_LOG_I("alive %u, mode %s", (unsigned)beat, mode_name(gate.mode));
            log_rc();
            log_chassis();
            log_imu();
            RM_LOG_I("battery %d mV%s", (int)(battery.voltage_v * 1000.0f),
                     battery.low ? " (low)" : "");
            beat++;
        }

        step = (step + 1u) % HEARTBEAT_STEPS;
        rm_task_delay_until(&last_wake, HEARTBEAT_STEP_MS);
    }
}

static bool init_motor(Motor *m, const MotorConfig *cfg)
{
    const Motor *conflict;
    if (!motor_init(m, cfg, &motors, &conflict))
    {
        RM_LOG_E("motor %s init failed%s%s", cfg->name,
                 conflict != NULL ? ": id conflict with " : "",
                 conflict != NULL ? conflict->cfg->name : "");
        return false;
    }
    return true;
}

bool robot_init(void)
{
    if (!dr16_init(&dr16, &rc_state) || !comm_rx_add_uart(DBUS_UART, on_dbus_bytes, &dr16))
    {
        RM_LOG_E("dr16 init failed");
        return false;
    }
    Motor *wheels[CHASSIS_WHEELS];
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        if (!init_motor(&wheel_motor[i], &wheel_config[i]))
        {
            return false;
        }
        wheels[i] = &wheel_motor[i];
    }
    if (!chassis_init(&chassis, &chassis_config, wheels, NULL))
    {
        RM_LOG_E("chassis init failed: motor without torque command");
        return false;
    }
    if (!ins_init(&ins, &ins_config, &imu_state))
    {
        RM_LOG_E("ins init failed");
        return false;
    }
    battery_init(&battery, &battery_config);
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

/* 摇杆 → 底盘速度（通道对应见 config.h）。摇杆向右为正，底盘系向左、逆时针为正，所以左右和旋转取反 */
static ChassisVel chassis_target_from_rc(const RcState *rc)
{
    const float k = 1.0f / (float)RC_CH_MAX;
    return (ChassisVel){ .vx_m_s = (float)rc->ch[3] * k * INFANTRY_MAX_VX_M_S,
                         .vy_m_s = -(float)rc->ch[2] * k * INFANTRY_MAX_VY_M_S,
                         .wz_rad_s = -(float)rc->ch[0] * k * INFANTRY_MAX_WZ_RAD_S };
}

void robot_control_step(void)
{
    const uint64_t now_us = rm_time_now_us();

    /* 1. 一次性读输入快照 */
    RcState rc;
    const bool rc_online = rc_state_read(&rc_state, &rc, RC_LOST_TIMEOUT_MS);
    ImuState imu;
    const bool imu_ready = imu_state_read(&imu_state, &imu, IMU_STALE_MS);

    /* 2. 安全门：遥控丢失、急停、未解锁、IMU 未就绪 → 全车停 */
    const SafetyDecision gate_out =
        safety_gate_update(&gate, rc_online ? &rc : NULL, imu_ready, now_us);

    /* 3. 底盘。不全车停时本周期遥控一定在线（安全门保证），rc 有效；全车停时底盘不读目标 */
    const ChassisVel target = gate_out.stop_all ? (ChassisVel){ 0 } : chassis_target_from_rc(&rc);
    chassis_step(&chassis, &target, gate_out.stop_all, safety_gate_output_scale(&gate, now_us),
                 CONTROL_DT_S);

    /* 4. 全车停在发送出口统一执行：即使上面漏判，也不会发出运动指令 */
    if (gate_out.stop_all)
    {
        motor_group_apply_stop_all(&motors);
    }
    motor_group_flush(&motors);
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
    if (!rm_task_create(&heartbeat_task, "heartbeat", heartbeat_entry, NULL, PRIORITY_HEARTBEAT,
                        heartbeat_stack, HEARTBEAT_STACK_WORDS))
    {
        RM_LOG_E("create heartbeat task failed");
    }
}
