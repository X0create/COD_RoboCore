/**
 * @file    test_motor_group.c
 * @brief   motor 与 motor_group 的单元测试：ID 冲突、反馈与离线、打包、零力矩填充、全车停、失能、丢帧计数
 */
#include "devices/motor/motor_group.h"

#include <string.h>

#include "fake_can.h"
#include "fake_time.h"
#include "unity.h"

/* 每个测试用新的 Motor：看门狗每个实例只能登记一次 */
static Motor pool[64];
static unsigned pool_used;
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

static MotorConfig cfg_of(MotorType type, CanBusId bus, uint8_t id, SafeAction stop)
{
    const float gear = (type == MOTOR_GM6020) ? 1.0f : DJI_M3508_GEAR_RATIO;
    return (MotorConfig){ .name = "m",
                          .type = type,
                          .can_bus = bus,
                          .id = id,
                          .direction = 1,
                          .gear_ratio = gear,
                          .stop_action = stop };
}

static Motor *add(const MotorConfig *cfg)
{
    Motor *m = &pool[pool_used++];
    const Motor *conflict;
    TEST_ASSERT_TRUE(motor_init(m, cfg, &group, &conflict));
    return m;
}

/* 送一帧反馈让电机在线 */
static void feed(const Motor *m)
{
    const uint8_t d[8] = { 0 };
    const uint32_t id = (m->cfg->type == MOTOR_GM6020 ? 0x204u : 0x200u) + m->cfg->id;
    TEST_ASSERT_TRUE(fake_can_deliver(m->cfg->can_bus, id, d, 8));
}

static void test_id_conflicts_rejected(void)
{
    static const MotorConfig a = {
        "a", MOTOR_M3508, CAN_BUS_1, 1, 1, 1.0f, SAFE_ACTION_ZERO_TORQUE
    };
    static const MotorConfig same = {
        "same", MOTOR_M3508, CAN_BUS_1, 1, 1, 1.0f, SAFE_ACTION_ZERO_TORQUE
    };
    static const MotorConfig other_bus = {
        "ob", MOTOR_M3508, CAN_BUS_2, 1, 1, 1.0f, SAFE_ACTION_ZERO_TORQUE
    };
    static const MotorConfig c5 = {
        "c5", MOTOR_M3508, CAN_BUS_1, 5, 1, 1.0f, SAFE_ACTION_ZERO_TORQUE
    };
    static const MotorConfig g1 = { "g1", MOTOR_GM6020,           CAN_BUS_1, 1, 1,
                                    1.0f, SAFE_ACTION_ZERO_TORQUE };
    static const MotorConfig g5 = { "g5", MOTOR_GM6020,           CAN_BUS_1, 5, 1,
                                    1.0f, SAFE_ACTION_ZERO_TORQUE };
    static const MotorConfig bad = {
        "bad", MOTOR_M3508, CAN_BUS_1, 9, 1, 1.0f, SAFE_ACTION_ZERO_TORQUE
    };
    const Motor *conflict;

    Motor *ma = add(&a);
    TEST_ASSERT_FALSE(motor_init(&pool[pool_used++], &same, &group, &conflict));
    TEST_ASSERT_EQUAL_PTR(ma, conflict);
    add(&other_bus); /* 另一路总线不冲突 */
    Motor *m5 = add(&c5);
    /* GM6020 1 号：反馈 0x205、控制 0x1FF 槽 0，都与 C620 5 号相同 */
    TEST_ASSERT_FALSE(motor_init(&pool[pool_used++], &g1, &group, &conflict));
    TEST_ASSERT_EQUAL_PTR(m5, conflict);
    add(&g5); /* GM6020 5 号：0x209 / 0x2FF，不冲突 */
    TEST_ASSERT_FALSE(motor_init(&pool[pool_used++], &bad, &group, &conflict));
    TEST_ASSERT_NULL(conflict);
}

