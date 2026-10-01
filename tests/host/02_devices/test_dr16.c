/**
 * @file    test_dr16.c
 * @brief   dr16 的单元测试：解析、范围检查、按时间间隔分帧、拆段与粘连、保存最新一帧与喂狗
 */
#include "02_devices/remote/dr16.h"

#include <string.h>

#include "fake_time.h"
#include "unity.h"

/* 每个测试用一个新的 Dr16：看门狗每个实例只能登记一次（watchdog_register 的前提） */
static Dr16 pool[16];
static unsigned next_dr16;
static Dr16 *dr16;

void setUp(void)
{
    dr16 = &pool[next_dr16++];
    fake_time_set_us(10000000u);
    dr16_init(dr16);
}

void tearDown(void)
{
}

/*
 * 按 DBUS 协议组帧，与被测的解析代码独立：4 个 11 位通道和两个 2 位拨杆依次排成一个 48 位小端数，
 * 其后是鼠标、按键、拨轮。
 */
static void encode(uint8_t f[DR16_FRAME_LEN], const uint16_t ch[5], uint8_t sw_left,
                   uint8_t sw_right)
{
    const uint64_t v = (uint64_t)ch[0] | ((uint64_t)ch[1] << 11) | ((uint64_t)ch[2] << 22)
                       | ((uint64_t)ch[3] << 33) | ((uint64_t)sw_left << 44)
                       | ((uint64_t)sw_right << 46);
    memset(f, 0, DR16_FRAME_LEN);
    for (int i = 0; i < 6; i++)
    {
        f[i] = (uint8_t)(v >> (8 * i));
    }
    f[16] = (uint8_t)(ch[4] & 0xFFu);
    f[17] = (uint8_t)(ch[4] >> 8);
}

static void centered_frame(uint8_t f[DR16_FRAME_LEN])
{
    const uint16_t ch[5] = { 1024, 1024, 1024, 1024, 1024 };
    encode(f, ch, 3, 3);
}

/* 字节值由 Python 按同一布局独立算出，检验组帧函数本身 */
static void test_encoder_matches_reference_bytes(void)
{
    uint8_t f[DR16_FRAME_LEN];
    centered_frame(f);
    const uint8_t expect[6] = { 0x00, 0x04, 0x20, 0x00, 0x01, 0xF8 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, f, 6);

    const uint16_t ch[5] = { 364, 1684, 1024, 1000, 1024 };
    encode(f, ch, 1, 2);
    const uint8_t expect2[6] = { 0x6C, 0xA1, 0x34, 0x00, 0xD1, 0x97 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect2, f, 6);
}

static void test_decode_fields(void)
{
    uint8_t f[DR16_FRAME_LEN];
    const uint16_t ch[5] = { 364, 1684, 1024, 1000, 1100 };
    encode(f, ch, 1, 2);
    f[6] = 0x10; /* 鼠标 x = -240 = 0xFF10 */
    f[7] = 0xFF;
    f[12] = 1;
    f[14] = 0x01; /* W */
    f[15] = 0x80; /* B */

    RcState rc;
    TEST_ASSERT_TRUE(dr16_decode(f, &rc));
    TEST_ASSERT_EQUAL_INT16(-660, rc.ch[0]);
    TEST_ASSERT_EQUAL_INT16(660, rc.ch[1]);
    TEST_ASSERT_EQUAL_INT16(0, rc.ch[2]);
    TEST_ASSERT_EQUAL_INT16(-24, rc.ch[3]);
    TEST_ASSERT_EQUAL_INT16(76, rc.ch[4]);
    TEST_ASSERT_EQUAL_INT(RC_SW_UP, rc.sw[0]);
    TEST_ASSERT_EQUAL_INT(RC_SW_DOWN, rc.sw[1]);
    TEST_ASSERT_EQUAL_INT16(-240, rc.mouse_x);
    TEST_ASSERT_TRUE(rc.mouse_left);
    TEST_ASSERT_FALSE(rc.mouse_right);
    TEST_ASSERT_EQUAL_HEX16(RC_KEY_W | RC_KEY_B, rc.keys);
}

/* 摇杆差一个数超出 364–1684、拨杆为 0 都整帧丢弃；拨轮不检查 */
static void test_range_check(void)
{
    uint8_t f[DR16_FRAME_LEN];
    RcState rc;
    uint16_t ch[5] = { 1024, 1024, 1024, 1024, 0 };

    encode(f, ch, 3, 3);
    TEST_ASSERT_TRUE(dr16_decode(f, &rc)); /* 拨轮为 0 仍接受 */

    for (int i = 0; i < 4; i++)
    {
        ch[i] = 363;
        encode(f, ch, 3, 3);
        TEST_ASSERT_FALSE(dr16_decode(f, &rc));
        ch[i] = 1685;
        encode(f, ch, 3, 3);
        TEST_ASSERT_FALSE(dr16_decode(f, &rc));
        ch[i] = 1024;
    }
    encode(f, ch, 0, 3);
    TEST_ASSERT_FALSE(dr16_decode(f, &rc));
    encode(f, ch, 3, 0);
    TEST_ASSERT_FALSE(dr16_decode(f, &rc));
}

