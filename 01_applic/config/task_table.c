/**
 * @file    task_table.c
 * @brief   这台车的任务表：6 个任务的名字、入口、优先级、栈（相当于老模板 Core/Src/freertos.c 里的任务列表）
 * @note    上电时 01_applic/system/app_main.c 按这张表逐个创建任务；任务本身都在 01_applic/tasks/。
 *          startup_task 由 CubeMX 创建，不在表里。
 */
#include "01_applic/system/app_main.h"

#include "01_applic/tasks/comm_rx_task.h"
#include "01_applic/tasks/control_task.h"
#include "01_applic/tasks/detect_task.h"
#include "01_applic/tasks/indicator_task.h"
#include "01_applic/tasks/ins_task.h"
#include "01_applic/tasks/log_task.h"
#include "objects.h"

/*
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
