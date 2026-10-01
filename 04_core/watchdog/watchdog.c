/**
 * @file    watchdog.c
 * @brief   设备在线检测，见 watchdog.h
 */
#include "watchdog.h"

#include <stddef.h>
#include <string.h>

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

void watchdog_feed_data(Watchdog *wd, void *slot, const void *src, size_t size, uint64_t rx_us)
{
    rm_critical_enter();
    memcpy(slot, src, size);
    wd->last_feed_us = rx_us;
    wd->fed = true;
    rm_critical_exit();
}

/* 调用方在临界区里取出 fed 和 last_us。now_us 在进临界区之前取：这之间若恰好喂了狗，时间戳比 now_us 还新，按“刚喂过”处理 */
static bool online_at(const Watchdog *wd, bool fed, uint64_t last_us, uint64_t now_us)
{
    return fed && (last_us >= now_us || now_us - last_us <= (uint64_t)wd->timeout_ms * 1000u);
}

bool watchdog_is_online(const Watchdog *wd)
{
    const uint64_t now_us = rm_time_now_us();
    rm_critical_enter();
    const bool fed = wd->fed;
    const uint64_t last_us = wd->last_feed_us;
    rm_critical_exit();
    return online_at(wd, fed, last_us, now_us);
}

bool watchdog_read_data(const Watchdog *wd, const void *slot, void *out, size_t size)
{
    const uint64_t now_us = rm_time_now_us();
    rm_critical_enter();
    memcpy(out, slot, size);
    const bool fed = wd->fed;
    const uint64_t last_us = wd->last_feed_us;
    rm_critical_exit();
    return online_at(wd, fed, last_us, now_us);
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
