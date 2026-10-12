/**
 * @file    test_supercap.c
 * @brief   supercap 的单元测试：配置与 ID 冲突检查、拒绝 FD 总线、指令编码（截断）、反馈解码（负功率、版本）、
 *          接收与超时、长度或版本不对不喂狗、发送限频、发送队列满
 * @note    期望字节按 README.md 的协议表手算
 */
#include "02_devices/supercap/supercap.h"

#include <string.h>

#include "05_platform/time/time.h"
#include "tests/host/fakes/fake_can.h"
#include "tests/host/fakes/fake_time.h"
#include "unity.h"

/* 每个测试用新的对象：看门狗只能登记一次 */
static Supercap pool[32];
static unsigned used;
static MotorGroup group;

static const SupercapConfig cfg = {
    .name = "supercap", .can_bus = CAN_BUS_2, .cmd_id = 0x061u, .feedback_id = 0x062u
};

void setUp(void)
{
    fake_can_reset();
    fake_time_set_us(1000000u);
    group = (MotorGroup){ 0 };
}

void tearDown(void)
{
}

static Supercap *add(void)
{
    Supercap *cap = &pool[used++];
    TEST_ASSERT_EQUAL(SUPERCAP_INIT_OK, supercap_init(cap, &cfg, &group));
    return cap;
}

/* 相当于 comm_rx_task：中断里记下接收时刻，交给电容 */
static bool deliver(Supercap *cap, CanBusId bus, uint32_t id, const uint8_t *data, uint8_t len)
{
    CanFrame frame = { .id = id, .len = len, .stamp_us = rm_time_now_us() };
    memcpy(frame.data, data, len);
    return supercap_receive(cap, bus, &frame);
}

/* 一帧合法反馈：无错误、正在输出、23.50 V、−12.3 W、能量 51/255、版本 1 */
static const uint8_t good_feedback[8] = { 0x00u, 0x01u, 0x2Eu, 0x09u, 0x85u, 0xFFu, 51u, 1u };

static void test_bad_config_rejected(void)
{
    Supercap cap;
    SupercapConfig c = cfg;
    c.cmd_id = 0x800u; /* 超过 11 位 */
    TEST_ASSERT_EQUAL(SUPERCAP_INIT_BAD_CONFIG, supercap_init(&cap, &c, &group));
    c = cfg;
    c.feedback_id = c.cmd_id; /* 指令和反馈同一个 ID */
    TEST_ASSERT_EQUAL(SUPERCAP_INIT_BAD_CONFIG, supercap_init(&cap, &c, &group));
}

static void test_fd_bus_rejected(void)
{
    fake_can_set_bus_fd(CAN_BUS_2, true);
    Supercap cap;
    TEST_ASSERT_EQUAL(SUPERCAP_INIT_FD_BUS, supercap_init(&cap, &cfg, &group));
    fake_can_set_bus_fd(CAN_BUS_2, false);
}

static void test_motor_id_conflict_rejected(void)
{
    static const MotorConfig m3508 = { .name = "m",
                                       .type = MOTOR_M3508,
                                       .can_bus = CAN_BUS_2,
                                       .id = 2u,
                                       .direction = 1,
                                       .gear_ratio = DJI_M3508_GEAR_RATIO,
                                       .stop_action = STOP_ACTION_ZERO_TORQUE };
    static Motor motor;
    const Motor *conflict;
    TEST_ASSERT_TRUE(motor_init(&motor, &m3508, &group, &conflict));

    Supercap cap;
    SupercapConfig c = cfg;
    c.feedback_id = 0x202u; /* 电调 2 的反馈 */
    TEST_ASSERT_EQUAL(SUPERCAP_INIT_MOTOR_CONFLICT, supercap_init(&cap, &c, &group));
    c = cfg;
    c.cmd_id = 0x200u; /* 电调 1–4 的控制帧 */
    TEST_ASSERT_EQUAL(SUPERCAP_INIT_MOTOR_CONFLICT, supercap_init(&cap, &c, &group));
}

static void test_encode_cmd(void)
{
    uint8_t out[8];
    const SupercapCmd cmd = { .enable_output = true,
                              .power_limit_w = 60.0f,
                              .buffer_energy_j = 57.3f };
    supercap_encode_cmd(&cmd, out);
    /* 600 = 0x0258，573 = 0x023D */
    const uint8_t expect[8] = { 0x01u, 0x58u, 0x02u, 0x3Du, 0x02u, 1u, 0u, 0u };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 8);
}

static void test_encode_cmd_clamps(void)
{
    uint8_t out[8];
    const SupercapCmd cmd = { .enable_output = false,
                              .power_limit_w = -5.0f,
                              .buffer_energy_j = 1.0e6f };
    supercap_encode_cmd(&cmd, out);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[1]); /* 负数按 0 */
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, out[3]); /* 超过 65535 截到 65535，不回绕 */
    TEST_ASSERT_EQUAL_HEX8(0xFFu, out[4]);
}

