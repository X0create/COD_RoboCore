/**
 * @file    test_board_link.c
 * @brief   board_link 的单元测试：配置与 ID 冲突检查、发送帧格式、经典 / FD 长度、接收与长度检查、
 *          数据年龄计入超时、年龄未知不采用、丢帧计数、编码小工具
 */
#include "02_devices/board_link/board_link.h"

#include <math.h>
#include <string.h>

#include "02_devices/motor/motor_group.h"
#include "05_platform/time/time.h"
#include "fake_can.h"
#include "fake_time.h"
#include "unity.h"

/* 每个测试用新的 BoardLink 和新的 ID：已初始化的消息留在模块的链表里，看门狗也只能登记一次 */
static BoardLink pool[64];
static BoardLinkConfig cfgs[64];
static unsigned used;
static uint32_t next_id = 0x100u;
static MotorGroup group;

void setUp(void)
{
    fake_can_reset();
    fake_time_set_us(1000000u);
    group = (MotorGroup){ 0 };
}

void tearDown(void)
{
}

static const BoardLinkConfig *make_cfg(CanBusId bus, uint8_t payload_len)
{
    BoardLinkConfig *c = &cfgs[used];
    *c = (BoardLinkConfig){ .name = "link",
                            .can_bus = bus,
                            .id = next_id++,
                            .payload_len = payload_len,
                            .timeout_ms = 50u };
    return c;
}

static BoardLink *add(const BoardLinkConfig *cfg, BoardLinkDir dir)
{
    BoardLink *link = &pool[used++];
    TEST_ASSERT_EQUAL(BOARD_LINK_INIT_OK, board_link_init(link, cfg, dir, &group));
    return link;
}

/* 相当于 comm_rx_task：中断里记下接收时刻，交给这条消息 */
static bool deliver(BoardLink *link, CanBusId bus, uint32_t id, const uint8_t *data, uint8_t len)
{
    CanFrame frame = { .id = id, .len = len, .stamp_us = rm_time_now_us() };
    memcpy(frame.data, data, len);
    return board_link_receive(link, bus, &frame);
}

/* 组一帧经典帧：序号、年龄、6 字节载荷 */
static void classic_frame(uint8_t f[8], uint8_t seq, uint8_t age_ms, const uint8_t payload[6])
{
    f[0] = seq;
    f[1] = age_ms;
    memcpy(&f[2], payload, 6);
}

static void test_init_rejects_bad_config(void)
{
    BoardLink link;
    BoardLinkConfig c = { .name = "x", .can_bus = CAN_BUS_1, .id = 0x800u, .payload_len = 6u };
    TEST_ASSERT_EQUAL(BOARD_LINK_INIT_BAD_CONFIG,
                      board_link_init(&link, &c, BOARD_LINK_TX, &group));
    c.id = 0x7F0u;
    c.payload_len = 0u;
    TEST_ASSERT_EQUAL(BOARD_LINK_INIT_BAD_CONFIG,
                      board_link_init(&link, &c, BOARD_LINK_TX, &group));
    c.payload_len = 63u;
    fake_can_set_bus_fd(CAN_BUS_1, true);
    TEST_ASSERT_EQUAL(BOARD_LINK_INIT_BAD_CONFIG,
                      board_link_init(&link, &c, BOARD_LINK_TX, &group));
}

static void test_long_payload_needs_fd_bus(void)
{
    BoardLink link;
    const BoardLinkConfig *c = make_cfg(CAN_BUS_1, 7u);
    TEST_ASSERT_EQUAL(BOARD_LINK_INIT_NEEDS_FD, board_link_init(&link, c, BOARD_LINK_TX, &group));
}

