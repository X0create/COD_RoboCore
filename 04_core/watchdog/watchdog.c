/**
 * @file    watchdog.c
 * @brief   设备在线检测，见 watchdog.h
 */
#include "watchdog.h"

#include <stddef.h>

#include "04_core/os/critical.h"
#include "05_platform/time/time.h"

static Watchdog
    *registered; /* 登记过的看门狗组成的单向链表（新登记的放在表头），detect_task 遍历它 */

/* 只在初始化阶段调用：此时还没有别的任务，链表操作不用加锁 */
void watchdog_register(Watchdog *wd, const char *name, uint32_t timeout_ms)
{
    wd->name = name;
    wd->timeout_ms = timeout_ms;
    wd->last_feed_us = 0u;
    wd->fed = false;
    wd->reported_online = false;
    wd->next = registered;
    registered = wd;
}

void watchdog_feed(Watchdog *wd, uint64_t rx_us)
{
    rm_critical_enter();
    wd->last_feed_us = rx_us;
    wd->fed = true;
    rm_critical_exit();
}

/* 现在的时刻在临界区里取：取时刻和读最后喂狗时刻之间不会插进一次喂狗，最后接收时刻不会比现在还新 */
bool watchdog_is_online(const Watchdog *wd)
{
    rm_critical_enter();
    const uint64_t now_us = rm_time_now_us();
    const bool online = wd->fed && now_us - wd->last_feed_us <= (uint64_t)wd->timeout_ms * 1000u;
    rm_critical_exit();
    return online;
}

/* 和上次报告的状态比较，变了才回调；reported_online 只由这里读写，所以只能有一个任务调用 */
void watchdog_poll(WatchdogChangeFn fn, void *ctx)
{
    for (Watchdog *wd = registered; wd != NULL; wd = wd->next)
    {
        const bool online = watchdog_is_online(wd);
        if (online != wd->reported_online)
        {
            wd->reported_online = online;
            fn(wd, online, ctx);
        }
    }
}

void watchdog_for_each(void (*fn)(const Watchdog *wd, void *ctx), void *ctx)
{
    for (const Watchdog *wd = registered; wd != NULL; wd = wd->next)
    {
        fn(wd, ctx);
    }
}
