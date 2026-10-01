/**
 * @file    robot.c
 * @brief   台架验证固件：全部对象、上电顺序、任务表都在这一个文件里，从上往下读
 * @note    1. 对象定义（相当于老模板的全局变量），声明在 robot.h
 *          2. init_objects()：遥控 → 视觉 / 图传链路 → 电机 → IMU → 安全门
 *          3. 任务表：5 个任务的优先级、栈、入口（相当于老模板 Core/Src/freertos.c 里的任务列表）
 *          4. app_main()：调度器启动前，由 CubeMX 的 freertos.c 调用
 *          5. startup_task()：调度器启动后第一个运行，打开 ADC、蜂鸣器，允许解锁
 *          control（遥控控制一台 M3508 的转速 + 一台达妙）、comm_rx、ins、heartbeat 一个任务一个文件在本目录；
 *          接线（哪个串口交给哪个解析器）在本目录的 comm_rx_task.c；daemon_task.c 各兵种相同，在 01_app/common/。
 *          图传链路只解析和发布，暂不参与控制（ADR 0036）。调用关系总图见 docs/CALL_FLOW.md。
 *          startup_task 必须和 app_main 放在同一个文件里：它在 CubeMX 生成代码里已有弱定义，
 *          单独放进静态库的另一个 .o 时链接器不会去取，弱定义的空函数就会被悄悄用上。
 */
#include "robot.h"

#include "01_app/common/comm_rx.h"
#include "01_app/common/daemon_task.h"
#include "04_core/log/log.h"
#include "04_core/os/os.h"
#include "05_platform/adc.h"
#include "05_platform/time.h"
#include "config.h"

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

/* 电机、电池、IMU 的参数都在 config.h 的配置表里 */
Motor chassis_motor;
Motor joint_motor;
MotorGroup motors;

Battery battery;
Buzzer buzzer;

/* 子系统与安全门 */
SafetyGate gate;
Ins ins;

/* ================================================================== */
/* 2. 初始化对象（调度器启动前）                                         */
/* ================================================================== */

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
    if (!dr16_init(&dr16, &rc_state))
    {
        RM_LOG_E("dr16 init failed");
        return false;
    }
    vision_link_init(&vision_link);
    if (!vt_link_init(&vt_link, &vt_rc_state, &kbm_state))
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
    safety_gate_init(&gate, arm_switch);
    return true;
}

/* ================================================================== */
/* 3. 任务表                                                           */
/* ================================================================== */

/*
 * | 任务      | 文件                      | 优先级 | 栈    | 周期                         |
 * | --------- | ------------------------- | ------ | ----- | ---------------------------- |
 * | ins       | ins_task.c                | 5 最高 | 4 KB  | 1 ms                         |
 * | comm_rx   | comm_rx_task.c               | 4      | 2 KB  | 收到 CAN / 串口 / USB 就运行 |
 * | control   | control_task.c            | 3      | 4 KB  | 1 ms                         |
 * | daemon    | 01_app/common/daemon_task.c  | 2      | 1 KB  | 10 ms                        |
 * | heartbeat | heartbeat_task.c          | 1      | 1 KB  | 25 ms                        |
 *
 * 优先级数字越大越高：ins 最高，IMU 采样时刻要准；comm_rx 高于 control，控制周期读到最新反馈（《架构设计》任务划分）。
 * ins 的栈：EKF 的矩阵运算在栈上有临时变量，按预算表给 4 KB。
 * startup 任务由 CubeMX 创建，不在这张表里。
 */
static RmTask ins_task, control_task, daemon_task,
    heartbeat_task; /* comm_rx_task 在 01_app/common/comm_rx.c（中断要用它唤醒任务） */
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
