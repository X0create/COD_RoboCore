/**
 * @file    test_can_rx_ring.c
 * @brief   can_rx_ring 的单元测试：先进先出、满了丢新帧并计数、回绕
 */
#include "05_platform/can/can_rx_ring.h"

#include "unity.h"

static CanRxRing ring;

void setUp(void)
{
    ring = (CanRxRing){ 0 };
}

void tearDown(void)
{
}

static CanFrame frame_with_id(uint32_t id)
{
    CanFrame f = { 0 };
    f.id = id;
    f.len = 1u;
    f.data[0] = (uint8_t)id;
    return f;
}

static void test_empty_pop_fails(void)
{
    CanFrame out;
    TEST_ASSERT_FALSE(can_rx_ring_pop(&ring, &out));
}

static void test_fifo_order(void)
{
    CanFrame a = frame_with_id(0x201u);
    CanFrame b = frame_with_id(0x202u);
    TEST_ASSERT_TRUE(can_rx_ring_push(&ring, &a));
    TEST_ASSERT_TRUE(can_rx_ring_push(&ring, &b));

    CanFrame out;
    TEST_ASSERT_TRUE(can_rx_ring_pop(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(0x201u, out.id);
    TEST_ASSERT_TRUE(can_rx_ring_pop(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(0x202u, out.id);
    TEST_ASSERT_FALSE(can_rx_ring_pop(&ring, &out));
}

static void test_full_drops_new_frame_and_counts(void)
{
    for (uint32_t i = 0u; i < CAN_RX_RING_SIZE - 1u; i++)
    {
        CanFrame f = frame_with_id(i);
        TEST_ASSERT_TRUE(can_rx_ring_push(&ring, &f));
    }
    CanFrame extra = frame_with_id(0x7FFu);
    TEST_ASSERT_FALSE(can_rx_ring_push(&ring, &extra));
    TEST_ASSERT_EQUAL_UINT32(1u, ring.dropped);

    CanFrame out;
    TEST_ASSERT_TRUE(can_rx_ring_pop(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(0u, out.id); /* 丢的是新帧，旧帧还在 */
}

static void test_wraps_around(void)
{
    CanFrame out;
    for (uint32_t i = 0u; i < 3u * CAN_RX_RING_SIZE; i++)
    {
        CanFrame f = frame_with_id(i & CAN_STD_ID_MAX);
        TEST_ASSERT_TRUE(can_rx_ring_push(&ring, &f));
        TEST_ASSERT_TRUE(can_rx_ring_pop(&ring, &out));
        TEST_ASSERT_EQUAL_UINT32(i & CAN_STD_ID_MAX, out.id);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, ring.dropped);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_pop_fails);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_full_drops_new_frame_and_counts);
    RUN_TEST(test_wraps_around);
    return UNITY_END();
}
