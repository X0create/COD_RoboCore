/**
 * @file    bench_robot.c
 * @brief   台架验证固件：全部对象、初始化、任务表
 * @note    1. 对象定义（相当于老模板的全局变量），声明在 bench_robot.h；参数都在 bench_config.h
 *          2. robot_init()：设备 → 机构 → 安全门
 *          3. 任务表 robot_tasks[]：6 个任务的优先级、栈、入口
 *          上电顺序（各兵种相同）在 01_applic/system/app_main.c：app_main() 调用 robot_init()，再按 robot_tasks[] 创建任务。
 *          本目录的任务：bench_control_task.c、bench_comm_rx_task.c（接线）、bench_log_task.c；ins、detect、indicator_task 各兵种相同，
 *          在 01_applic/ins/ 和 01_applic/system/。调用关系总图见 docs/CALL_FLOW.md。
 */
#include "bench_robot.h"

#include "01_applic/ins/ins_task.h"
#include "01_applic/system/app_main.h"
#include "01_applic/system/comm_rx_common.h"
#include "01_applic/system/detect_task.h"
#include "01_applic/system/indicator_task.h"
#include "04_core/log/log.h"
#include "bench_config.h"

/* ================================================================== */
/* 1. 对象                                                             */
/* ================================================================== */

/* 话题：谁发布、谁读取 */
RcStateTopic rc_state;   /* 发布：dr16    读取：control（安全门、速度目标）、log */
ImuStateTopic imu_state; /* 发布：ins     读取：control（安全门）、log */
VtRcStateTopic vt_rc_state; /* 发布：vt_link 读取：log（以后是 command） */
KbmStateTopic kbm_state;    /* 发布：vt_link 读取：log（以后是 command） */

/* 设备 */
Dr16 dr16;
VtLink vt_link;
VisionLink vision_link;
volatile uint32_t usb_rx_bytes;
volatile uint32_t usb_echo_dropped;

/* 电机、IMU 的参数都在 bench_config.h 的配置表里 */
Motor chassis_motor;
Motor joint_motor;
MotorGroup motors;

/* 机构（安全门是全车唯一的，在 01_applic/system/safety_gate.c） */
Ins ins;

/* ================================================================== */
/* 2. robot_init()：初始化对象（调度器启动前，由 app_main 调用）          */
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

bool robot_init(void)
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
    safety_gate_init(&safety_gate, arm_switch);
    return true;
}

/* ================================================================== */
/* 3. 任务表（相当于老模板 Core/Src/freertos.c 里的任务列表）               */
/* ================================================================== */

/*
 * 优先级数字越大越高：ins 最高，IMU 采样时刻要准；comm_rx 高于 control，控制周期读到最新反馈。
 * 改优先级：改下面表里的数字；改栈大小：改紧挨着的栈数组。startup_task 由 CubeMX 创建，不在表里。
 */
static StackType_t ins_stack[1024]; /* 4 KB：EKF 的矩阵运算在栈上有临时变量（预算表） */
static StackType_t comm_rx_stack[512];   /* 2 KB */
static StackType_t control_stack[1024];  /* 4 KB */
static StackType_t detect_stack[256];    /* 1 KB */
static StackType_t indicator_stack[256]; /* 1 KB */
static StackType_t log_stack[256];       /* 1 KB */

static RmTask ins_task, control_task, detect_task, indicator_task,
    log_task; /* comm_rx_task 在 01_applic/system/comm_rx_common.c（中断要用它唤醒任务） */

#define STACK_WORDS(stack) ((uint32_t)(sizeof(stack) / sizeof((stack)[0])))

/* clang-format off */
const AppTask robot_tasks[] = {
    /* 任务             名字              入口                  参数   优先级  栈                                          周期 */
    { &ins_task,       "ins_task",       ins_task_entry,        &ins,  6u,  ins_stack,       STACK_WORDS(ins_stack)       }, /* 1 ms（01_applic/ins/ins_task.c） */
    { &comm_rx_task,   "comm_rx_task",   comm_rx_task_entry,    NULL,  5u,  comm_rx_stack,   STACK_WORDS(comm_rx_stack)   }, /* 收到 CAN / 串口 / USB 就运行（bench_comm_rx_task.c） */
    { &control_task,   "control_task",   control_task_entry,    NULL,  4u,  control_stack,   STACK_WORDS(control_stack)   }, /* 1 ms（bench_control_task.c） */
    { &detect_task,    "detect_task",    detect_task_entry,     NULL,  3u,  detect_stack,    STACK_WORDS(detect_stack)    }, /* 10 ms（01_applic/system/detect_task.c） */
    { &indicator_task, "indicator_task", indicator_task_entry,  NULL,  2u,  indicator_stack, STACK_WORDS(indicator_stack) }, /* 25 ms（01_applic/system/indicator_task.c） */
    { &log_task,       "log_task",       log_task_entry,        NULL,  1u,  log_stack,       STACK_WORDS(log_stack)       }, /* 1 s（bench_log_task.c） */
};
/* clang-format on */
const uint32_t robot_task_count = (uint32_t)(sizeof(robot_tasks) / sizeof(robot_tasks[0]));
