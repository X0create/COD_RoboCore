/**
 * @file    detect_task.c
 * @brief   detect_task（100 Hz，相当于老模板 Detect_Task）：报告设备上线 / 离线（《架构设计》核心机制第 3 节、任务划分）
 * @note    通用。只做报告，不参与安全判定：遥控丢失、电机离线由读取方按时间戳当场判断。
 *          任务开始时打印一次设备清单（本固件认为车上有哪些设备）。
 *          硬件看门狗 IWDG（收齐关键任务心跳才喂狗）在阶段 1 加入本任务。
 */
#include "tasks.h"

#include "04_core/log/log.h"
#include "04_core/os/os.h"
#include "04_core/watchdog/watchdog.h"

#define DETECT_PERIOD_MS 10u

/* 设备清单的一行：名字和超时（watchdog_for_each 的回调） */
static void log_device(const Watchdog *wd, void *ctx)
{
    (void)ctx;
    RM_LOG_I("  %s (timeout %u ms)", wd->name, (unsigned)wd->timeout_ms);
}

/* 上线 / 离线变化时打印一行（watchdog_poll 的回调，只在状态变化时调用） */
static void log_change(const Watchdog *wd, bool online, void *ctx)
{
    (void)ctx;
    if (online)
    {
        RM_LOG_I("%s online", wd->name);
    }
    else
    {
        RM_LOG_W("%s offline", wd->name);
    }
}

void detect_task_entry(void *arg)
{
    (void)arg;
    /* 开始时打印一次“本固件认为车上有哪些设备”：排查某设备没接上时先看这里 */
    RM_LOG_I("devices:");
    watchdog_for_each(log_device, NULL);

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        /* 只报告；停不停车由 dr16_read() 等读取函数在使用处当场判断（同一个看门狗，结果一致） */
        watchdog_poll(log_change, NULL);
        rm_task_delay_until(&last_wake, DETECT_PERIOD_MS);
    }
}
