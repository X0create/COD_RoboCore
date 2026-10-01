/**
 * @file    watchdog.h
 * @brief   设备在线检测（软件看门狗，《架构设计》核心机制第 3 节）——判断“这个设备最近有没有发来数据”
 * @warning 不是芯片的硬件看门狗 IWDG（防程序卡死、超时复位），那个在阶段 1 另做。
 * @note    - 设备每收到一帧**合法**数据就 watchdog_feed()；
 *          - 在不在线由读取方当场计算（现在 − 最后喂狗时刻 < 超时），不等 detect 任务来标记，检测延迟就是超时本身；
 *          - detect 任务定期调用 watchdog_poll() 只做**报告**（上线 / 离线日志），不参与安全判定。
 *          喂狗和查询在任务临界区里读写 64 位时间戳，任务之间可以并发调用；中断里不能调用。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Watchdog
{
    const char *name;      /* 日志和设备清单里显示的名字 */
    uint32_t timeout_ms;   /* 超过这么久没喂就算离线 */
    uint64_t last_feed_us; /* 以下由本模块维护 */
    bool fed;              /* 是否喂过；从没喂过的设备算离线 */
    bool reported_online;  /* 上次报告的状态，只由 watchdog_poll() 使用 */
    struct Watchdog *next; /* 登记链表 */
} Watchdog;

/**
 * @brief   登记：设置名字和超时，加入设备清单，初始为离线
 * @pre     只在初始化阶段（调度器启动前）调用；同一个 wd 只登记一次
 */
void watchdog_register(Watchdog *wd, const char *name, uint32_t timeout_ms);

/** 收到一帧合法数据时调用 */
void watchdog_feed(Watchdog *wd);

/** 现在是否在线 */
bool watchdog_is_online(const Watchdog *wd);

/** 上线 / 离线变化的回调 */
typedef void (*WatchdogChangeFn)(const Watchdog *wd, bool online, void *ctx);

/** 检查所有登记的设备，状态和上次报告的不同时调用 fn（只能由一个任务调用，通常是守护任务） */
void watchdog_poll(WatchdogChangeFn fn, void *ctx);

/** 逐个访问登记的设备（打印设备清单用） */
void watchdog_for_each(void (*fn)(const Watchdog *wd, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
