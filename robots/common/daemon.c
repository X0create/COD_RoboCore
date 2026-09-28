/**
 * @file    daemon.c
 * @brief   daemon 任务，见 daemon.h
 */
#include "daemon.h"

#include "core/log/log.h"
#include "core/os/os.h"
#include "core/watchdog/watchdog.h"

#define DAEMON_STACK_WORDS 256u
#define DAEMON_PERIOD_MS   10u

static RmTask daemon_task;
static StackType_t daemon_stack[DAEMON_STACK_WORDS];

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

static void daemon_entry(void *arg)
{
    (void)arg;
    RM_LOG_I("devices:");
    watchdog_for_each(log_device, NULL);

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        watchdog_poll(log_change, NULL);
        rm_task_delay_until(&last_wake, DAEMON_PERIOD_MS);
    }
}

void daemon_create_task(uint32_t priority)
{
    if (!rm_task_create(&daemon_task, "daemon", daemon_entry, NULL, priority, daemon_stack,
                        DAEMON_STACK_WORDS))
    {
        RM_LOG_E("create daemon task failed");
    }
}
