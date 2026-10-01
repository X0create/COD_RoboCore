/**
 * @file    robot.c
 * @brief   台架验证固件：全部对象、上电顺序、任务表都在这一个文件里，从上往下读
 * @note    1. 对象定义（相当于老模板的全局变量），声明在 robot.h
 *          2. init_objects()：遥控 → 视觉 / 图传链路 → 电机 → IMU → 安全门
 *          3. 任务表：5 个任务的优先级、栈、入口（相当于老模板 Core/Src/freertos.c 里的任务列表）
 *          4. app_main()：调度器启动前，由 CubeMX 的 freertos.c 调用
 *          5. startup_task()：调度器启动后第一个运行，打开接收、允许解锁
 *          control（遥控控制一台 M3508 的转速 + 一台达妙）、ins、heartbeat 一个任务一个文件在本目录，
 *          comm_rx_task.c、daemon_task.c 各兵种相同，在 app/common/。
 *          图传链路（VT13 遥控器、键鼠）接 USART10，只解析和发布，暂不参与控制（ADR 0036）。
 *          USB 虚拟串口接上位机：vision_link 找 0x5A 帧并计数；收到的字节原样回发，用电脑串口助手验证通道
 *          （视觉协议确定后去掉回发，ADR 0037）。调用关系总图见 docs/CALL_FLOW.md。
 *          startup_task 必须和 app_main 放在同一个文件里：它在 CubeMX 生成代码里已有弱定义，
 *          单独放进静态库的另一个 .o 时链接器不会去取，弱定义的空函数就会被悄悄用上。
 */
#include "robot.h"

#include "app/common/comm_rx_task.h"
#include "app/common/daemon_task.h"
#include "config.h"
#include "core/log/log.h"
#include "core/os/os.h"
#include "platform/adc.h"
#include "platform/can.h"
#include "platform/time.h"
#include "platform/uart.h"
#include "platform/usb_cdc.h"

/* DR16 接收机接 UART5（接线沿用 COD-H7-Template，《架构设计》“阶段 1 清单”） */
#define DBUS_UART UART_5
/* 图传链路接 USART10（921600，ADR 0036） */
#define VT_LINK_UART UART_10

/* ================================================================== */
/* 1. 对象                                                             */
/* ================================================================== */

/* 话题：谁发布、谁读取 */
RcStateTopic rc_state; /* 发布：dr16    读取：control（安全门、速度目标）、heartbeat */
ImuStateTopic imu_state;    /* 发布：ins     读取：control（安全门）、heartbeat */
VtRcStateTopic vt_rc_state; /* 发布：vt_link 读取：heartbeat（以后是 command） */
KbmStateTopic kbm_state;    /* 发布：vt_link 读取：heartbeat（以后是 command） */

/* 设备 */
Dr16 dr16;
VtLink vt_link;
VisionLink vision_link;
volatile uint32_t usb_rx_bytes;
volatile uint32_t usb_echo_dropped;

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
Motor chassis_motor;

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
Motor joint_motor;
MotorGroup motors;

static const BatteryConfig battery_config = BENCH_BATTERY_CONFIG;
Battery battery;
Buzzer buzzer;

/* 子系统与安全门 */
SafetyGate gate;
static const InsConfig ins_config = { .install_rotation = BENCH_IMU_INSTALL_ROTATION };
Ins ins;

/* ================================================================== */
/* 2. 初始化对象（调度器启动前）                                         */
/* ================================================================== */

/* 以下三个由 comm_rx 任务调用：UART5、USB、USART10 收到的字节交给对应设备 */
static void on_dbus_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    (void)ctx;
    dr16_on_bytes(&dr16, data, len, now_us);
}

/* USB 收到的数据：交给视觉链路找帧，并原样回发（验证通道用；发送忙时丢弃） */
static void on_usb_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    (void)now_us;
    (void)ctx;
    vision_link_on_bytes(&vision_link, data, len);
    usb_rx_bytes += len;
    if (!usb_cdc_write(data, len))
    {
        usb_echo_dropped++; /* 上一包还没发完：这段不回发 */
    }
}

static void on_vt_link_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    (void)now_us;
    (void)ctx;
    vt_link_on_bytes(&vt_link, data, len);
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

/** @return false：必需的设备或子系统初始化失败，原因已记日志 */
static bool init_objects(void)
{
    if (!dr16_init(&dr16, &rc_state) || !comm_rx_add_uart(DBUS_UART, on_dbus_bytes, NULL))
    {
        RM_LOG_E("dr16 init failed");
        return false;
    }
    vision_link_init(&vision_link);
    comm_rx_set_usb(on_usb_bytes, NULL);
    if (!vt_link_init(&vt_link, &vt_rc_state, &kbm_state)
        || !comm_rx_add_uart(VT_LINK_UART, on_vt_link_bytes, NULL))
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
    safety_gate_init(&gate, BENCH_ARM_SWITCH);
    return true;
}

