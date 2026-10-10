/**
 * @file    test_can.c
 * @brief   can 的辅助代码单元测试：DLC 换算（经典帧长度、FD 长度、非法长度）；
 *          接收环形缓冲（先进先出、满了丢新帧并计数、回绕）
 */
#include "05_platform/can/can.h"

#include "unity.h"

static CanRxRing ring;

void setUp(void)
{
    ring = (CanRxRing){ 0 };
}

void tearDown(void)
{
}

/* ---------------- DLC 换算 ---------------- */

static void test_classic_lengths_are_identity(void)
{
    for (uint8_t len = 0u; len <= 8u; len++)
    {
        uint8_t dlc = 0xFFu;
        TEST_ASSERT_TRUE(can_len_to_dlc(len, &dlc));
        TEST_ASSERT_EQUAL_UINT8(len, dlc);
        TEST_ASSERT_EQUAL_UINT8(len, can_dlc_to_len(dlc));
    }
}

static void test_fd_lengths_round_trip(void)
{
    const uint8_t lens[] = { 12, 16, 20, 24, 32, 48, 64 };
    for (uint8_t i = 0u; i < sizeof(lens); i++)
    {
        uint8_t dlc = 0u;
        TEST_ASSERT_TRUE(can_len_to_dlc(lens[i], &dlc));
        TEST_ASSERT_EQUAL_UINT8(9u + i, dlc);
        TEST_ASSERT_EQUAL_UINT8(lens[i], can_dlc_to_len(dlc));
    }
}

static void test_invalid_lengths_rejected(void)
{
    uint8_t dlc = 0u;
    TEST_ASSERT_FALSE(can_len_to_dlc(9u, &dlc));
    TEST_ASSERT_FALSE(can_len_to_dlc(10u, &dlc));
    TEST_ASSERT_FALSE(can_len_to_dlc(65u, &dlc));
}

/* ---------------- 接收环形缓冲 ---------------- */

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
    RUN_TEST(test_classic_lengths_are_identity);
    RUN_TEST(test_fd_lengths_round_trip);
    RUN_TEST(test_invalid_lengths_rejected);
    RUN_TEST(test_empty_pop_fails);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_full_drops_new_frame_and_counts);
    RUN_TEST(test_wraps_around);
    return UNITY_END();
}
