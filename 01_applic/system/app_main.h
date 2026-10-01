/**
 * @file    app_main.h
 * @brief   上电顺序（通用，只有这一份）与 01_applic/config/ 必须提供的两样东西
 * @note    上电顺序（app_main.c）：
 *            调度器启动前  app_main()：DWT 计时 → RTT 日志 → objects_init() → 按 task_table[] 创建全部任务
 *            调度器启动后  startup_task()：允许解锁，然后删除自己
 *          01_applic/config/ 提供：objects.h 的 objects_init()、task_table.c 的 task_table[]（声明在下面）。
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
    void *arg;         /* 传给 entry 的参数，如 ins_task 的 &ins */
    uint32_t priority; /* 数字越大优先级越高 */
    StackType_t *stack;
    uint32_t stack_words;
} TaskTableEntry;

/* ---------------- 01_applic/config/task_table.c 提供 ---------------- */

/** task_table.c：这台车的全部任务（startup_task 除外，它由 CubeMX 创建） */
extern const TaskTableEntry task_table[];
extern const uint32_t task_table_count;

/* ---------------- 本文件提供，由 CubeMX 生成的 freertos.c 调用 ---------------- */

/** 调度器启动前：在 MX_FREERTOS_Init() 的 USER CODE 区里调用；返回后由生成的 main() 调用 osKernelStart() */
void app_main(void);

/** 启动任务：调度器启动后第一个运行。CubeMX 以最高优先级静态创建它（ADR 0025 修订），生成的是弱定义 */
void startup_task(void *argument);

#ifdef __cplusplus
}
#endif
