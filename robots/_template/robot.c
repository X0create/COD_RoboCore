/**
 * @file    robot.c
 * @brief   新兵种的样板：遥控控制一台 M3508 的转速（照搬 COD-H7-Template `Control_Task.c`）+ 安全门
 * @note    control 任务 1 kHz：读遥控 → 安全门 → 速度环 → 全车停改写 → 电机组发送。
 *          图传链路（VT13 遥控器、键鼠）接 USART10，只解析和发布，暂不参与控制（ADR 0036）。
 *          USB 虚拟串口接上位机：vision_link 找 0x5A 帧并计数；收到的字节原样回发，用电脑串口助手验证通道
 *          （视觉协议确定后去掉回发，ADR 0037）。
 *          另接一台达妙 DM8009（FDCAN2，FD；同旧工程配置）：解锁时请求使能、之后零力矩；全车停时阻尼；
 *          每个周期都发一帧（驱动器只在收到帧时回反馈）。
 *          **这个固件会给电机发指令**：未解锁时持续发 0 电流；解锁后按遥控通道 3 转动。上板按台架条件
 *          （docs/VERIFICATION_TODO.md“接电机”）。
 *          解锁：右拨杆拨到下再拨到中或上；急停：右拨杆拨到下（config.h，ADR 0032）。
 *          ins 任务 1 kHz：subsystems/ins（BMI088、上电零偏标定、EKF、加热），标定完成后才发布 imu_state；
 *          IMU 未就绪时安全门全车停。
 *          心跳任务驱动状态灯和蜂鸣器（启动音、解锁 / 上锁音、低电量每 2 s 两声），检查电池电压，
 *          每秒通过 RTT 打印模式、遥控、电机反馈、IMU 和电池；上线 / 离线的变化由 daemon 任务打印。
 */
#include "robot.h"

#include "algorithm/control/pid.h"
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
#include "devices/remote/vt_link.h"
#include "devices/vision/vision_link.h"
#include "msgs/imu_state.h"
#include "msgs/kbm_state.h"
#include "msgs/rc_state.h"
#include "msgs/vt_rc_state.h"
#include "platform/adc.h"
#include "platform/can.h"
#include "platform/status_led.h"
#include "platform/time.h"
#include "platform/uart.h"
#include "platform/usb_cdc.h"
#include "safety_gate.h"
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

#define HEARTBEAT_STEP_MS     25u
#define HEARTBEAT_STEPS       40u /* 40 × 25 ms = 1 s 一拍 */
#define HEARTBEAT_STACK_WORDS 256u
#define LED_GREEN_LEVEL       0x20u /* WS2812 满亮度很刺眼，1/8 亮度足够看清 */
#define INS_STACK_WORDS       1024u /* EKF 的矩阵运算在栈上有临时变量，按预算表给 4 KB */
#define INS_RETRY_MS          1000u

/* DR16 接收机接 UART5（接线沿用 COD-H7-Template，《架构设计》“阶段 1 清单”） */
#define DBUS_UART UART_5
/* 图传链路接 USART10（921600，ADR 0036） */
#define VT_LINK_UART UART_10

/* 话题实例 */
static RcStateTopic rc_state; /* 发布：dr16    读取：control（安全门、速度目标）、心跳任务 */
static ImuStateTopic imu_state; /* 发布：ins     读取：control（安全门）、心跳任务 */
static VtRcStateTopic vt_rc_state; /* 发布：vt_link 读取：心跳任务（以后是 command） */
static KbmStateTopic kbm_state; /* 发布：vt_link 读取：心跳任务（以后是 command） */

static Dr16 dr16;
static VtLink vt_link;
static VisionLink vision_link;
/* comm_rx 累加、心跳读（32 位读写是原子的，只用于调试统计） */
static volatile uint32_t usb_rx_bytes;
static volatile uint32_t usb_echo_dropped;

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

