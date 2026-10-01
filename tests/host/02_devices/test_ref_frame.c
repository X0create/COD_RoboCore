/**
 * @file    test_ref_frame.c
 * @brief   ref_frame 的单元测试：合法帧、字节不够、帧头 / CRC8 / CRC16 / 长度错误、连续两帧
 */
#include "02_devices/referee/ref_frame.h"

#include <string.h>

#include "04_core/util/crc.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* 按帧格式组一帧，返回总长 */
static size_t build(uint8_t *out, uint16_t cmd, const uint8_t *data, uint16_t len)
{
    out[0] = REF_FRAME_SOF;
    out[1] = (uint8_t)(len & 0xFFu);
    out[2] = (uint8_t)(len >> 8);
    out[3] = 7u; /* 包序号，任意 */
    crc8_append(out, REF_FRAME_HEADER_LEN);
    out[5] = (uint8_t)(cmd & 0xFFu);
    out[6] = (uint8_t)(cmd >> 8);
    memcpy(&out[7], data, len);
    const size_t total = (size_t)len + REF_FRAME_OVERHEAD;
    crc16_append(out, total);
    return total;
}

static const uint8_t payload[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

static void test_good_frame(void)
{
    uint8_t buf[64];
    const size_t n = build(buf, 0x0304u, payload, sizeof(payload));
    RefFrame f;
    TEST_ASSERT_EQUAL_INT(REF_FRAME_OK, ref_frame_check(buf, n, 55u, &f));
    TEST_ASSERT_EQUAL_HEX16(0x0304, f.cmd_id);
    TEST_ASSERT_EQUAL_UINT16(12, f.data_len);
    TEST_ASSERT_EQUAL_UINT(21, f.frame_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.data, 12);
}

/* 每个不完整的前缀都是“字节不够”，不会误判为坏帧 */
static void test_every_prefix_needs_more(void)
{
    uint8_t buf[64];
    const size_t n = build(buf, 0x0304u, payload, sizeof(payload));
    RefFrame f;
    for (size_t len = 0u; len < n; len++)
    {
        TEST_ASSERT_EQUAL_INT(REF_FRAME_NEED_MORE, ref_frame_check(buf, len, 55u, &f));
    }
}

static void test_bad_frames(void)
{
    uint8_t buf[64];
    RefFrame f;
    size_t n = build(buf, 0x0304u, payload, sizeof(payload));
    buf[0] = 0x00u;
    TEST_ASSERT_EQUAL_INT(REF_FRAME_BAD, ref_frame_check(buf, n, 55u, &f));

    n = build(buf, 0x0304u, payload, sizeof(payload));
    buf[3] ^= 0x01u; /* 帧头 CRC8 不对 */
    TEST_ASSERT_EQUAL_INT(REF_FRAME_BAD, ref_frame_check(buf, n, 55u, &f));

    n = build(buf, 0x0304u, payload, sizeof(payload));
    buf[10] ^= 0x01u; /* 数据被改，CRC16 不对 */
    TEST_ASSERT_EQUAL_INT(REF_FRAME_BAD, ref_frame_check(buf, n, 55u, &f));

    n = build(buf, 0x0304u, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_INT(REF_FRAME_BAD, ref_frame_check(buf, n, 11u, &f)); /* 超过允许长度 */
}

static void test_two_frames_back_to_back(void)
{
    uint8_t buf[64];
    const size_t n1 = build(buf, 0x0304u, payload, sizeof(payload));
    const size_t n2 = build(buf + n1, 0x0001u, payload, 3u);
    RefFrame f;
    TEST_ASSERT_EQUAL_INT(REF_FRAME_OK, ref_frame_check(buf, n1 + n2, 55u, &f));
    TEST_ASSERT_EQUAL_UINT(n1, f.frame_len);
    TEST_ASSERT_EQUAL_INT(REF_FRAME_OK, ref_frame_check(buf + n1, n2, 55u, &f));
    TEST_ASSERT_EQUAL_HEX16(0x0001, f.cmd_id);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_good_frame);
    RUN_TEST(test_every_prefix_needs_more);
    RUN_TEST(test_bad_frames);
    RUN_TEST(test_two_frames_back_to_back);
    return UNITY_END();
}
