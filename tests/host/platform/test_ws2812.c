/**
 * @file    test_ws2812.c
 * @brief   ws2812_encode 的单元测试：颜色顺序 GRB、每位的编码、MSB 先发
 */
#include "ws2812.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_black_is_all_zero_codes(void)
{
    uint8_t out[WS2812_BYTES_PER_LED];
    ws2812_encode(out, 0u, 0u, 0u);
    for (uint32_t i = 0u; i < WS2812_BYTES_PER_LED; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(WS2812_CODE_0, out[i]);
    }
}

static void test_green_comes_first(void)
{
    uint8_t out[WS2812_BYTES_PER_LED];
    ws2812_encode(out, 0u, 0xFFu, 0u); /* 纯绿 */
    for (uint32_t i = 0u; i < 8u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(WS2812_CODE_1, out[i]); /* 前 8 字节是绿色 */
    }
    for (uint32_t i = 8u; i < WS2812_BYTES_PER_LED; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(WS2812_CODE_0, out[i]);
    }
}

static void test_red_is_second_and_msb_first(void)
{
    uint8_t out[WS2812_BYTES_PER_LED];
    ws2812_encode(out, 0x80u, 0u, 0x01u);           /* 红色最高位、蓝色最低位 */
    TEST_ASSERT_EQUAL_HEX8(WS2812_CODE_1, out[8]);  /* 红色第 1 个字节 = bit7 */
    TEST_ASSERT_EQUAL_HEX8(WS2812_CODE_0, out[15]); /* 红色 bit0 */
    TEST_ASSERT_EQUAL_HEX8(WS2812_CODE_0, out[16]); /* 蓝色 bit7 */
    TEST_ASSERT_EQUAL_HEX8(WS2812_CODE_1, out[23]); /* 蓝色 bit0 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_black_is_all_zero_codes);
    RUN_TEST(test_green_comes_first);
    RUN_TEST(test_red_is_second_and_msb_first);
    return UNITY_END();
}
