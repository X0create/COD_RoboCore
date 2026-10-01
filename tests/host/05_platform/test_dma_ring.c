/**
 * @file    test_dma_ring.c
 * @brief   dma_ring_take 的单元测试：无数据、连续一段、跨过末尾、长度受限
 */
#include "05_platform/uart/dma_ring.h"

#include "unity.h"

#define SIZE 8u

static const uint8_t buf[SIZE] = { 10, 11, 12, 13, 14, 15, 16, 17 };

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_no_new_data(void)
{
    uint8_t out[SIZE];
    uint32_t read = 3u;
    TEST_ASSERT_EQUAL_UINT32(0u, dma_ring_take(buf, SIZE, &read, 3u, out, SIZE));
    TEST_ASSERT_EQUAL_UINT32(3u, read);
}

static void test_contiguous(void)
{
    uint8_t out[SIZE];
    uint32_t read = 1u;
    TEST_ASSERT_EQUAL_UINT32(3u, dma_ring_take(buf, SIZE, &read, 4u, out, SIZE));
    const uint8_t expect[] = { 11, 12, 13 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 3);
    TEST_ASSERT_EQUAL_UINT32(4u, read);
}

static void test_wraps_past_end(void)
{
    uint8_t out[SIZE];
    uint32_t read = 6u;
    /* DMA 写完 6、7 后回到开头又写了 0、1 */
    TEST_ASSERT_EQUAL_UINT32(4u, dma_ring_take(buf, SIZE, &read, 2u, out, SIZE));
    const uint8_t expect[] = { 16, 17, 10, 11 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 4);
    TEST_ASSERT_EQUAL_UINT32(2u, read);
}

static void test_write_at_zero_after_wrap(void)
{
    uint8_t out[SIZE];
    uint32_t read = 5u;
    /* DMA 正好写到末尾、写位置回到 0 */
    TEST_ASSERT_EQUAL_UINT32(3u, dma_ring_take(buf, SIZE, &read, 0u, out, SIZE));
    const uint8_t expect[] = { 15, 16, 17 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 3);
    TEST_ASSERT_EQUAL_UINT32(0u, read);
}

static void test_max_len_leaves_rest_for_next_call(void)
{
    uint8_t out[SIZE];
    uint32_t read = 6u;
    TEST_ASSERT_EQUAL_UINT32(3u, dma_ring_take(buf, SIZE, &read, 2u, out, 3u));
    const uint8_t first[] = { 16, 17, 10 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, out, 3);
    TEST_ASSERT_EQUAL_UINT32(1u, read);

    TEST_ASSERT_EQUAL_UINT32(1u, dma_ring_take(buf, SIZE, &read, 2u, out, 3u));
    TEST_ASSERT_EQUAL_UINT8(11, out[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, read);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_new_data);
    RUN_TEST(test_contiguous);
    RUN_TEST(test_wraps_past_end);
    RUN_TEST(test_write_at_zero_after_wrap);
    RUN_TEST(test_max_len_leaves_rest_for_next_call);
    return UNITY_END();
}