static void test_id_used_by_motor_rejected(void)
{
    static const MotorConfig m3508 = { .name = "m",
                                       .type = MOTOR_M3508,
                                       .can_bus = CAN_BUS_1,
                                       .id = 1u,
                                       .direction = 1,
                                       .gear_ratio = DJI_M3508_GEAR_RATIO,
                                       .stop_action = STOP_ACTION_ZERO_TORQUE };
    static Motor motor;
    const Motor *conflict;
    TEST_ASSERT_TRUE(motor_init(&motor, &m3508, &group, &conflict));

    BoardLink link;
    /* 0x200 是电调 1–4 的控制帧：发出去电机会当成电流指令 */
    BoardLinkConfig c = { .name = "x", .can_bus = CAN_BUS_1, .id = 0x200u, .payload_len = 6u };
    TEST_ASSERT_EQUAL(BOARD_LINK_INIT_MOTOR_CONFLICT,
                      board_link_init(&link, &c, BOARD_LINK_TX, &group));
    c.id = 0x201u; /* 电调 1 的反馈 */
    TEST_ASSERT_EQUAL(BOARD_LINK_INIT_MOTOR_CONFLICT,
                      board_link_init(&link, &c, BOARD_LINK_RX, &group));
    static BoardLinkConfig other_bus; /* 初始化成功的消息留在模块链表里，配置和对象都要静态 */
    other_bus = c;
    other_bus.can_bus = CAN_BUS_2; /* 别的总线上不冲突 */
    (void)add(&other_bus, BOARD_LINK_TX);
}

static void test_same_id_on_same_bus_rejected(void)
{
    const BoardLinkConfig *a = make_cfg(CAN_BUS_1, 6u);
    (void)add(a, BOARD_LINK_TX);

    BoardLink link;
    static BoardLinkConfig same;
    same = *a;
    TEST_ASSERT_EQUAL(BOARD_LINK_INIT_LINK_CONFLICT,
                      board_link_init(&link, &same, BOARD_LINK_RX, &group));
    same.can_bus = CAN_BUS_3;
    (void)add(&same, BOARD_LINK_RX);
}

static void test_send_classic_frame_layout(void)
{
    const BoardLinkConfig *c = make_cfg(CAN_BUS_1, 6u);
    BoardLink *link = add(c, BOARD_LINK_TX);
    const uint8_t payload[6] = { 1, 2, 3, 4, 5, 6 };

    TEST_ASSERT_TRUE(board_link_send(link, payload, 3u));
    TEST_ASSERT_TRUE(board_link_send(link, payload, 300u));

    TEST_ASSERT_EQUAL_UINT32(2u, fake_can_sent_count());
    const CanFrame *f0 = fake_can_sent(0);
    TEST_ASSERT_EQUAL_HEX32(c->id, f0->id);
    TEST_ASSERT_EQUAL_UINT8(8u, f0->len);
    TEST_ASSERT_FALSE(f0->is_fd);
    const uint8_t expect0[8] = { 0, 3, 1, 2, 3, 4, 5, 6 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect0, f0->data, 8);

    const CanFrame *f1 = fake_can_sent(1);
    TEST_ASSERT_EQUAL_UINT8(1u, f1->data[0]);                     /* 序号递增 */
    TEST_ASSERT_EQUAL_UINT8(BOARD_LINK_AGE_UNKNOWN, f1->data[1]); /* ≥ 255 ms 按未知发 */
}

static void test_short_payload_sends_short_frame(void)
{
    BoardLink *link = add(make_cfg(CAN_BUS_1, 2u), BOARD_LINK_TX);
    const uint8_t payload[2] = { 0xAA, 0xBB };
    TEST_ASSERT_TRUE(board_link_send(link, payload, 0u));
    TEST_ASSERT_EQUAL_UINT8(4u, fake_can_sent(0)->len);
}

static void test_fd_payload_rounds_up_to_fd_size(void)
{
    fake_can_set_bus_fd(CAN_BUS_2, true);
    BoardLink *link = add(make_cfg(CAN_BUS_2, 11u), BOARD_LINK_TX); /* 2 + 11 = 13 → 16 */
    uint8_t payload[11];
    memset(payload, 0x5A, sizeof(payload));
    TEST_ASSERT_TRUE(board_link_send(link, payload, 0u));

    const CanFrame *f = fake_can_sent(0);
    TEST_ASSERT_TRUE(f->is_fd);
    TEST_ASSERT_EQUAL_UINT8(16u, f->len);
    TEST_ASSERT_EQUAL_HEX8(0x5A, f->data[12]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f->data[13]); /* 填充 */
    TEST_ASSERT_EQUAL_HEX8(0x00, f->data[15]);
}

