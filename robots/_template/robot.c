/**
 * @file    robot.c
 * @brief   新兵种的样板：有哪些对象、怎么初始化、有哪些任务
 * @note    本目录一个任务一个文件（相当于老模板的 Application/Task/）：
 *            control_task.c    每 1 ms：读输入 → 安全门 → 速度环 → 发送（遥控控制一台 M3508 的转速 + 一台达妙）
 *            ins_task.c        每 1 ms：IMU 姿态解算
 *            heartbeat_task.c  状态灯、蜂鸣器、电池、RTT 打印
 *          comm_rx（收 CAN / 串口 / USB）和 daemon（上线 / 离线报告）两个任务各兵种相同，在 robots/common/。
 *          图传链路（VT13 遥控器、键鼠）接 USART10，只解析和发布，暂不参与控制（ADR 0036）。
 *          USB 虚拟串口接上位机：vision_link 找 0x5A 帧并计数；收到的字节原样回发，用电脑串口助手验证通道
 *          （视觉协议确定后去掉回发，ADR 0037）。
 *          调用关系总图见 docs/CALL_FLOW.md。
 */
#include "robot.h"

#include "comm_rx.h"
#include "config.h"
#include "core/log/log.h"
#include "core/os/os.h"
#include "daemon.h"
#include "objects.h"
#include "platform/adc.h"
#include "platform/can.h"
#include "platform/uart.h"
#include "platform/usb_cdc.h"
#include "tasks.h"

/* DR16 接收机接 UART5（接线沿用 COD-H7-Template，《架构设计》“阶段 1 清单”） */
#define DBUS_UART UART_5
/* 图传链路接 USART10（921600，ADR 0036） */
#define VT_LINK_UART UART_10

/* ---------------- 话题：谁发布、谁读取 ---------------- */
RcStateTopic rc_state; /* 发布：dr16    读取：control（安全门、速度目标）、heartbeat */
ImuStateTopic imu_state;    /* 发布：ins     读取：control（安全门）、heartbeat */
VtRcStateTopic vt_rc_state; /* 发布：vt_link 读取：heartbeat（以后是 command） */
KbmStateTopic kbm_state;    /* 发布：vt_link 读取：heartbeat（以后是 command） */

/* ---------------- 设备 ---------------- */
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

/* ---------------- 子系统与安全门 ---------------- */
SafetyGate gate;
static const InsConfig ins_config = { .install_rotation = TEMPLATE_IMU_INSTALL_ROTATION };
Ins ins;

/* ================================================================== */
/* 初始化                                                              */
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

bool robot_init(void)
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
    safety_gate_init(&gate, TEMPLATE_ARM_SWITCH);
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

/* ================================================================== */
/* 任务表（相当于老模板 Core/Src/freertos.c 里的任务列表）               */
/* ================================================================== */

/*
 * | 任务      | 文件                   | 优先级 | 栈    | 周期                     |
 * | --------- | ---------------------- | ------ | ----- | ------------------------ |
 * | ins       | ins_task.c             | 5 最高 | 4 KB  | 1 ms                     |
 * | comm_rx   | common/comm_rx.c       | 4      | 2 KB  | 收到 CAN / 串口 / USB 就运行 |
 * | control   | control_task.c         | 3      | 4 KB  | 1 ms                     |
 * | daemon    | common/daemon.c        | 2      | 1 KB  | 10 ms                    |
 * | heartbeat | heartbeat_task.c       | 1      | 1 KB  | 25 ms                    |
 *
 * 优先级数字越大越高：ins 最高，IMU 采样时刻要准；comm_rx 高于 control，控制周期读到最新反馈（《架构设计》任务划分）。
 * ins 的栈：EKF 的矩阵运算在栈上有临时变量，按预算表给 4 KB。comm_rx、daemon 的栈在各自文件里。
 */
enum
{
    PRIORITY_HEARTBEAT = 1,
    PRIORITY_DAEMON = 2,
    PRIORITY_CONTROL = 3,
    PRIORITY_COMM_RX = 4,
    PRIORITY_INS = 5,
};

static RmTask ins_task, control_task, heartbeat_task;
static StackType_t ins_stack[1024], control_stack[1024], heartbeat_stack[256];

#define STACK_WORDS(stack) ((uint32_t)(sizeof(stack) / sizeof((stack)[0])))

static void create_task(RmTask *task, const char *name, RmTaskEntry entry, uint32_t priority,
                        StackType_t *stack, uint32_t stack_words)
{
    if (!rm_task_create(task, name, entry, NULL, priority, stack, stack_words))
    {
        RM_LOG_E("create %s task failed", name);
    }
}

void robot_create_tasks(void)
{
    create_task(&ins_task, "ins", ins_task_entry, PRIORITY_INS, ins_stack, STACK_WORDS(ins_stack));
    comm_rx_create_task(PRIORITY_COMM_RX);
    create_task(&control_task, "control", control_task_entry, PRIORITY_CONTROL, control_stack,
                STACK_WORDS(control_stack));
    daemon_create_task(PRIORITY_DAEMON);
    create_task(&heartbeat_task, "heartbeat", heartbeat_task_entry, PRIORITY_HEARTBEAT,
                heartbeat_stack, STACK_WORDS(heartbeat_stack));
}
