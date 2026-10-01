/**
 * @file    test_can_dlc.c
 * @brief   can_dlc 的单元测试：经典帧长度、FD 长度、非法长度
 */
#include "05_platform/can/can_dlc.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_classic_lengths_are_identity);
    RUN_TEST(test_fd_lengths_round_trip);
    RUN_TEST(test_invalid_lengths_rejected);
    return UNITY_END();
}