static void test_send_failure_reported(void)
{
    BoardLink *link = add(make_cfg(CAN_BUS_1, 6u), BOARD_LINK_TX);
    const uint8_t payload[6] = { 0 };
    fake_can_set_send_fail(true);
    TEST_ASSERT_FALSE(board_link_send(link, payload, 0u));
}

static void test_receive_and_read(void)
{
    const BoardLinkConfig *c = make_cfg(CAN_BUS_1, 6u);
    BoardLink *link = add(c, BOARD_LINK_RX);
    uint8_t out[6];
    TEST_ASSERT_FALSE(board_link_read(link, out)); /* 从未收到 */

    const uint8_t payload[6] = { 9, 8, 7, 6, 5, 4 };
    uint8_t f[8];
    classic_frame(f, 0u, 0u, payload);
    TEST_ASSERT_FALSE(deliver(link, CAN_BUS_2, c->id, f, 8));      /* 总线不对 */
    TEST_ASSERT_FALSE(deliver(link, CAN_BUS_1, c->id + 1u, f, 8)); /* ID 不对 */
    TEST_ASSERT_TRUE(deliver(link, CAN_BUS_1, c->id, f, 8));

    TEST_ASSERT_TRUE(board_link_read(link, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, out, 6);

    fake_time_advance_ms(51u);
    TEST_ASSERT_FALSE(board_link_read(link, out)); /* 超时离线 */
}

static void test_wrong_length_dropped(void)
{
    const BoardLinkConfig *c = make_cfg(CAN_BUS_1, 6u);
    BoardLink *link = add(c, BOARD_LINK_RX);
    const uint8_t f[8] = { 0 };
    TEST_ASSERT_TRUE(deliver(link, CAN_BUS_1, c->id, f, 7)); /* 是它的，但不采用 */
    uint8_t out[6];
    TEST_ASSERT_FALSE(board_link_read(link, out));
    TEST_ASSERT_EQUAL_UINT32(1u, link->bad_frames);
}

static void test_data_age_counts_toward_timeout(void)
{
    const BoardLinkConfig *c = make_cfg(CAN_BUS_1, 6u); /* 超时 50 ms */
    BoardLink *link = add(c, BOARD_LINK_RX);
    const uint8_t payload[6] = { 0 };
    uint8_t f[8];
    classic_frame(f, 0u, 40u, payload); /* 对方的数据已经 40 ms 了 */
    TEST_ASSERT_TRUE(deliver(link, CAN_BUS_1, c->id, f, 8));

    uint8_t out[6];
    fake_time_advance_ms(10u);
    TEST_ASSERT_TRUE(board_link_read(link, out)); /* 40 + 10 = 50：正好等于超时还算在线 */
    fake_time_advance_ms(1u);
    TEST_ASSERT_FALSE(board_link_read(link, out)); /* 51 ms */
}

static void test_unknown_age_not_used(void)
{
    const BoardLinkConfig *c = make_cfg(CAN_BUS_1, 6u);
    BoardLink *link = add(c, BOARD_LINK_RX);
    const uint8_t payload[6] = { 1, 1, 1, 1, 1, 1 };
    uint8_t f[8];
    classic_frame(f, 0u, BOARD_LINK_AGE_UNKNOWN, payload);
    TEST_ASSERT_TRUE(deliver(link, CAN_BUS_1, c->id, f, 8));
    uint8_t out[6];
    TEST_ASSERT_FALSE(board_link_read(link, out));
}

static void test_lost_frames_counted_with_wraparound(void)
{
    const BoardLinkConfig *c = make_cfg(CAN_BUS_1, 6u);
    BoardLink *link = add(c, BOARD_LINK_RX);
    const uint8_t payload[6] = { 0 };
    uint8_t f[8];

    classic_frame(f, 250u, 0u, payload);
    TEST_ASSERT_TRUE(deliver(link, CAN_BUS_1, c->id, f, 8));
    classic_frame(f, 251u, 0u, payload);
    TEST_ASSERT_TRUE(deliver(link, CAN_BUS_1, c->id, f, 8));
    TEST_ASSERT_EQUAL_UINT32(0u, link->lost);

    classic_frame(f, 2u, 0u, payload); /* 252–255、0、1 共 6 帧丢了 */
    TEST_ASSERT_TRUE(deliver(link, CAN_BUS_1, c->id, f, 8));
    TEST_ASSERT_EQUAL_UINT32(6u, link->lost);
}

static void test_fd_receive(void)
{
    fake_can_set_bus_fd(CAN_BUS_2, true);
    const BoardLinkConfig *c = make_cfg(CAN_BUS_2, 30u); /* 2 + 30 = 32 */
    BoardLink *link = add(c, BOARD_LINK_RX);
    uint8_t f[32] = { 0 };
    for (uint8_t i = 0; i < 30u; i++)
    {
        f[2u + i] = i;
    }
    TEST_ASSERT_TRUE(deliver(link, CAN_BUS_2, c->id, f, 32));
    uint8_t out[30];
    TEST_ASSERT_TRUE(board_link_read(link, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&f[2], out, 30);
}

/* 小端：低字节在前；字节值手算 */
static void test_put_get_little_endian(void)
{
    uint8_t b[2];
    board_link_put_u16(b, 0x1234u);
    TEST_ASSERT_EQUAL_HEX8(0x34, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x12, b[1]);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, board_link_get_u16(b));

    board_link_put_i16(b, -2); /* 0xFFFE */
    TEST_ASSERT_EQUAL_HEX8(0xFE, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, b[1]);
    TEST_ASSERT_EQUAL_INT16(-2, board_link_get_i16(b));
}

static void test_float_to_i16_scales_saturates_and_marks_nan(void)
{
    TEST_ASSERT_EQUAL_INT16(1235, board_link_float_to_i16(1.2346f, 0.001f)); /* 四舍五入 */
    TEST_ASSERT_EQUAL_INT16(-500, board_link_float_to_i16(-0.5f, 0.001f));
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, board_link_float_to_i16(100.0f, 0.001f));
    TEST_ASSERT_EQUAL_INT16(-INT16_MAX,
                            board_link_float_to_i16(-100.0f, 0.001f)); /* 不占用无效值 */
    TEST_ASSERT_EQUAL_INT16(BOARD_LINK_I16_INVALID, board_link_float_to_i16(NAN, 0.001f));

    float v = 0.0f;
    TEST_ASSERT_TRUE(board_link_i16_to_float(1235, 0.001f, &v));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.235f, v);
    TEST_ASSERT_FALSE(board_link_i16_to_float(BOARD_LINK_I16_INVALID, 0.001f, &v));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.235f, v); /* 无效时不改 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_rejects_bad_config);
    RUN_TEST(test_long_payload_needs_fd_bus);
    RUN_TEST(test_id_used_by_motor_rejected);
    RUN_TEST(test_same_id_on_same_bus_rejected);
    RUN_TEST(test_send_classic_frame_layout);
    RUN_TEST(test_short_payload_sends_short_frame);
    RUN_TEST(test_fd_payload_rounds_up_to_fd_size);
    RUN_TEST(test_send_failure_reported);
    RUN_TEST(test_receive_and_read);
    RUN_TEST(test_wrong_length_dropped);
    RUN_TEST(test_data_age_counts_toward_timeout);
    RUN_TEST(test_unknown_age_not_used);
    RUN_TEST(test_lost_frames_counted_with_wraparound);
    RUN_TEST(test_fd_receive);
    RUN_TEST(test_put_get_little_endian);
    RUN_TEST(test_float_to_i16_scales_saturates_and_marks_nan);
    return UNITY_END();
}
