/**
 * @file    test_vision_link.c
 * @brief   vision_frame / vision_link 的单元测试：组帧后能检查通过、各种坏帧、每个前缀、字节流找帧
 */
#include "02_devices/vision/vision_link.h"

#include "fake_time.h"
#include "unity.h"

static VisionLink pool[8];
static unsigned pool_used;
static VisionLink *link;

void setUp(void)
{
    fake_time_set_us(1000000u);
    link = &pool[pool_used++]; /* 看门狗每个实例只能登记一次 */
    vision_link_init(link);
}

void tearDown(void)
{
}

static const uint8_t payload[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };

/* 组帧：头 4 字节 + 数据 + CRC16；再检查应原样取回 */
static void test_encode_then_check(void)
{
    uint8_t buf[32];
    const size_t n = vision_frame_encode(0x02u, payload, sizeof(payload), buf);
    TEST_ASSERT_EQUAL_UINT(12, n);
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(6, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[2]);
    VisionFrame f;
    TEST_ASSERT_EQUAL_INT(VISION_FRAME_OK, vision_frame_check(buf, n, &f));
    TEST_ASSERT_EQUAL_HEX8(0x02, f.id);
    TEST_ASSERT_EQUAL_UINT8(6, f.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.data, 6);
    TEST_ASSERT_EQUAL_UINT(n, f.frame_len);
}

static void test_every_prefix_needs_more(void)
{
    uint8_t buf[32];
    const size_t n = vision_frame_encode(0x02u, payload, sizeof(payload), buf);
    VisionFrame f;
    for (size_t len = 0u; len < n; len++)
    {
        TEST_ASSERT_EQUAL_INT(VISION_FRAME_NEED_MORE, vision_frame_check(buf, len, &f));
    }
}

static void test_bad_frames(void)
{
    uint8_t buf[32];
    VisionFrame f;
    size_t n = vision_frame_encode(0x02u, payload, sizeof(payload), buf);
    buf[0] = 0xA5u;
    TEST_ASSERT_EQUAL_INT(VISION_FRAME_BAD, vision_frame_check(buf, n, &f));
    n = vision_frame_encode(0x02u, payload, sizeof(payload), buf);
    buf[2] ^= 0x01u; /* 帧头 CRC8 不对 */
    TEST_ASSERT_EQUAL_INT(VISION_FRAME_BAD, vision_frame_check(buf, n, &f));
    n = vision_frame_encode(0x02u, payload, sizeof(payload), buf);
    buf[6] ^= 0x01u; /* 整帧 CRC16 不对 */
    TEST_ASSERT_EQUAL_INT(VISION_FRAME_BAD, vision_frame_check(buf, n, &f));
}

/* 垃圾 + 两帧 + 一个坏帧 + 一帧，逐字节送入：3 帧通过，坏帧和垃圾按字节丢弃 */
static void test_stream(void)
{
    uint8_t stream[128];
    size_t n = 0u;
    stream[n++] = 0x00u;
    stream[n++] = 0x5Au; /* 假帧头 */
    n += vision_frame_encode(0x01u, payload, 3u, &stream[n]);
    n += vision_frame_encode(0x02u, payload, 6u, &stream[n]);
    const size_t bad_at = n;
    n += vision_frame_encode(0x03u, payload, 6u, &stream[n]);
    stream[bad_at + 5] ^= 0xFFu; /* 第三帧损坏 */
    n += vision_frame_encode(0x04u, payload, 0u, &stream[n]);
    for (size_t i = 0u; i < n; i++)
    {
        vision_link_on_bytes(link, &stream[i], 1u);
    }
    TEST_ASSERT_EQUAL_UINT32(3, link->frames);
    TEST_ASSERT_EQUAL_HEX8(0x04, link->last_id);
    TEST_ASSERT_EQUAL_UINT32(0, link->len);
    TEST_ASSERT_TRUE(watchdog_is_online(&link->wd));
}

/* 最长的帧（255 字节数据）一次送入也能解析 */
static void test_longest_frame(void)
{
    static uint8_t data[255];
    static uint8_t buf[300];
    for (int i = 0; i < 255; i++)
    {
        data[i] = (uint8_t)i;
    }
    const size_t n = vision_frame_encode(0x7Fu, data, 255u, buf);
    vision_link_on_bytes(link, buf, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32(1, link->frames);
    TEST_ASSERT_EQUAL_HEX8(0x7F, link->last_id);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_then_check);
    RUN_TEST(test_every_prefix_needs_more);
    RUN_TEST(test_bad_frames);
    RUN_TEST(test_stream);
    RUN_TEST(test_longest_frame);
    return UNITY_END();
}
