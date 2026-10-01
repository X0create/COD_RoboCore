/**
 * @file    detect_task.c
 * @brief   detect_task，见 detect_task.h
 */
#include "detect_task.h"

#include "04_core/log/log.h"
#include "04_core/os/os.h"
#include "04_core/watchdog/watchdog.h"

#define DETECT_PERIOD_MS 10u

static void log_device(const Watchdog *wd, void *ctx)
{
    (void)ctx;
    RM_LOG_I("  %s (timeout %u ms)", wd->name, (unsigned)wd->timeout_ms);
}

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
    RM_LOG_I("devices:");
    watchdog_for_each(log_device, NULL);

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        watchdog_poll(log_change, NULL);
        rm_task_delay_until(&last_wake, DETECT_PERIOD_MS);
    }
}
