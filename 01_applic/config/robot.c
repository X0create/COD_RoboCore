/**
 * @file    robot.c
 * @brief   这台车的全部对象、它们的初始化和任务表（目前：四轮麦轮底盘，遥控直接给底盘速度）
 * @note    本文件分三节：
 *          1. 对象定义：整车共用的全局变量，声明和参数都在 robot.h
 *          2. robot_init()：按 设备 → 机构 → 安全门 的顺序初始化（机构要用到设备，所以设备先初始化）
 *          3. 任务表 task_table[]：每个任务的名字、入口函数、优先级、栈，一行一个
 *          上电顺序（通用）在 01_applic/system/app_main.c：app_main() 调用 robot_init()，再按 task_table[] 创建任务。
 *          6 个任务都在 01_applic/tasks/，调用关系总图见 docs/CALL_FLOW.md。
 */
#include "01_applic/config/robot.h"

#include "01_applic/system/app_main.h"
#include "01_applic/tasks/tasks.h"
#include "04_core/log/log.h"

/* ================================================================== */
/* 1. 对象                                                             */
/* ================================================================== */

/* 设备 */
Dr16 dr16;

/* 电机、底盘、IMU 的参数都在 robot.h 的配置表里（wheel_config、chassis_config …） */
Motor wheel_motor[CHASSIS_WHEELS];
MotorGroup motors;
Supercap supercap;

/* 机构（安全门是全车唯一的，在 01_applic/system/safety_gate.c） */
Chassis chassis;
Ins ins;

/* ================================================================== */
/* 2. robot_init()：初始化对象（调度器启动前，由 app_main 调用）          */
/* ================================================================== */

bool robot_init(void)
{
    dr16_init(&dr16);

    /* 电机：按 robot.h 的电机表逐个初始化，并加入电机组 motors（之后收反馈、发指令都按组统一处理）。
     * 两个电机的 CAN 总线和 ID 撞了（会互相收到对方的反馈）时初始化失败，日志里写明和谁冲突 */
    Motor *drive[CHASSIS_WHEELS];
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        const Motor *conflict;
        if (!motor_init(&wheel_motor[i], &wheel_config[i], &motors, &conflict))
        {
            RM_LOG_E("motor %s init failed%s%s", wheel_config[i].name,
                     conflict != NULL ? ": id conflict with " : "",
                     conflict != NULL ? conflict->cfg->name : "");
            return false;
        }
        drive[i] = &wheel_motor[i];
    }
    /* 底盘：把四个驱动电机交给它；第 4 个参数是舵轮的转向电机，麦轮没有，传 NULL */
    if (!chassis_init(&chassis, &chassis_config, drive, NULL))
    {
        RM_LOG_E("chassis init failed: invalid geometry");
        return false;
    }

    /* 超级电容：在电机之后初始化，才能查出 ID 有没有和电机撞 */
    const SupercapInitResult cap_result = supercap_init(&supercap, &supercap_config, &motors);
    if (cap_result != SUPERCAP_INIT_OK)
    {
        RM_LOG_E("supercap init failed (%d)", (int)cap_result);
        return false;
    }

    ins_init(&ins, &ins_config);
    safety_gate_init(&safety_gate, arm_switch);
    return true;
}

/* ================================================================== */
/* 3. 任务表：6 个任务的名字、入口、优先级、栈                           */
/* ================================================================== */

/* startup_task 由 CubeMX 创建，不在表里。
 * 优先级数字越大越高：ins 最高，IMU 采样时刻要准；comm_rx 高于 control，控制周期读到最新反馈。
 * 改优先级：改下面表里的数字；改栈大小：改紧挨着的栈数组。
 */
static StackType_t ins_stack[1024]; /* 4 KB：EKF 的矩阵运算在栈上有临时变量（预算表） */
static StackType_t comm_rx_stack[512];   /* 2 KB */
static StackType_t control_stack[1024];  /* 4 KB */
static StackType_t detect_stack[256];    /* 1 KB */
static StackType_t indicator_stack[256]; /* 1 KB */
static StackType_t log_stack[256];       /* 1 KB */

static RmTask ins_task, control_task, detect_task, indicator_task,
    log_task; /* comm_rx_task 在 comm_rx_task.c（中断要用它唤醒任务） */

#define STACK_WORDS(stack) ((uint32_t)(sizeof(stack) / sizeof((stack)[0])))

/* clang-format off */
const TaskTableEntry task_table[] = {
    /* 任务             名字              入口                  参数   优先级  栈                                          周期 */
    { &ins_task,       "ins_task",       ins_task_entry,        &ins,  6u,  ins_stack,       STACK_WORDS(ins_stack)       }, /* 1 ms */
    { &comm_rx_task,   "comm_rx_task",   comm_rx_task_entry,    NULL,  5u,  comm_rx_stack,   STACK_WORDS(comm_rx_stack)   }, /* 收到 CAN / 串口就运行 */
    { &control_task,   "control_task",   control_task_entry,    NULL,  4u,  control_stack,   STACK_WORDS(control_stack)   }, /* 1 ms */
    { &detect_task,    "detect_task",    detect_task_entry,     NULL,  3u,  detect_stack,    STACK_WORDS(detect_stack)    }, /* 10 ms */
    { &indicator_task, "indicator_task", indicator_task_entry,  NULL,  2u,  indicator_stack, STACK_WORDS(indicator_stack) }, /* 25 ms */
    { &log_task,       "log_task",       log_task_entry,        NULL,  1u,  log_stack,       STACK_WORDS(log_stack)       }, /* 1 s */
};
/* clang-format on */
const uint32_t task_table_count = (uint32_t)(sizeof(task_table) / sizeof(task_table[0]));
