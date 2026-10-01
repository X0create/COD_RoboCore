/**
 * @file    watchdog.h
 * @brief   设备在线检测（软件看门狗，《架构设计》核心机制第 3 节）——判断“这个设备最近有没有发来数据”，
 *          并保管它最新的一份数据
 * @warning 不是芯片的硬件看门狗 IWDG（防程序卡死、超时复位），那个在阶段 1 另做。
 * @note    - 设备每收到一帧**合法**数据就喂狗：有数据要给别的任务读的用 watchdog_feed_data()（数据和时刻一起写），
 *            只需要在线状态的用 watchdog_feed()；
 *          - 在不在线由读取方当场计算（现在 − 最后喂狗时刻 ≤ 超时），不等 detect_task 来标记，检测延迟就是超时本身；
 *          - 一个设备的在线状态只有这一个来源（ADR 0059）：控制用的 dr16_read() 等和 detect_task 的上线 / 离线日志
 *            读的是同一个时间戳、同一个超时，不会出现“日志说离线、控制仍认为在线”；
 *          - detect_task 定期调用 watchdog_poll() 只做**报告**，不参与安全判定。
 *          喂狗和读取都在任务临界区里拷贝数据和 64 位时间戳，任务之间可以并发调用；中断里不能调用。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Watchdog
{
    const char *name;      /* 日志和设备清单里显示的名字 */
    uint32_t timeout_ms;   /* 超过这么久没喂就算离线（正好等于还算在线） */
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

/** 收到一帧合法数据时调用（只要在线状态、没有数据给别人读的设备用这个） */
void watchdog_feed(Watchdog *wd);

/**
 * @brief   收到一帧合法数据：在同一个临界区里把 src 整份拷进 slot 并记下它的接收时刻，读到的数据和它的时刻一定对得上
 * @param   rx_us  这帧数据的接收时刻（rm_time_now_us() 的时间）。能在中断里记下时刻的（CAN 的 CanFrame.stamp_us）就用它，
 *                 这样任务被耽误、帧积压时，旧帧按它真正的年龄判断，不会被当成刚收到的数据
 */
void watchdog_feed_data(Watchdog *wd, void *slot, const void *src, size_t size, uint64_t rx_us);

/** 现在是否在线 */
bool watchdog_is_online(const Watchdog *wd);

/**
 * @brief   把 slot 整份拷到 out（从未收到时是初始化时的全 0），同时判断现在是否在线
 * @return  与 watchdog_is_online() 相同；false 时 out 里是最后一次收到的旧数据，只能用来打印
 */
bool watchdog_read_data(const Watchdog *wd, const void *slot, void *out, size_t size);

/** 上线 / 离线变化的回调 */
typedef void (*WatchdogChangeFn)(const Watchdog *wd, bool online, void *ctx);

/** 检查所有登记的设备，状态和上次报告的不同时调用 fn（只能由一个任务调用，通常是守护任务） */
void watchdog_poll(WatchdogChangeFn fn, void *ctx);

/** 逐个访问登记的设备（打印设备清单用） */
void watchdog_for_each(void (*fn)(const Watchdog *wd, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
