/**
 * @file    test_vt_link.c
 * @brief   vt_link 的单元测试：VT13 解析与范围 / CRC、键鼠 0x0304、字节流中夹杂垃圾、拆段、错位恢复
 */
#include "02_devices/remote/vt_link.h"

#include <string.h>

#include "02_devices/referee/ref_frame.h"
#include "04_core/util/crc.h"
#include "fake_time.h"
#include "unity.h"

/* 每个测试用新的 VtLink：看门狗每个实例只能登记一次 */
static VtLink pool[16];
static unsigned pool_used;
static VtLink *vt;
static VtRcStateTopic rc_topic;
static KbmStateTopic kbm_topic;

void setUp(void)
{
    rc_topic = (VtRcStateTopic){ 0 };
    kbm_topic = (KbmStateTopic){ 0 };
    fake_time_set_us(1000000u);
    vt = &pool[pool_used++];
    TEST_ASSERT_TRUE(vt_link_init(vt, &rc_topic, &kbm_topic));
}

void tearDown(void)
{
}

/* 按 VT13 位布局组帧（与解析代码独立：4 个 11 位通道 + 各位域依次排成小端位流） */
static void vt13_frame(uint8_t f[VT13_FRAME_LEN], const uint16_t ch[4], uint8_t mode, bool pause,
                       bool left, bool right, uint16_t wheel, bool trigger)
{
    memset(f, 0, VT13_FRAME_LEN);
    f[0] = 0xA9u;
    f[1] = 0x53u;
    uint64_t v = (uint64_t)ch[0] | ((uint64_t)ch[1] << 11) | ((uint64_t)ch[2] << 22)
                 | ((uint64_t)ch[3] << 33) | ((uint64_t)mode << 44) | ((uint64_t)pause << 46)
                 | ((uint64_t)left << 47) | ((uint64_t)right << 48) | ((uint64_t)wheel << 49)
                 | ((uint64_t)trigger << 60);
    for (int i = 0; i < 8; i++)
    {
        f[2 + i] = (uint8_t)(v >> (8 * i));
    }
    f[10] = 0x10; /* 鼠标 x = -240 */
    f[11] = 0xFF;
    f[16] = 0x21; /* 左键 1、右键 0、中键 2 */
    f[17] = 0x01; /* W */
    crc16_append(f, VT13_FRAME_LEN);
}

static void centered(uint8_t f[VT13_FRAME_LEN])
{
    const uint16_t ch[4] = { 1024, 1024, 1024, 1024 };
    vt13_frame(f, ch, VT_MODE_N, false, false, false, 1024, false);
}

static size_t kbm_frame(uint8_t *out, uint16_t cmd, uint16_t keys)
{
    uint8_t d[12] = { 0 };
    d[0] = 100; /* 鼠标 x = 100 */
    d[6] = 1;   /* 左键 */
    d[8] = (uint8_t)(keys & 0xFFu);
    d[9] = (uint8_t)(keys >> 8);
    out[0] = 0xA5u;
    out[1] = 12u;
    out[2] = 0u;
    out[3] = 0u;
    crc8_append(out, 5u);
    out[5] = (uint8_t)(cmd & 0xFFu);
    out[6] = (uint8_t)(cmd >> 8);
    memcpy(&out[7], d, sizeof(d));
    crc16_append(out, 21u);
    return 21u;
}

static void test_vt13_decode_fields(void)
{
    uint8_t f[VT13_FRAME_LEN];
    const uint16_t ch[4] = { 364, 1684, 1024, 1000 };
    vt13_frame(f, ch, VT_MODE_S, true, true, false, 1100, true);
    VtRcState rc;
    TEST_ASSERT_TRUE(vt13_decode(f, &rc));
    TEST_ASSERT_EQUAL_INT16(-660, rc.ch[0]);
    TEST_ASSERT_EQUAL_INT16(660, rc.ch[1]);
    TEST_ASSERT_EQUAL_INT16(0, rc.ch[2]);
    TEST_ASSERT_EQUAL_INT16(-24, rc.ch[3]);
    TEST_ASSERT_EQUAL_INT(VT_MODE_S, rc.mode);
    TEST_ASSERT_TRUE(rc.pause);
    TEST_ASSERT_TRUE(rc.custom_left);
    TEST_ASSERT_FALSE(rc.custom_right);
    TEST_ASSERT_EQUAL_INT16(76, rc.wheel);
    TEST_ASSERT_TRUE(rc.trigger);
    TEST_ASSERT_EQUAL_INT16(-240, rc.mouse_x);
    TEST_ASSERT_EQUAL_UINT8(1, rc.mouse_left);
    TEST_ASSERT_EQUAL_UINT8(0, rc.mouse_right);
    TEST_ASSERT_EQUAL_UINT8(2, rc.mouse_middle);
    TEST_ASSERT_EQUAL_HEX16(0x0001, rc.keys);
}