/* 达妙 DM8009：ID、范围同旧工程 Motor.c 的 DM_8009_Motor[0]；阻尼 Kd 为暂定值，台架确认（V41） */
static const MotorConfig joint_motor_config = {
    .name = "dm8009_1",
    .type = MOTOR_DM,
    .can_bus = CAN_BUS_2,
    .id = 0x01u,
    .direction = 1,
    .gear_ratio = 1.0f,
    .stop_action = SAFE_ACTION_DAMP,
    .dm = { .master_id = 0x11u,
            .p_max = 3.141593f,
            .v_max = 45.0f,
            .t_max = 54.0f,
            .damp_kd = 1.0f },
};
static Motor joint_motor;
static MotorGroup motors;

/* 以下只在 control 任务里读写（gate.mode 另由心跳任务读来打印） */
static SafetyGate gate;
static Pid speed_pid;
static const InsConfig ins_config = { .install_rotation = TEMPLATE_IMU_INSTALL_ROTATION };
static Ins ins;
static RmTask ins_task;
static StackType_t ins_stack[INS_STACK_WORDS];

static const BatteryConfig battery_config = TEMPLATE_BATTERY_CONFIG;
static Battery battery; /* 以下两个只在心跳任务里用 */
static Buzzer buzzer;
#define LOW_BATTERY_BEEP_STEPS 80u /* 80 × 25 ms = 2 s 响一次 */

static RmTask heartbeat_task;
static StackType_t heartbeat_stack[HEARTBEAT_STACK_WORDS];

static void on_dbus_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    dr16_on_bytes((Dr16 *)ctx, data, len, now_us);
}

/* USB 收到的数据：交给视觉链路找帧，并原样回发（验证通道用；发送忙时丢弃） */
static void on_usb_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    (void)now_us;
    vision_link_on_bytes((VisionLink *)ctx, data, len);
    usb_rx_bytes += len;
    if (!usb_cdc_write(data, len))
    {
        usb_echo_dropped++; /* 上一包还没发完：这段不回发 */
    }
}

static void on_vt_link_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    (void)now_us;
    vt_link_on_bytes((VtLink *)ctx, data, len);
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

static void log_vt_link(void)
{
    VtRcState rc;
    if (vt_rc_state_read(&vt_rc_state, &rc, VT_LINK_TIMEOUT_MS))
    {
        RM_LOG_I("vt13 ch %d %d %d %d, mode %d, pause %d", rc.ch[0], rc.ch[1], rc.ch[2], rc.ch[3],
                 (int)rc.mode, (int)rc.pause);
    }
    KbmState kbm;
    if (kbm_state_read(&kbm_state, &kbm, VT_LINK_TIMEOUT_MS))
    {
        RM_LOG_I("kbm keys 0x%x, mouse %d %d", (unsigned)kbm.keys, kbm.mouse_x, kbm.mouse_y);
    }
}

static void log_usb(void)
{
    static uint32_t last_bytes;
    const uint32_t bytes = usb_rx_bytes;
    RM_LOG_I("usb rx %u B/s, vision frames %u (last id 0x%x), rx dropped %u, echo dropped %u",
             (unsigned)(bytes - last_bytes), (unsigned)vision_link.frames,
             (unsigned)vision_link.last_id, (unsigned)usb_cdc_rx_dropped(),
             (unsigned)usb_echo_dropped);
    last_bytes = bytes;
}

