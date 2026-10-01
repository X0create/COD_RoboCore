/**
 * @file    os.h
 * @brief   FreeRTOS 原生接口的薄封装：静态创建任务、按绝对时刻延时（ADR 0025）
 * @note    框架代码只通过这里使用 RTOS；不调用 CMSIS-RTOS 接口，不做动态分配。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "05_platform/compiler.h"

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef void (*RmTaskEntry)(void *arg);

/** 一个静态任务：控制块和句柄。栈由调用方另外提供（通常是同一文件里的 static 数组） */
typedef struct
{
    StaticTask_t tcb;
    TaskHandle_t handle;
} RmTask;

/**
 * @brief   静态创建任务
 * @param   priority     FreeRTOS 优先级，数字越大越高；各任务的取值只在兵种 robot.c 的任务表一处写
 * @param   stack        栈数组，元素类型 StackType_t
 * @param   stack_words  栈数组的元素个数
 * @return  false：参数不合法，任务没有创建
 */
RM_NODISCARD bool rm_task_create(RmTask *task, const char *name, RmTaskEntry entry, void *arg,
                                 uint32_t priority, StackType_t *stack, uint32_t stack_words);

/** 按绝对时刻延时的起点：进入任务循环前调用一次 rm_task_period_start() 取得 */
typedef TickType_t RmTaskPeriod;

/** 取当前 tick，作为周期延时的起点 */
RmTaskPeriod rm_task_period_start(void);

/**
 * @brief   延时到“上一次唤醒时刻 + period_ms”，周期不随执行时间漂移
 * @pre     period_ms 是 tick 周期（1 ms）的整数倍
 */
void rm_task_delay_until(RmTaskPeriod *last_wake, uint32_t period_ms);

/** 删除当前任务（静态任务的栈和控制块不回收，只是不再运行） */
void rm_task_delete_self(void);

/**
 * @brief   等待其他地方（通常是中断）发来的通知，最多等 timeout_ms
 * @return  true：收到了通知；false：超时
 * @note    多次通知在被取走前只算一次（计数清零），适合“有新数据了，去取吧”这种用法
 */
bool rm_task_wait_notify(uint32_t timeout_ms);

/**
 * @brief   在中断里通知一个任务（配合 rm_task_wait_notify）
 * @pre     任务已创建；只能在“RTOS 管理的中断”里调用（优先级数值 ≥ configMAX_SYSCALL_INTERRUPT_PRIORITY）
 */
void rm_task_notify_from_isr(RmTask *task);

#ifdef __cplusplus
}
#endif
