/**
 * @file    test_crc.c
 * @brief   crc 的单元测试：公开校验值、查表与逐位算法一致、分段计算、追加与校验
 */
#include "core/util/crc.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/*
 * 逐位计算的参考实现，按 crc.h 写明的参数（反射多项式、初值、无结果异或）独立写出，不用被测模块的表。
 * 查表数组抄错一项时，两者会对不上，而不是“自己和自己一致”。
 */
static uint8_t ref_crc8(const uint8_t *data, size_t len, uint8_t crc)
{
    for (size_t i = 0u; i < len; i++)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 1u) ? (uint8_t)((crc >> 1) ^ 0x8Cu) : (uint8_t)(crc >> 1);
        }
    }
    return crc;
}

static uint16_t ref_crc16(const uint8_t *data, size_t len, uint16_t crc)
{
    for (size_t i = 0u; i < len; i++)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static const uint8_t check_string[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

/* CRC 目录中 CRC-16/MCRF4XX 公布的校验值，把本模块和仓库外的标准对上 */
static void test_crc16_matches_published_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x6F91u, crc16_calc(check_string, sizeof(check_string), CRC16_INIT));
}

/* 256 个单字节输入正好用到两张表的每一项 */
static void test_tables_match_bitwise_reference(void)
{
    for (unsigned v = 0u; v < 256u; v++)
    {
        const uint8_t byte = (uint8_t)v;
        TEST_ASSERT_EQUAL_HEX8(ref_crc8(&byte, 1u, CRC8_INIT), crc8_calc(&byte, 1u, CRC8_INIT));
        TEST_ASSERT_EQUAL_HEX16(ref_crc16(&byte, 1u, CRC16_INIT),
                                crc16_calc(&byte, 1u, CRC16_INIT));
    }
}

/* 每个前缀长度都比一次，循环边界差一的错误会在第一个出错的长度上暴露 */
static void test_every_length_matches_reference(void)
{
    uint8_t data[64];
    for (unsigned i = 0u; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)(i * 31u + 7u);
    }
    for (size_t len = 0u; len <= sizeof(data); len++)
    {
        TEST_ASSERT_EQUAL_HEX8(ref_crc8(data, len, CRC8_INIT), crc8_calc(data, len, CRC8_INIT));
        TEST_ASSERT_EQUAL_HEX16(ref_crc16(data, len, CRC16_INIT),
                                crc16_calc(data, len, CRC16_INIT));
    }
}

/* 把上一段的结果当初值接着算，等于一次算完 */
static void test_split_calc_equals_whole(void)
{
    const uint8_t first = crc8_calc(check_string, 4u, CRC8_INIT);
    TEST_ASSERT_EQUAL_HEX8(crc8_calc(check_string, 9u, CRC8_INIT),
                           crc8_calc(check_string + 4, 5u, first));

    const uint16_t first16 = crc16_calc(check_string, 4u, CRC16_INIT);
    TEST_ASSERT_EQUAL_HEX16(crc16_calc(check_string, 9u, CRC16_INIT),
                            crc16_calc(check_string + 4, 5u, first16));
}

/* 裁判系统帧头：SOF 0xA5、长度 2 字节、包序号，第 5 字节是 CRC8 */
static void test_crc8_append_then_verify(void)
{
    uint8_t header[5] = { 0xA5u, 0x0Au, 0x00u, 0x01u, 0x00u };
    crc8_append(header, sizeof(header));
    TEST_ASSERT_EQUAL_HEX8(ref_crc8(header, 4u, CRC8_INIT), header[4]);
    TEST_ASSERT_TRUE(crc8_verify(header, sizeof(header)));

    header[2] ^= 0x01u;
    TEST_ASSERT_FALSE(crc8_verify(header, sizeof(header)));
}

/* CRC16 追加在帧尾，低字节在前；改任何一个字节（含校验字节本身）都校验失败 */
static void test_crc16_append_little_endian_then_verify(void)
{
    uint8_t frame[11] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', 0u, 0u };
    crc16_append(frame, sizeof(frame));
    TEST_ASSERT_EQUAL_HEX8(0x91u, frame[9]);
    TEST_ASSERT_EQUAL_HEX8(0x6Fu, frame[10]);
    TEST_ASSERT_TRUE(crc16_verify(frame, sizeof(frame)));

    for (size_t i = 0u; i < sizeof(frame); i++)
    {
        frame[i] ^= 0x10u;
        TEST_ASSERT_FALSE(crc16_verify(frame, sizeof(frame)));
        frame[i] ^= 0x10u;
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_matches_published_check_value);
    RUN_TEST(test_tables_match_bitwise_reference);
    RUN_TEST(test_every_length_matches_reference);
    RUN_TEST(test_split_calc_equals_whole);
    RUN_TEST(test_crc8_append_then_verify);
    RUN_TEST(test_crc16_append_little_endian_then_verify);
    return UNITY_END();
}