/* ================================================================== */
/* 3. 任务表                                                           */
/* ================================================================== */

/*
 * | 任务      | 文件                      | 优先级 | 栈    | 周期                         |
 * | --------- | ------------------------- | ------ | ----- | ---------------------------- |
 * | ins       | ins_task.c                | 5 最高 | 4 KB  | 1 ms                         |
 * | comm_rx   | app/common/comm_rx_task.c | 4      | 2 KB  | 收到 CAN / 串口 / USB 就运行 |
 * | control   | control_task.c            | 3      | 4 KB  | 1 ms                         |
 * | daemon    | app/common/daemon_task.c  | 2      | 1 KB  | 10 ms                        |
 * | heartbeat | heartbeat_task.c          | 1      | 1 KB  | 25 ms                        |
 *
 * 优先级数字越大越高：ins 最高，IMU 采样时刻要准；comm_rx 高于 control，控制周期读到最新反馈（《架构设计》任务划分）。
 * ins 的栈：EKF 的矩阵运算在栈上有临时变量，按预算表给 4 KB。
 * startup 任务由 CubeMX 创建，不在这张表里。
 */
static RmTask ins_task, control_task, daemon_task,
    heartbeat_task; /* comm_rx_task 在 comm_rx_task.c */
static StackType_t ins_stack[1024], comm_rx_stack[512], control_stack[1024], daemon_stack[256],
    heartbeat_stack[256];

#define STACK_WORDS(stack) ((uint32_t)(sizeof(stack) / sizeof((stack)[0])))

static bool create_tasks(void)
{
    return rm_task_create(&ins_task, "ins", ins_task_entry, NULL, 5u, ins_stack,
                          STACK_WORDS(ins_stack))
           && rm_task_create(&comm_rx_task, "comm_rx", comm_rx_task_entry, NULL, 4u, comm_rx_stack,
                             STACK_WORDS(comm_rx_stack))
           && rm_task_create(&control_task, "control", control_task_entry, NULL, 3u, control_stack,
                             STACK_WORDS(control_stack))
           && rm_task_create(&daemon_task, "daemon", daemon_task_entry, NULL, 2u, daemon_stack,
                             STACK_WORDS(daemon_stack))
           && rm_task_create(&heartbeat_task, "heartbeat", heartbeat_task_entry, NULL, 1u,
                             heartbeat_stack, STACK_WORDS(heartbeat_stack));
}

/* ================================================================== */
/* 4. 上电（调度器启动前）                                               */
/* ================================================================== */

/* 阶段 0 还没有 RM_ASSERT 和故障记录（阶段 1），初始化失败时先停在这里，调试器能直接看到位置；
 * 停在这里时没有任何任务运行，不会给电机发指令 */
static void halt_on_init_failure(void)
{
    for (;;)
    {
    }
}

void app_main(void)
{
    const bool time_ok = rm_time_init(); /* DWT 计时 */
    rm_log_init();                       /* RTT 日志 */
    RM_LOG_I("COD RoboCore booting");
    if (!time_ok)
    {
        RM_LOG_E("DWT cycle counter not running");
        halt_on_init_failure();
    }

    /* 读故障记录、board_init、参数在阶段 1 加入 */

    if (!init_objects())
    {
        RM_LOG_E("init failed");
        halt_on_init_failure();
    }
    if (!create_tasks())
    {
        RM_LOG_E("create tasks failed");
        halt_on_init_failure();
    }
    RM_LOG_I("starting scheduler");
}

/* ================================================================== */
/* 5. startup 任务（调度器启动后第一个运行，完成后删除自己）               */
/* ================================================================== */

void startup_task(void *argument)
{
    (void)argument;

    comm_rx_start(); /* 打开 CAN、串口、USB 接收，收到数据由中断唤醒 comm_rx 任务 */
    if (!adc_start())
    {
        RM_LOG_E("adc start failed"); /* 只影响低电量提示，不阻止解锁 */
    }
    if (buzzer_init(&buzzer))
    {
        buzzer_play(&buzzer, BUZZER_STARTUP);
    }
    else
    {
        RM_LOG_E("buzzer pwm start failed");
    }

    /* 设备自检、硬件看门狗在阶段 1 加入 */
    safety_gate_set_system_ready(&gate); /* 允许解锁 */
    RM_LOG_I("startup done");
    rm_task_delete_self();
}
