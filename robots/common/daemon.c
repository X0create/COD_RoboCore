/**
 * @file    daemon.c
 * @brief   daemon 任务，见 daemon.h
 */
#include "daemon.h"

#include "core/log/log.h"
#include "core/os/os.h"
#include "core/watchdog/watchdog.h"
#include "platform/can.h"
#include "platform/time.h"

#define DAEMON_STACK_WORDS    256u
#define DAEMON_PERIOD_MS      10u
#define CAN_RECOVER_PERIOD_US 100000u /* 同一路 bus-off 恢复至少间隔 100 ms */

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

/* bus-off 后 FDCAN 不会自己回到总线，这路上的电机全部离线（机构停）。这里负责把它拉回来 */
static void recover_bus_off(uint64_t now_us)
{
    static uint64_t last_try_us[CAN_BUS_COUNT];
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        if (!can_is_bus_off((CanBusId)bus) || now_us - last_try_us[bus] < CAN_RECOVER_PERIOD_US)
        {
            continue;
        }
        last_try_us[bus] = now_us;
        RM_LOG_W("can%d bus-off, restarting", bus + 1);
        can_recover((CanBusId)bus);
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
        recover_bus_off(rm_time_now_us());
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
