/**
 * @file    robot.c
 * @brief   新兵种的样板：遥控控制一台 M3508 的转速（照搬 COD-H7-Template `Control_Task.c`）+ 安全门
 * @note    control 任务 1 kHz：读遥控 → 安全门 → 速度环 → 全车停改写 → 电机组发送。
 *          **这个固件会给电机发指令**：未解锁时持续发 0 电流；解锁后按遥控通道 3 转动。上板按台架条件
 *          （docs/VERIFICATION_TODO.md“接电机”）。
 *          解锁：右拨杆拨到下再拨到中或上；急停：右拨杆拨到下（config.h，ADR 0032）。
 *          心跳任务驱动状态灯，每秒通过 RTT 打印模式、遥控和电机反馈；上线 / 离线的变化由 daemon 任务打印。
 */
#include "robot.h"

#include "algorithm/control/pid.h"
#include "comm_rx.h"
#include "config.h"
#include "control_task.h"
#include "core/log/log.h"
#include "core/os/os.h"
#include "daemon.h"
#include "devices/motor/motor.h"
#include "devices/motor/motor_group.h"
#include "devices/remote/dr16.h"
#include "msgs/rc_state.h"
#include "platform/can.h"
#include "platform/status_led.h"
#include "platform/time.h"
#include "platform/uart.h"
#include "safety_gate.h"

/* FreeRTOS 优先级，数字越大越高；本兵种全部任务的优先级只写在这里 */
enum
{
    PRIORITY_HEARTBEAT = 1,
    PRIORITY_DAEMON = 2,
    PRIORITY_CONTROL = 3,
    PRIORITY_COMM_RX = 4, /* 高于 control，保证控制周期读到最新反馈（《架构设计》任务划分） */
};

#define HEARTBEAT_STEP_MS     25u
#define HEARTBEAT_STEPS       40u /* 40 × 25 ms = 1 s 一拍 */
#define HEARTBEAT_STACK_WORDS 256u
#define LED_GREEN_LEVEL       0x20u /* WS2812 满亮度很刺眼，1/8 亮度足够看清 */

/* DR16 接收机接 UART5（接线沿用 COD-H7-Template，《架构设计》“阶段 1 清单”） */
#define DBUS_UART UART_5

/* 话题实例 */
static RcStateTopic rc_state; /* 发布：dr16    读取：control（安全门、速度目标）、心跳任务 */

static Dr16 dr16;

/* 电机：配置是 const，运行状态单独存放（《架构设计》“配置和运行状态分开存放”） */
static const MotorConfig chassis_motor_config = {
    .name = "m3508_1",
    .type = MOTOR_M3508,
    .can_bus = CAN_BUS_1,
    .id = 1u,
    .direction = 1,
    .gear_ratio = DJI_M3508_GEAR_RATIO,
    .stop_action = SAFE_ACTION_ZERO_TORQUE,
};
static Motor chassis_motor;
static MotorGroup motors;

/* 以下只在 control 任务里读写（gate.mode 另由心跳任务读来打印） */
static SafetyGate gate;
static Pid speed_pid;
static RmTask heartbeat_task;
static StackType_t heartbeat_stack[HEARTBEAT_STACK_WORDS];

static void on_dbus_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    dr16_on_bytes((Dr16 *)ctx, data, len, now_us);
}

/*
 * 绿色每拍闪两下表示正常：0–50 ms 亮一次，200–225 ms 再亮一次，其余时间灭。
 * 两次亮的时长不同（50 ms、25 ms，用户 2026-09-28 指定），一长一短容易和其他闪烁码区分。
 */
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
    RM_LOG_I("rc ch %d %d %d %d %d, sw %d %d, bad frames %u", rc.ch[0], rc.ch[1], rc.ch[2],
             rc.ch[3], rc.ch[4], (int)rc.sw[0], (int)rc.sw[1], (unsigned)dr16.bad_frames);
}

