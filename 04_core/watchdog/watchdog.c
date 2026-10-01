/**
 * @file    watchdog.c
 * @brief   设备在线检测，见 watchdog.h
 */
#include "watchdog.h"

#include <stddef.h>

#include "04_core/os/critical.h"
#include "05_platform/time/time.h"

static Watchdog *registered;

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

void watchdog_feed(Watchdog *wd)
{
    const uint64_t now_us = rm_time_now_us();
    rm_critical_enter();
    wd->last_feed_us = now_us;
    wd->fed = true;
    rm_critical_exit();
}

bool watchdog_is_online(const Watchdog *wd)
{
    /* 先取时刻再读时间戳：这之间若恰好喂了狗，时间戳比 now_us 还新，按“刚喂过”处理 */
    const uint64_t now_us = rm_time_now_us();
    rm_critical_enter();
    const bool fed = wd->fed;
    const uint64_t last_us = wd->last_feed_us;
    rm_critical_exit();
    return fed && (last_us >= now_us || now_us - last_us < (uint64_t)wd->timeout_ms * 1000u);
}

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
