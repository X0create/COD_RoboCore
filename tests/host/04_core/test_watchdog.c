/**
 * @file    test_watchdog.c
 * @brief   watchdog 的单元测试：没喂过算离线、超时边界、积压旧帧按接收时刻判断、poll 只在状态变化时报告
 * @note    登记链表在同一个测试程序里一直累积，所以每个测试用自己的 Watchdog，并只看自己的报告
 */
#include "04_core/watchdog/watchdog.h"

#include "05_platform/time/time.h"
#include "fake_time.h"
#include "unity.h"

void setUp(void)
{
    fake_time_set_us(5000000u);
}

void tearDown(void)
{
}

static void test_never_fed_is_offline(void)
{
    static Watchdog wd;
    watchdog_register(&wd, "never_fed", 100u);
    TEST_ASSERT_FALSE(watchdog_is_online(&wd));
}

/* “现在 − 最后喂狗 ≤ 超时”才在线：正好等于超时仍在线，再多 1 µs 算离线 */
static void test_timeout_boundary(void)
{
    static Watchdog wd;
    watchdog_register(&wd, "boundary", 200u);
    watchdog_feed(&wd, rm_time_now_us());
    fake_time_set_us(5000000u + 200000u);
    TEST_ASSERT_TRUE(watchdog_is_online(&wd));
    fake_time_set_us(5000000u + 200000u + 1u);
    TEST_ASSERT_FALSE(watchdog_is_online(&wd));
    watchdog_feed(&wd, rm_time_now_us());
    TEST_ASSERT_TRUE(watchdog_is_online(&wd));
}

typedef struct
{
    const Watchdog *target;
    int online_reports;
    int offline_reports;
} ReportCount;

static void count_reports(const Watchdog *wd, bool online, void *ctx)
{
    ReportCount *count = ctx;
    if (wd != count->target)
    {
        return;
    }
    if (online)
    {
        count->online_reports++;
    }
    else
    {
        count->offline_reports++;
    }
}

static void test_poll_reports_changes_only(void)
{
    static Watchdog wd;
    watchdog_register(&wd, "poll", 100u);
    ReportCount count = { .target = &wd };

    watchdog_poll(count_reports, &count); /* 初始离线，没有变化 */
    TEST_ASSERT_EQUAL_INT(0, count.online_reports + count.offline_reports);

    watchdog_feed(&wd, rm_time_now_us());
    watchdog_poll(count_reports, &count);
    watchdog_poll(count_reports, &count);
    TEST_ASSERT_EQUAL_INT(1, count.online_reports);

    fake_time_advance_ms(150u);
    watchdog_poll(count_reports, &count);
    watchdog_poll(count_reports, &count);
    TEST_ASSERT_EQUAL_INT(1, count.offline_reports);
}

static void count_devices(const Watchdog *wd, void *ctx)
{
    (void)wd;
    (*(int *)ctx)++;
}

static void test_for_each_visits_all_registered(void)
{
    int before = 0;
    watchdog_for_each(count_devices, &before);
    static Watchdog extra;
    watchdog_register(&extra, "extra", 10u);
    int after = 0;
    watchdog_for_each(count_devices, &after);
    TEST_ASSERT_EQUAL_INT(before + 1, after);
}

/* 积压的旧数据按它的接收时刻判断：150 ms 前收到、现在才处理的一帧，超时 100 ms 时不算在线 */
static void test_old_rx_time_stays_offline(void)
{
    static Watchdog wd;
    watchdog_register(&wd, "old", 100u);
    const uint64_t rx_us = rm_time_now_us();
    fake_time_advance_ms(150u);
    watchdog_feed(&wd, rx_us);
    TEST_ASSERT_FALSE(watchdog_is_online(&wd));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_never_fed_is_offline);
    RUN_TEST(test_timeout_boundary);
    RUN_TEST(test_poll_reports_changes_only);
    RUN_TEST(test_for_each_visits_all_registered);
    RUN_TEST(test_old_rx_time_stays_offline);
    return UNITY_END();
}