static void test_feedback_online_and_timeout(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    Motor *m = add(&c);
    MotorFeedback fb;
    TEST_ASSERT_FALSE(motor_read_feedback(m, &fb)); /* 还没收到帧 */

    const uint8_t d[8] = {
        0x03, 0xE8, 0x03, 0xE8, 0x20, 0x00, 25, 0
    }; /* 编码器 1000、1000 rpm、8192、25 °C */
    TEST_ASSERT_TRUE(fake_can_deliver(CAN_BUS_1, 0x201, d, 8));
    TEST_ASSERT_TRUE(motor_read_feedback(m, &fb));
    TEST_ASSERT_TRUE(fb.online);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.4533f, fb.speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, fb.torque_nm);
    TEST_ASSERT_EQUAL_UINT64(1000000u, fb.stamp_us);

    fake_time_advance_ms(MOTOR_OFFLINE_TIMEOUT_MS);
    TEST_ASSERT_FALSE(motor_read_feedback(m, &fb));      /* 正好 20 ms 算离线 */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, fb.torque_nm); /* 离线时仍是最后一帧的内容 */
}

static void test_short_frame_ignored(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, CAN_BUS_1, 2, SAFE_ACTION_ZERO_TORQUE);
    Motor *m = add(&c);
    const uint8_t d[8] = { 0 };
    TEST_ASSERT_TRUE(fake_can_deliver(CAN_BUS_1, 0x202, d, 7));
    MotorFeedback fb;
    TEST_ASSERT_FALSE(motor_read_feedback(m, &fb));
}

/* 1、2 号在同一帧 0x200：3 N·m → 0x2000，-1.5 N·m → 0xF000，未用的槽位为 0 */
static void test_flush_packs_frame(void)
{
    const MotorConfig c1 = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    const MotorConfig c2 = cfg_of(MOTOR_M3508, CAN_BUS_1, 2, SAFE_ACTION_ZERO_TORQUE);
    Motor *m1 = add(&c1);
    Motor *m2 = add(&c2);
    feed(m1);
    feed(m2);
    motor_set_torque(m1, 3.0f);
    motor_set_torque(m2, -1.5f);
    motor_group_flush(&group);

    TEST_ASSERT_EQUAL_UINT32(1, fake_can_sent_count());
    const CanFrame *f = fake_can_sent(0);
    TEST_ASSERT_EQUAL_INT(CAN_BUS_1, fake_can_sent_bus(0));
    TEST_ASSERT_EQUAL_HEX32(0x200, f->id);
    TEST_ASSERT_EQUAL_UINT8(8, f->len);
    TEST_ASSERT_FALSE(f->is_fd);
    const uint8_t expect[8] = { 0x20, 0x00, 0xF0, 0x00, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, f->data, 8);
}

/* 没写指令、或离线，填零力矩；发送后槽位清空，下个周期不重复旧指令 */
static void test_unset_offline_and_cleared_slots_are_zero(void)
{
    const MotorConfig c1 = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    const MotorConfig c2 = cfg_of(MOTOR_M3508, CAN_BUS_1, 2, SAFE_ACTION_ZERO_TORQUE);
    Motor *m1 = add(&c1);
    Motor *m2 = add(&c2);
    feed(m1); /* m2 从未在线 */
    motor_set_torque(m2, 3.0f);
    motor_group_flush(&group);
    const uint8_t zeros[8] = { 0 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, fake_can_sent(0)->data, 8);

    motor_set_torque(m1, 3.0f);
    motor_group_flush(&group);
    TEST_ASSERT_EQUAL_HEX8(0x20, fake_can_sent(1)->data[0]);
    motor_group_flush(&group); /* 这个周期没人写 */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, fake_can_sent(2)->data, 8);
}

static void test_stop_all_overrides_torque(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    Motor *m = add(&c);
    feed(m);
    motor_set_torque(m, 3.0f);
    motor_group_apply_stop_all(&group);
    motor_group_flush(&group);
    const uint8_t zeros[8] = { 0 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, fake_can_sent(0)->data, 8);
}