static void test_decode_feedback(void)
{
    SupercapFeedback fb;
    TEST_ASSERT_TRUE(supercap_decode_feedback(good_feedback, &fb));
    TEST_ASSERT_EQUAL_UINT8(0u, fb.error_flags);
    TEST_ASSERT_TRUE(fb.output_enabled);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 23.50f, fb.cap_voltage_v);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -12.3f, fb.chassis_power_w); /* 0xFF85 = −123 */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.2f, fb.energy_ratio);
}

static void test_decode_rejects_other_version(void)
{
    uint8_t data[8];
    memcpy(data, good_feedback, 8);
    data[7] = 2u;
    SupercapFeedback fb = { .cap_voltage_v = 7.0f };
    TEST_ASSERT_FALSE(supercap_decode_feedback(data, &fb));
    TEST_ASSERT_EQUAL_FLOAT(7.0f, fb.cap_voltage_v); /* out 不变 */
}

static void test_receive_and_timeout(void)
{
    Supercap *cap = add();
    SupercapFeedback fb;
    TEST_ASSERT_FALSE(supercap_read(cap, &fb)); /* 从未收到 */

    TEST_ASSERT_FALSE(deliver(cap, CAN_BUS_1, 0x062u, good_feedback, 8)); /* 别的总线 */
    TEST_ASSERT_FALSE(deliver(cap, CAN_BUS_2, 0x063u, good_feedback, 8)); /* 别的 ID */
    TEST_ASSERT_TRUE(deliver(cap, CAN_BUS_2, 0x062u, good_feedback, 8));
    TEST_ASSERT_TRUE(supercap_read(cap, &fb));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 23.50f, fb.cap_voltage_v);

    fake_time_advance_ms(SUPERCAP_TIMEOUT_MS); /* 正好等于超时还算在线 */
    TEST_ASSERT_TRUE(supercap_read(cap, &fb));
    fake_time_advance_ms(1u);
    TEST_ASSERT_FALSE(supercap_read(cap, &fb));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 23.50f,
                             fb.cap_voltage_v); /* 离线时仍是最后一帧，只能用来打印 */
}

static void test_bad_frames_do_not_feed(void)
{
    Supercap *cap = add();
    SupercapFeedback fb;
    TEST_ASSERT_TRUE(
        deliver(cap, CAN_BUS_2, 0x062u, good_feedback, 6)); /* 长度不对：是它的，但丢弃 */
    uint8_t other_version[8];
    memcpy(other_version, good_feedback, 8);
    other_version[7] = 9u;
    TEST_ASSERT_TRUE(deliver(cap, CAN_BUS_2, 0x062u, other_version, 8));
    TEST_ASSERT_EQUAL_UINT32(2u, cap->bad_frames);
    TEST_ASSERT_FALSE(supercap_read(cap, &fb));
}

static void test_send_rate_limited(void)
{
    Supercap *cap = add();
    const SupercapCmd cmd = { .enable_output = true, .power_limit_w = 60.0f };

    supercap_send(cap, &cmd); /* 第一次立刻发 */
    TEST_ASSERT_EQUAL_UINT32(1u, fake_can_sent_count());
    TEST_ASSERT_EQUAL(CAN_BUS_2, fake_can_sent_bus(0));
    TEST_ASSERT_EQUAL_HEX32(0x061u, fake_can_sent(0)->id);
    TEST_ASSERT_EQUAL_UINT8(8u, fake_can_sent(0)->len);
    TEST_ASSERT_FALSE(fake_can_sent(0)->is_fd);

    for (unsigned i = 0u; i < SUPERCAP_CMD_PERIOD_MS - 1u; i++) /* 1 kHz 调用，不满 10 ms 不发 */
    {
        fake_time_advance_ms(1u);
        supercap_send(cap, &cmd);
    }
    TEST_ASSERT_EQUAL_UINT32(1u, fake_can_sent_count());
    fake_time_advance_ms(1u);
    supercap_send(cap, &cmd);
    TEST_ASSERT_EQUAL_UINT32(2u, fake_can_sent_count());
}

static void test_send_queue_full_counted(void)
{
    Supercap *cap = add();
    const SupercapCmd cmd = { 0 };
    fake_can_set_send_fail(true);
    supercap_send(cap, &cmd);
    fake_can_set_send_fail(false);
    TEST_ASSERT_EQUAL_UINT32(1u, cap->tx_dropped);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bad_config_rejected);
    RUN_TEST(test_fd_bus_rejected);
    RUN_TEST(test_motor_id_conflict_rejected);
    RUN_TEST(test_encode_cmd);
    RUN_TEST(test_encode_cmd_clamps);
    RUN_TEST(test_decode_feedback);
    RUN_TEST(test_decode_rejects_other_version);
    RUN_TEST(test_receive_and_timeout);
    RUN_TEST(test_bad_frames_do_not_feed);
    RUN_TEST(test_send_rate_limited);
    RUN_TEST(test_send_queue_full_counted);
    return UNITY_END();
}