static void log_joint(void)
{
    MotorFeedback fb;
    if (!motor_read_feedback(&joint_motor, &fb))
    {
        RM_LOG_I("dm8009_1 offline");
        return;
    }
    RM_LOG_I("dm8009_1 %s, error 0x%x, angle %d mrad, speed %d mrad/s, torque %d mNm",
             fb.enabled ? "enabled" : "disabled", (unsigned)fb.error_code,
             (int)(fb.angle_rad * 1000.0f), (int)(fb.speed_rad_s * 1000.0f),
             (int)(fb.torque_nm * 1000.0f));
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

/* 浮点用整数打印：毫弧度、毫摄氏度、微弧度每秒。零偏直接读 ins 内部（单个 float 读写是原子的，只供观察在线修正） */
static void log_imu(void)
{
    ImuState st;
    if (!imu_state_read(&imu_state, &st, IMU_STALE_MS))
    {
        RM_LOG_I("imu not ready");
        return;
    }
    const float *r_z = &ins_config.install_rotation[6];
    const float *off = ins.imu.gyro_offset_rad_s;
    const float bias_z = r_z[0] * off[0] + r_z[1] * off[1] + r_z[2] * off[2];
    RM_LOG_I("imu yaw %d pitch %d roll %d mrad, yaw total %d mrad, %d mC, yaw bias %d urad/s",
             (int)(st.yaw_rad * 1000.0f), (int)(st.pitch_rad * 1000.0f),
             (int)(st.roll_rad * 1000.0f), (int)(st.yaw_total_rad * 1000.0f),
             (int)(st.temperature_c * 1000.0f), (int)(bias_z * 1e6f));
    /* 机体系加速度（已低通）与角速度（已减零偏）：静止水平时约 0 0 9800 mm/s²、各轴几 mrad/s（V5、V7） */
    RM_LOG_I("imu accel %d %d %d mm/s2, gyro %d %d %d mrad/s, read failures %u",
             (int)(st.accel_m_s2[0] * 1000.0f), (int)(st.accel_m_s2[1] * 1000.0f),
             (int)(st.accel_m_s2[2] * 1000.0f), (int)(st.gyro_rad_s[0] * 1000.0f),
             (int)(st.gyro_rad_s[1] * 1000.0f), (int)(st.gyro_rad_s[2] * 1000.0f),
             (unsigned)ins.read_failures);
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
        buzzer_play(&buzzer,
                    BUZZER_STARTUP); /* 心跳任务在 startup 任务结束后才运行，此时启动已完成 */
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
            log_motor();
            log_joint();
            log_vt_link();
            log_usb();
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
    vision_link_init(&vision_link);
    comm_rx_set_usb(on_usb_bytes, &vision_link);
    if (!vt_link_init(&vt_link, &vt_rc_state, &kbm_state)
        || !comm_rx_add_uart(VT_LINK_UART, on_vt_link_bytes, &vt_link))
    {
        RM_LOG_E("vt_link init failed");
        return false;
    }
    if (!init_motor(&chassis_motor, &chassis_motor_config)
        || !init_motor(&joint_motor, &joint_motor_config))
    {
        return false;
    }
    if (!ins_init(&ins, &ins_config, &imu_state))
    {
        RM_LOG_E("ins init failed");
        return false;
    }
    battery_init(&battery, &battery_config);
    safety_gate_init(&gate, TEMPLATE_ARM_SWITCH);
    const PidParam speed_param = TEMPLATE_SPEED_PID_PARAM;
    pid_init(&speed_pid, PID_POSITION, &speed_param);
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

void robot_control_step(void)
{
    const uint64_t now_us = rm_time_now_us();

    /* 1. 一次性读输入快照 */
    RcState rc;
    const bool rc_online = rc_state_read(&rc_state, &rc, RC_LOST_TIMEOUT_MS);
    ImuState imu;
    const bool imu_ready = imu_state_read(&imu_state, &imu, IMU_STALE_MS);
    MotorFeedback fb;
    const bool motor_online = motor_read_feedback(&chassis_motor, &fb);

    /* 2. 安全门：遥控丢失、急停、未解锁、IMU 未就绪 → 全车停 */
    const SafetyDecision gate_out =
        safety_gate_update(&gate, rc_online ? &rc : NULL, imu_ready, now_us);

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

    /* 达妙：解锁时请求使能（离线后请求会被清除，需要重新解锁）；之后不写指令即为零力矩 */
    if (gate_out.entered_manual)
    {
        motor_request_enable(&joint_motor);
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