/* 失能：发一次 0 后不再发；恢复后重新发送 */
static void test_disable_sends_zero_once(void)
{
    const MotorConfig c1 = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_DISABLE);
    const MotorConfig c2 = cfg_of(MOTOR_M3508, CAN_BUS_1, 2, SAFE_ACTION_DISABLE);
    Motor *m1 = add(&c1);
    add(&c2);
    feed(m1);
    motor_group_apply_stop_all(&group);
    motor_group_flush(&group);
    TEST_ASSERT_EQUAL_UINT32(1, fake_can_sent_count());
    motor_group_apply_stop_all(&group);
    motor_group_flush(&group);
    TEST_ASSERT_EQUAL_UINT32(1, fake_can_sent_count());

    motor_set_torque(m1, 3.0f);
    motor_group_flush(&group);
    TEST_ASSERT_EQUAL_UINT32(2, fake_can_sent_count());
    TEST_ASSERT_EQUAL_HEX8(0x20, fake_can_sent(1)->data[0]);
}

/* 同一帧里有一个是零力矩，就继续每周期发送 */
static void test_mixed_disable_keeps_sending(void)
{
    const MotorConfig c1 = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_DISABLE);
    const MotorConfig c2 = cfg_of(MOTOR_M3508, CAN_BUS_1, 2, SAFE_ACTION_ZERO_TORQUE);
    add(&c1);
    add(&c2);
    for (int i = 0; i < 3; i++)
    {
        motor_group_apply_stop_all(&group);
        motor_group_flush(&group);
    }
    TEST_ASSERT_EQUAL_UINT32(3, fake_can_sent_count());
}

/* 每路总线各自一帧；5 号在 0x1FF */
static void test_frames_per_bus_and_frame_id(void)
{
    const MotorConfig a = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    const MotorConfig b = cfg_of(MOTOR_M3508, CAN_BUS_2, 5, SAFE_ACTION_ZERO_TORQUE);
    add(&a);
    Motor *mb = add(&b);
    feed(mb);
    motor_set_torque(mb, -3.0f);
    motor_group_flush(&group);
    TEST_ASSERT_EQUAL_UINT32(2, fake_can_sent_count());
    TEST_ASSERT_EQUAL_INT(CAN_BUS_1, fake_can_sent_bus(0));
    TEST_ASSERT_EQUAL_HEX32(0x200, fake_can_sent(0)->id);
    TEST_ASSERT_EQUAL_INT(CAN_BUS_2, fake_can_sent_bus(1));
    TEST_ASSERT_EQUAL_HEX32(0x1FF, fake_can_sent(1)->id);
    TEST_ASSERT_EQUAL_HEX8(0xE0, fake_can_sent(1)->data[0]); /* -8192 = 0xE000 */
    TEST_ASSERT_EQUAL_HEX8(0x00, fake_can_sent(1)->data[1]);
}

static void test_send_failure_counted(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    add(&c);
    fake_can_set_send_fail(true);
    motor_group_flush(&group);
    TEST_ASSERT_EQUAL_UINT32(1, group.tx_dropped);
}

static void test_caps_through_motor(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    const MotorConfig g = cfg_of(MOTOR_GM6020, CAN_BUS_1, 5, SAFE_ACTION_ZERO_TORQUE);
    TEST_ASSERT_TRUE(motor_supports_torque(add(&c)));
    TEST_ASSERT_FALSE(motor_supports_torque(add(&g)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_id_conflicts_rejected);
    RUN_TEST(test_feedback_online_and_timeout);
    RUN_TEST(test_short_frame_ignored);
    RUN_TEST(test_flush_packs_frame);
    RUN_TEST(test_unset_offline_and_cleared_slots_are_zero);
    RUN_TEST(test_stop_all_overrides_torque);
    RUN_TEST(test_disable_sends_zero_once);
    RUN_TEST(test_mixed_disable_keeps_sending);
    RUN_TEST(test_frames_per_bus_and_frame_id);
    RUN_TEST(test_send_failure_counted);
    RUN_TEST(test_caps_through_motor);
    return UNITY_END();
}
