/**
 * @file    app_main.h
 * @brief   上电顺序（各兵种共用，只有这一份）与兵种必须提供的两样东西
 * @note    上电顺序（app_main.c）：
 *            调度器启动前  app_main()：DWT 计时 → RTT 日志 → robot_init() → 按 robot_tasks[] 创建全部任务
 *            调度器启动后  startup_task()：允许解锁，然后删除自己
 *          每个兵种在自己的 robot.c 里提供 robot_init() 和 robot_tasks[]。
 *          任何一步失败都停在 halt_on_init_failure()（app_main.c），此时没有任务运行，不会给电机发指令。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "04_core/os/os.h"
#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 任务表的一行：一个任务的全部创建参数 */
typedef struct
{
    RmTask *task;
    const char *name;
    RmTaskEntry entry;
    void *arg;         /* 传给 entry 的参数，如 ins 任务的 &ins */
    uint32_t priority; /* 数字越大优先级越高 */
    StackType_t *stack;
    uint32_t stack_words;
} AppTask;

/* ---------------- 每个兵种的 robot.c 提供 ---------------- */

/**
 * @brief   初始化本兵种的全部对象（设备 → 机构 → 安全门），调度器启动前调用
 * @return  false：必需的设备或机构初始化失败，原因已记日志
 */
RM_NODISCARD bool robot_init(void);

/** 本兵种的全部任务（startup 任务除外，它由 CubeMX 创建） */
extern const AppTask robot_tasks[];
extern const uint32_t robot_task_count;

/* ---------------- 本文件提供，由 CubeMX 生成的 freertos.c 调用 ---------------- */

/** 调度器启动前：在 MX_FREERTOS_Init() 的 USER CODE 区里调用；返回后由生成的 main() 调用 osKernelStart() */
void app_main(void);

/** 启动任务：调度器启动后第一个运行。CubeMX 以最高优先级静态创建它（ADR 0025 修订），生成的是弱定义 */
void startup_task(void *argument);

#ifdef __cplusplus
}
#endif