static void test_vt13_rejects_bad_crc_and_range(void)
{
    uint8_t f[VT13_FRAME_LEN];
    VtRcState rc;
    centered(f);
    f[5] ^= 0x01u;
    TEST_ASSERT_FALSE(vt13_decode(f, &rc));

    const uint16_t ch[4] = { 1024, 100, 1024, 1024 };
    vt13_frame(f, ch, VT_MODE_N, false, false, false, 1024, false);
    TEST_ASSERT_FALSE(vt13_decode(f, &rc));
}

static void test_kbm_frame_published(void)
{
    uint8_t f[32];
    const size_t n = kbm_frame(f, 0x0304u, 0x8001u);
    vt_link_on_bytes(vt, f, (uint32_t)n);
    KbmState kbm;
    TEST_ASSERT_TRUE(kbm_state_read(&kbm_topic, &kbm, TOPIC_ANY_AGE));
    TEST_ASSERT_EQUAL_INT16(100, kbm.mouse_x);
    TEST_ASSERT_TRUE(kbm.mouse_left);
    TEST_ASSERT_FALSE(kbm.mouse_right);
    TEST_ASSERT_EQUAL_HEX16(0x8001, kbm.keys);
}

/* 垃圾字节 + VT13 帧 + 键鼠帧，拆成很多小段送入：两帧都解析出来，垃圾字节被计数 */
static void test_stream_with_garbage_and_splits(void)
{
    uint8_t stream[80];
    size_t n = 0u;
    const uint8_t garbage[5] = { 0x00, 0xA5, 0x12, 0xA9, 0x00 }; /* 含假帧头 */
    memcpy(stream, garbage, sizeof(garbage));
    n += sizeof(garbage);
    centered(&stream[n]);
    n += VT13_FRAME_LEN;
    n += kbm_frame(&stream[n], 0x0304u, 0x0002u);

    for (size_t i = 0u; i < n; i += 3u)
    {
        const size_t chunk = (n - i < 3u) ? n - i : 3u;
        vt_link_on_bytes(vt, &stream[i], (uint32_t)chunk);
    }
    VtRcState rc;
    KbmState kbm;
    TEST_ASSERT_TRUE(vt_rc_state_read(&rc_topic, &rc, TOPIC_ANY_AGE));
    TEST_ASSERT_EQUAL_INT(VT_MODE_N, rc.mode);
    TEST_ASSERT_TRUE(kbm_state_read(&kbm_topic, &kbm, TOPIC_ANY_AGE));
    TEST_ASSERT_EQUAL_HEX16(0x0002, kbm.keys);
    TEST_ASSERT_EQUAL_UINT32(sizeof(garbage), vt->bad_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, vt->len);
}

/* 一帧中途损坏：丢弃后能从下一帧恢复 */
static void test_recovers_after_corrupt_frame(void)
{
    uint8_t stream[64];
    centered(stream);
    stream[8] ^= 0x40u; /* 第一帧 CRC 不对 */
    const uint16_t ch[4] = { 1684, 1024, 1024, 1024 };
    vt13_frame(&stream[VT13_FRAME_LEN], ch, VT_MODE_C, false, false, false, 1024, false);
    vt_link_on_bytes(vt, stream, 2u * VT13_FRAME_LEN);
    VtRcState rc;
    TEST_ASSERT_TRUE(vt_rc_state_read(&rc_topic, &rc, TOPIC_ANY_AGE));
    TEST_ASSERT_EQUAL_INT16(660, rc.ch[0]);
    TEST_ASSERT_EQUAL_INT(VT_MODE_C, rc.mode);
}

/* 校验通过但不处理的命令（如 0x0302）：计数，不发布 */
static void test_other_commands_ignored(void)
{
    uint8_t f[32];
    const size_t n = kbm_frame(f, 0x0302u, 0u);
    vt_link_on_bytes(vt, f, (uint32_t)n);
    KbmState kbm;
    TEST_ASSERT_FALSE(kbm_state_read(&kbm_topic, &kbm, TOPIC_ANY_AGE));
    TEST_ASSERT_EQUAL_UINT32(1, vt->ignored_frames);
}

/* 合法帧喂看门狗 */
static void test_valid_frame_feeds_watchdog(void)
{
    uint8_t f[VT13_FRAME_LEN];
    centered(f);
    TEST_ASSERT_FALSE(watchdog_is_online(&vt->wd));
    vt_link_on_bytes(vt, f, VT13_FRAME_LEN);
    TEST_ASSERT_TRUE(watchdog_is_online(&vt->wd));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_vt13_decode_fields);
    RUN_TEST(test_vt13_rejects_bad_crc_and_range);
    RUN_TEST(test_kbm_frame_published);
    RUN_TEST(test_stream_with_garbage_and_splits);
    RUN_TEST(test_recovers_after_corrupt_frame);
    RUN_TEST(test_other_commands_ignored);
    RUN_TEST(test_valid_frame_feeds_watchdog);
    return UNITY_END();
}