static void test_good_frame_publishes_and_feeds(void)
{
    uint8_t f[DR16_FRAME_LEN];
    RcState rc;
    centered_frame(f);
    TEST_ASSERT_FALSE(dr16_read(dr16, &rc));
    TEST_ASSERT_FALSE(watchdog_is_online(&dr16->wd));

    dr16_on_bytes(dr16, f, DR16_FRAME_LEN, 10000000u);
    TEST_ASSERT_TRUE(dr16_read(dr16, &rc));
    TEST_ASSERT_EQUAL_INT(RC_SW_MID, rc.sw[0]);
    TEST_ASSERT_TRUE(watchdog_is_online(&dr16->wd));
}

static void test_bad_frame_not_published(void)
{
    uint8_t f[DR16_FRAME_LEN];
    RcState rc;
    const uint16_t ch[5] = { 100, 1024, 1024, 1024, 1024 };
    encode(f, ch, 3, 3);
    dr16_on_bytes(dr16, f, DR16_FRAME_LEN, 10000000u);
    TEST_ASSERT_FALSE(dr16_read(dr16, &rc));
    TEST_ASSERT_EQUAL_UINT32(1, dr16->bad_frames);
}

/* 一帧被切成两段、间隔小于 6 ms：拼起来 */
static void test_split_frame_is_joined(void)
{
    uint8_t f[DR16_FRAME_LEN];
    RcState rc;
    centered_frame(f);
    dr16_on_bytes(dr16, f, 9, 10000000u);
    dr16_on_bytes(dr16, f + 9, 9, 10001000u);
    TEST_ASSERT_TRUE(dr16_read(dr16, &rc));
    TEST_ASSERT_EQUAL_UINT32(0, dr16->bad_frames);
}

/* 开始接收时只收到上一帧的后半段：空闲超过 6 ms 后，下一段从帧头重新开始 */
static void test_gap_resyncs(void)
{
    uint8_t f[DR16_FRAME_LEN];
    RcState rc;
    centered_frame(f);
    dr16_on_bytes(dr16, f + 5, DR16_FRAME_LEN - 5, 10000000u);  /* 残帧 */
    dr16_on_bytes(dr16, f, DR16_FRAME_LEN, 10000000u + 12000u); /* 12 ms 后的完整一帧 */
    TEST_ASSERT_TRUE(dr16_read(dr16, &rc));
    TEST_ASSERT_EQUAL_UINT32(0, dr16->bad_frames);
}

/* 任务被耽误时一次读到两帧：依次解析，读到的是后一帧 */
static void test_two_frames_in_one_chunk(void)
{
    uint8_t two[2 * DR16_FRAME_LEN];
    const uint16_t first[5] = { 1024, 1024, 1024, 1024, 1024 };
    const uint16_t second[5] = { 1684, 1024, 1024, 1024, 1024 };
    encode(two, first, 3, 3);
    encode(two + DR16_FRAME_LEN, second, 3, 3);

    RcState rc;
    dr16_on_bytes(dr16, two, sizeof(two), 10000000u);
    TEST_ASSERT_TRUE(dr16_read(dr16, &rc));
    TEST_ASSERT_EQUAL_INT16(660, rc.ch[0]);
    TEST_ASSERT_EQUAL_UINT32(0, dr16->bad_frames);
}

/* 超过 DR16_TIMEOUT_MS 没有合法帧：dr16_read 返回 false，即遥控丢失（ADR 0030） */
static void test_lost_after_timeout(void)
{
    uint8_t f[DR16_FRAME_LEN];
    RcState rc;
    centered_frame(f);
    dr16_on_bytes(dr16, f, DR16_FRAME_LEN, 10000000u);
    fake_time_advance_ms(DR16_TIMEOUT_MS);
    TEST_ASSERT_TRUE(dr16_read(dr16, &rc));
    TEST_ASSERT_TRUE(watchdog_is_online(&dr16->wd));
    fake_time_advance_ms(1u);
    TEST_ASSERT_FALSE(dr16_read(dr16, &rc));
    TEST_ASSERT_FALSE(watchdog_is_online(&dr16->wd)); /* 日志和控制同一个判断 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encoder_matches_reference_bytes);
    RUN_TEST(test_decode_fields);
    RUN_TEST(test_range_check);
    RUN_TEST(test_good_frame_publishes_and_feeds);
    RUN_TEST(test_bad_frame_not_published);
    RUN_TEST(test_split_frame_is_joined);
    RUN_TEST(test_gap_resyncs);
    RUN_TEST(test_two_frames_in_one_chunk);
    RUN_TEST(test_lost_after_timeout);
    return UNITY_END();
}
