/**
 * @file    test_uart.c
 * @brief   DMA 循环接收记账（uart 的辅助代码）的单元测试：无数据、连续一段、跨过末尾、长度受限、
 *          正好绕一整圈与超过一圈都识别为覆盖、出错重启、接收时刻取自中断
 */
#include "05_platform/uart/uart.h"

#include <string.h>

#include "unity.h"

#define SIZE 8u

static uint8_t buf[SIZE];
static DmaRingWriter w;
static DmaRingReader r;
static uint32_t dma_pos; /* 模拟 DMA 写到的位置 */

void setUp(void)
{
    memset(&w, 0, sizeof(w));
    memset(&r, 0, sizeof(r));
    dma_pos = 0u;
}

void tearDown(void)
{
}

/* 模拟 DMA 写入 n 个字节（值从 first 开始递增）后产生一次接收事件；n 不超过半圈（半满、全满中断保证） */
static void dma_write(uint32_t n, uint8_t first, uint64_t now_us)
{
    for (uint32_t i = 0u; i < n; i++)
    {
        buf[dma_pos] = (uint8_t)(first + i);
        dma_pos = (dma_pos + 1u) % SIZE;
    }
    dma_ring_on_event(&w, SIZE, dma_pos == 0u ? SIZE : dma_pos, now_us); /* 全满时 HAL 给 SIZE */
}

static void test_no_new_data(void)
{
    uint8_t out[SIZE];
    TEST_ASSERT_EQUAL_UINT32(0u, dma_ring_read(&r, &w, buf, SIZE, out, SIZE));
}

static void test_contiguous(void)
{
    dma_write(3u, 10u, 100u);
    uint8_t out[SIZE];
    TEST_ASSERT_EQUAL_UINT32(3u, dma_ring_read(&r, &w, buf, SIZE, out, SIZE));
    const uint8_t expect[3] = { 10, 11, 12 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 3);
    TEST_ASSERT_EQUAL_UINT32(0u, dma_ring_read(&r, &w, buf, SIZE, out, SIZE));
}

static void test_wraps_past_end(void)
{
    dma_write(4u, 0u, 100u);
    dma_write(2u, 4u, 200u);
    uint8_t out[SIZE];
    TEST_ASSERT_EQUAL_UINT32(6u, dma_ring_read(&r, &w, buf, SIZE, out, SIZE));
    dma_write(4u, 20u, 300u); /* 位置 6、7、0、1 */
    TEST_ASSERT_EQUAL_UINT32(4u, dma_ring_read(&r, &w, buf, SIZE, out, SIZE));
    const uint8_t expect[4] = { 20, 21, 22, 23 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 4);
}

static void test_max_len_leaves_rest_for_next_call(void)
{
    dma_write(4u, 0u, 100u);
    uint8_t out[SIZE];
    TEST_ASSERT_EQUAL_UINT32(3u, dma_ring_read(&r, &w, buf, SIZE, out, 3u));
    TEST_ASSERT_EQUAL_UINT32(1u, dma_ring_read(&r, &w, buf, SIZE, out, 3u));
    TEST_ASSERT_EQUAL_UINT8(3u, out[0]);
}

/* 正好绕一整圈：读写位置相同，但不是“空”，是未读数据被覆盖 —— 丢弃并计一次 */
static void test_exactly_one_lap_is_overrun(void)
{
    dma_write(4u, 0u, 100u);
    dma_write(4u, 4u, 200u);
    uint8_t out[SIZE];
    TEST_ASSERT_EQUAL_UINT32(0u, dma_ring_read(&r, &w, buf, SIZE, out, SIZE));
    TEST_ASSERT_EQUAL_UINT32(1u, r.overruns);

    dma_write(2u, 30u, 300u); /* 之后的新数据照常读 */
    TEST_ASSERT_EQUAL_UINT32(2u, dma_ring_read(&r, &w, buf, SIZE, out, SIZE));
    TEST_ASSERT_EQUAL_UINT8(30u, out[0]);
}

static void test_more_than_one_lap_is_overrun(void)
{
    for (int i = 0; i < 5; i++)
    {
        dma_write(3u, (uint8_t)(3 * i), 100u);
    }
    uint8_t out[SIZE];
    TEST_ASSERT_EQUAL_UINT32(0u, dma_ring_read(&r, &w, buf, SIZE, out, SIZE));
    TEST_ASSERT_EQUAL_UINT32(1u, r.overruns);
}

/* 出错后重新启动：DMA 从开头重新写，重启前没读的作废，重启后的照常读 */
static void test_restart_resets_to_start(void)
{
    dma_write(3u, 0u, 100u);
    dma_ring_on_restart(&w);
    dma_pos = 0u;
    dma_write(2u, 50u, 200u);
    uint8_t out[SIZE];
    TEST_ASSERT_EQUAL_UINT32(2u, dma_ring_read(&r, &w, buf, SIZE, out, SIZE));
    TEST_ASSERT_EQUAL_UINT8(50u, out[0]);
}

/* 接收时刻是中断里最近一次事件的时刻，与任务什么时候读无关 */
static void test_stamp_is_event_time(void)
{
    dma_write(2u, 0u, 1000u);
    dma_write(2u, 2u, 2000u);
    TEST_ASSERT_EQUAL_UINT64(2000u, w.stamp_us);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_new_data);
    RUN_TEST(test_contiguous);
    RUN_TEST(test_wraps_past_end);
    RUN_TEST(test_max_len_leaves_rest_for_next_call);
    RUN_TEST(test_exactly_one_lap_is_overrun);
    RUN_TEST(test_more_than_one_lap_is_overrun);
    RUN_TEST(test_restart_resets_to_start);
    RUN_TEST(test_stamp_is_event_time);
    return UNITY_END();
}
