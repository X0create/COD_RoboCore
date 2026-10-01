/**
 * @file    test_byte_ring.c
 * @brief   byte_ring 的单元测试：先进先出、写满丢弃并计数、回绕、分次取出
 */
#include "05_platform/usb_cdc/byte_ring.h"

#include "unity.h"

static ByteRing ring;

void setUp(void)
{
    ring = (ByteRing){ 0 };
}

void tearDown(void)
{
}

static void test_fifo(void)
{
    const uint8_t in[5] = { 1, 2, 3, 4, 5 };
    uint8_t out[8];
    TEST_ASSERT_EQUAL_UINT32(5, byte_ring_push(&ring, in, 5));
    TEST_ASSERT_EQUAL_UINT32(3, byte_ring_pop(&ring, out, 3));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, 3);
    TEST_ASSERT_EQUAL_UINT32(2, byte_ring_pop(&ring, out, 8));
    TEST_ASSERT_EQUAL_UINT8(4, out[0]);
    TEST_ASSERT_EQUAL_UINT32(0, byte_ring_pop(&ring, out, 8));
}

/* 容量 BYTE_RING_SIZE - 1：多出来的丢弃并计数 */
static void test_full_drops_and_counts(void)
{
    static uint8_t big[BYTE_RING_SIZE + 10];
    for (uint32_t i = 0u; i < sizeof(big); i++)
    {
        big[i] = (uint8_t)i;
    }
    TEST_ASSERT_EQUAL_UINT32(BYTE_RING_SIZE - 1u, byte_ring_push(&ring, big, sizeof(big)));
    TEST_ASSERT_EQUAL_UINT32(11, ring.dropped);
}

/* 反复写读越过数组末尾，数据仍按顺序 */
static void test_wraparound(void)
{
    uint8_t in[100];
    uint8_t out[100];
    uint8_t next = 0u;
    uint8_t expect = 0u;
    for (int round = 0; round < 50; round++)
    {
        for (int i = 0; i < 100; i++)
        {
            in[i] = next++;
        }
        TEST_ASSERT_EQUAL_UINT32(100, byte_ring_push(&ring, in, 100));
        TEST_ASSERT_EQUAL_UINT32(100, byte_ring_pop(&ring, out, 100));
        for (int i = 0; i < 100; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(expect++, out[i]);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fifo);
    RUN_TEST(test_full_drops_and_counts);
    RUN_TEST(test_wraparound);
    return UNITY_END();
}
