/**
 * @file    detect_task.c
 * @brief   detect_task（100 Hz，相当于老模板 Detect_Task）：全部“检测”都在这里——
 *          报告设备上线 / 离线，并按 control_task 的心跳喂硬件看门狗 IWDG
 * @note    通用。设备上线 / 离线只做报告，不参与安全判定：遥控丢失、电机离线由读取方按时间戳当场判断。
 *          任务开始时打印一次设备清单（本固件认为车上有哪些设备）。
 *          IWDG（ADR 0074）：control_task 每周期心跳加 1，这里每 10 ms 看一次，在涨才喂狗。
 *          control_task 卡死、被饿死，或本任务自己卡死，都会在 IWDG_TIMEOUT_MS（50 ms）内复位。
 *          只看 control_task：只有它发电机指令；其他任务卡死时用到它数据的机构自己处理。
 */
#include "01_applic/tasks/tasks.h"

#include "04_core/log/log.h"
#include "04_core/os/os.h"
#include "04_core/watchdog/watchdog.h"
#include "05_platform/iwdg/iwdg.h"

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

    iwdg_start();
    uint32_t last_heartbeat = control_heartbeat;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        /* 硬件看门狗：control_task 这 10 ms 里跑过才喂 */
        const uint32_t heartbeat = control_heartbeat;
        if (heartbeat != last_heartbeat)
        {
            iwdg_feed();
            last_heartbeat = heartbeat;
        }

        /* 只报告；停不停车由 dr16_read() 等读取函数在使用处当场判断（同一个看门狗，结果一致） */
        watchdog_poll(log_change, NULL);
        rm_task_delay_until(&last_wake, DETECT_PERIOD_MS);
    }
}