/* 浮点用整数打印（RTT 的 printf 不支持 %f）：毫弧度、毫弧度/秒、毫牛·米 */
static void log_motor(void)
{
    MotorFeedback fb;
    if (!motor_read_feedback(&chassis_motor, &fb))
    {
        RM_LOG_I("m3508_1 offline");
        return;
    }
    RM_LOG_I("m3508_1 angle %d mrad, speed %d mrad/s, torque %d mNm, %d C",
             (int)(fb.angle_rad * 1000.0f), (int)(fb.speed_rad_s * 1000.0f),
             (int)(fb.torque_nm * 1000.0f), (int)fb.temperature_c);
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

static void heartbeat_entry(void *arg)
{
    (void)arg;
    uint32_t beat = 0u;
    uint32_t step = 0u;

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        rm_status_led_set(0u, led_on_at(step) ? LED_GREEN_LEVEL : 0u, 0u);

        if (step == 0u)
        {
            /* 每次打印都会读一次时间，同时保证 DWT 扩展计数至少每 7.8 s 更新一次（time.h 的 @pre） */
            RM_LOG_I("alive %u, mode %s", (unsigned)beat, mode_name(gate.mode));
            log_rc();
            log_motor();
            beat++;
        }

        step = (step + 1u) % HEARTBEAT_STEPS;
        rm_task_delay_until(&last_wake, HEARTBEAT_STEP_MS);
    }
}

bool robot_init(void)
{
    if (!dr16_init(&dr16, &rc_state) || !comm_rx_add_uart(DBUS_UART, on_dbus_bytes, &dr16))
    {
        RM_LOG_E("dr16 init failed");
        return false;
    }
    const Motor *conflict;
    if (!motor_init(&chassis_motor, &chassis_motor_config, &motors, &conflict))
    {
        RM_LOG_E("motor %s init failed%s%s", chassis_motor_config.name,
                 conflict != NULL ? ": id conflict with " : "",
                 conflict != NULL ? conflict->cfg->name : "");
        return false;
    }
    safety_gate_init(&gate, TEMPLATE_ARM_SWITCH);
    const PidParam speed_param = TEMPLATE_SPEED_PID_PARAM;
    pid_init(&speed_pid, PID_POSITION, &speed_param);
    return true;
}

void robot_start(void)
{
    safety_gate_set_system_ready(&gate);
}

void robot_control_step(void)
{
    const uint64_t now_us = rm_time_now_us();

    /* 1. 一次性读输入快照 */
    RcState rc;
    const bool rc_online = rc_state_read(&rc_state, &rc, RC_LOST_TIMEOUT_MS);
    MotorFeedback fb;
    const bool motor_online = motor_read_feedback(&chassis_motor, &fb);

    /* 2. 安全门：遥控丢失、急停、未解锁 → 全车停 */
    const SafetyDecision gate_out = safety_gate_update(&gate, rc_online ? &rc : NULL, now_us);

    /* 3. 速度环（旧工程 Control_Task）。全车停或电机离线（机构停）时清积分，不写指令：
     *    槽位没写就填零力矩，恢复时从零开始，不会因积分猛冲 */
    if (gate_out.stop_all || !motor_online)
    {
        pid_reset(&speed_pid);
    }
    else /* 不全车停时本周期遥控一定在线（安全门保证），rc 有效 */
    {
        const float target_rad_s = (float)rc.ch[3] * TEMPLATE_SPEED_PER_CH;
        const float limit = speed_pid.param.output_limit * safety_gate_output_scale(&gate, now_us);
        float torque_nm = pid_calc(&speed_pid, target_rad_s, fb.speed_rad_s);
        torque_nm = (torque_nm > limit) ? limit : ((torque_nm < -limit) ? -limit : torque_nm);
        motor_set_torque(&chassis_motor, torque_nm);
    }

    /* 4. 全车停在发送出口统一执行：即使上面漏判，也不会发出运动指令 */
    if (gate_out.stop_all)
    {
        motor_group_apply_stop_all(&motors);
    }
    motor_group_flush(&motors);
}

void robot_create_tasks(void)
{
    comm_rx_create_task(PRIORITY_COMM_RX);
    control_task_create(PRIORITY_CONTROL);
    daemon_create_task(PRIORITY_DAEMON);
    if (!rm_task_create(&heartbeat_task, "heartbeat", heartbeat_entry, NULL, PRIORITY_HEARTBEAT,
                        heartbeat_stack, HEARTBEAT_STACK_WORDS))
    {
        RM_LOG_E("create heartbeat task failed");
    }
}
