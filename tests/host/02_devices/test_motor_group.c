/**
 * @file    test_motor_group.c
 * @brief   motor 与 motor_group 的单元测试：ID 冲突、反馈与离线、打包、零力矩填充、全车停、失能、丢帧计数；
 *          达妙：使能按期望状态对齐、清错只一次、离线清除使能请求、阻尼与失能停机、FD 帧
 */
#include "02_devices/motor/motor_group.h"

#include <string.h>

#include "fake_can.h"
#include "fake_time.h"
#include "unity.h"

/* 每个测试用新的 Motor：看门狗每个实例只能登记一次 */
static Motor pool[64];
static unsigned pool_used;
static MotorGroup group;

/* 相当于comm_rx_task.c：把收到的一帧依次交给组里的每个电机（motor_receive），有电机认领就返回 true */
static bool deliver(CanBusId bus, uint32_t id, const uint8_t *data, uint8_t len)
{
    CanFrame frame = { .id = id, .len = len };
    memcpy(frame.data, data, len);
    for (Motor *m = group.head; m != NULL; m = m->next)
    {
        if (motor_receive(m, bus, &frame))
        {
            return true;
        }
    }
    return false;
}

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
    TEST_ASSERT_TRUE(deliver(m->cfg->can_bus, id, d, 8));
}

static void test_id_conflicts_rejected(void)
{
    /* 电机保存配置指针，所以配置放在静态存储里 */
    static MotorConfig a, same, other_bus, c5, g1, g5, bad;
    a = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    same = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    other_bus = cfg_of(MOTOR_M3508, CAN_BUS_2, 1, SAFE_ACTION_ZERO_TORQUE);
    c5 = cfg_of(MOTOR_M3508, CAN_BUS_1, 5, SAFE_ACTION_ZERO_TORQUE);
    g1 = cfg_of(MOTOR_GM6020, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    g5 = cfg_of(MOTOR_GM6020, CAN_BUS_1, 5, SAFE_ACTION_ZERO_TORQUE);
    bad = cfg_of(MOTOR_M3508, CAN_BUS_1, 9, SAFE_ACTION_ZERO_TORQUE);
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
    TEST_ASSERT_TRUE(deliver(CAN_BUS_1, 0x201, d, 8));
    TEST_ASSERT_TRUE(motor_read_feedback(m, &fb));
    TEST_ASSERT_TRUE(fb.online);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.4533f, fb.speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, fb.torque_nm);
    TEST_ASSERT_EQUAL_UINT64(1000000u, fb.stamp_us);

    fake_time_advance_ms(MOTOR_OFFLINE_TIMEOUT_MS);
    TEST_ASSERT_FALSE(motor_read_feedback(m, &fb));      /* 正好 20 ms 算离线 */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, fb.torque_nm); /* 离线时仍是最后一帧的内容 */
}

/* 同一个反馈 ID 出现在另一路总线上：不是这个电机的帧，不收、不喂狗 */
static void test_frame_on_other_bus_ignored(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    Motor *m = add(&c);
    const uint8_t d[8] = { 0 };
    TEST_ASSERT_FALSE(deliver(CAN_BUS_2, 0x201, d, 8));
    MotorFeedback fb;
    TEST_ASSERT_FALSE(motor_read_feedback(m, &fb));
    TEST_ASSERT_TRUE(deliver(CAN_BUS_1, 0x201, d, 8));
    TEST_ASSERT_TRUE(motor_read_feedback(m, &fb));
}

static void test_short_frame_ignored(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, CAN_BUS_1, 2, SAFE_ACTION_ZERO_TORQUE);
    Motor *m = add(&c);
    const uint8_t d[8] = { 0 };
    TEST_ASSERT_TRUE(deliver(CAN_BUS_1, 0x202, d, 7));
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
    motor_group_send(&group);

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
    motor_group_send(&group);
    const uint8_t zeros[8] = { 0 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, fake_can_sent(0)->data, 8);

    motor_set_torque(m1, 3.0f);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_HEX8(0x20, fake_can_sent(1)->data[0]);
    motor_group_send(&group); /* 这个周期没人写 */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, fake_can_sent(2)->data, 8);
}

static void test_stop_all_overrides_torque(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    Motor *m = add(&c);
    feed(m);
    motor_set_torque(m, 3.0f);
    motor_group_apply_stop_all(&group);
    motor_group_send(&group);
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
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_UINT32(1, fake_can_sent_count());
    motor_group_apply_stop_all(&group);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_UINT32(1, fake_can_sent_count());

    motor_set_torque(m1, 3.0f);
    motor_group_send(&group);
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
        motor_group_send(&group);
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
    motor_group_send(&group);
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
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_UINT32(1, group.tx_dropped);
}

static void test_caps_through_motor(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, CAN_BUS_1, 1, SAFE_ACTION_ZERO_TORQUE);
    const MotorConfig g = cfg_of(MOTOR_GM6020, CAN_BUS_1, 5, SAFE_ACTION_ZERO_TORQUE);
    TEST_ASSERT_TRUE(motor_supports_torque(add(&c)));
    TEST_ASSERT_FALSE(motor_supports_torque(add(&g)));
}

/* ---------------- 达妙 ---------------- */

static const MotorConfig dm_config = {
    .name = "dm",
    .type = MOTOR_DM,
    .can_bus = CAN_BUS_2,
    .id = 1u,
    .direction = 1,
    .gear_ratio = 1.0f,
    .stop_action = SAFE_ACTION_DAMP,
    .dm = { .master_id = 0x11u,
            .p_max = 3.141593f,
            .v_max = 45.0f,
            .t_max = 54.0f,
            .damp_kd = 1.0f },
};

static const uint8_t zero_mit[8] = { 0x7F, 0xFF, 0x7F, 0xF0, 0x00, 0x00, 0x07, 0xFF };

/* 送一帧达妙反馈：状态码在第 0 字节高 4 位 */
static void feed_dm(uint8_t state)
{
    const uint8_t d[8] = { (uint8_t)((state << 4) | 0x01u), 0x80, 0x00, 0x80, 0x08, 0x00, 30, 30 };
    TEST_ASSERT_TRUE(deliver(CAN_BUS_2, 0x11u, d, 8));
}

static const CanFrame *last_sent(void)
{
    return fake_can_sent(fake_can_sent_count() - 1u);
}

static bool last_is_command(uint8_t cmd)
{
    const CanFrame *f = last_sent();
    for (int i = 0; i < 7; i++)
    {
        if (f->data[i] != 0xFFu)
        {
            return false;
        }
    }
    return f->data[7] == cmd;
}

/* 每个周期都发一帧（驱动器只在收到帧时回反馈）；未请求使能时是零力矩 MIT；FD 总线上发 FD 帧 */
static void test_dm_sends_zero_mit_every_cycle(void)
{
    fake_can_set_bus_fd(CAN_BUS_2, true);
    add(&dm_config);
    motor_group_send(&group);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_UINT32(2, fake_can_sent_count());
    TEST_ASSERT_EQUAL_INT(CAN_BUS_2, fake_can_sent_bus(0));
    TEST_ASSERT_EQUAL_HEX32(0x01, last_sent()->id); /* 发往电机 CAN ID，不是反馈 ID */
    TEST_ASSERT_TRUE(last_sent()->is_fd);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero_mit, last_sent()->data, 8);
}

static void test_dm_classic_bus_sends_classic_frames(void)
{
    add(&dm_config);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_UINT32(1, fake_can_sent_count());
    TEST_ASSERT_FALSE(last_sent()->is_fd);
}

/* 请求使能：发使能命令；20 ms 内不重发（中间发 MIT）；没确认就重发；确认后发力矩 */
static void test_dm_enable_sequence(void)
{
    Motor *m = add(&dm_config);
    feed_dm(0x0);
    motor_request_enable(m);
    motor_group_send(&group);
    TEST_ASSERT_TRUE(last_is_command(0xFC));

    fake_time_advance_ms(5u);
    feed_dm(0x0);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero_mit, last_sent()->data, 8);

    fake_time_advance_ms(15u);
    feed_dm(0x0);
    motor_group_send(&group);
    TEST_ASSERT_TRUE(last_is_command(0xFC)); /* 20 ms 没确认：重发 */

    feed_dm(0x1);
    motor_set_torque(m, 10.0f);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_HEX8(0x7A, last_sent()->data[7]); /* 力矩 10 N·m 编码为 0x97A */
}

/* 报错时请求使能：先清错一次；仍报错就不再清，也不使能，等下一次请求 */
static void test_dm_clear_error_once(void)
{
    Motor *m = add(&dm_config);
    feed_dm(0x8);
    motor_request_enable(m);
    motor_group_send(&group);
    TEST_ASSERT_TRUE(last_is_command(0xFB));

    fake_time_advance_ms(25u);
    feed_dm(0x8);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero_mit, last_sent()->data, 8);

    feed_dm(0x0); /* 错误清除 */
    motor_group_send(&group);
    TEST_ASSERT_TRUE(last_is_command(0xFC));
}

/* 离线清除使能请求：重新上线后不自动使能 */
static void test_dm_offline_cancels_enable(void)
{
    Motor *m = add(&dm_config);
    feed_dm(0x0);
    motor_request_enable(m);
    fake_time_advance_ms(MOTOR_OFFLINE_TIMEOUT_MS + 1u);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero_mit, last_sent()->data, 8);

    feed_dm(0x0);
    for (int i = 0; i < 30; i++)
    {
        fake_time_advance_ms(1u);
        motor_group_send(&group);
        TEST_ASSERT_FALSE(last_is_command(0xFC));
    }
}

/* 全车停：阻尼停机发只含 Kd 的 MIT 帧（Kd 1.0 = 0x333），覆盖力矩指令 */
static void test_dm_stop_all_damp(void)
{
    Motor *m = add(&dm_config);
    feed_dm(0x1);
    motor_request_enable(m);
    motor_set_torque(m, 10.0f);
    motor_group_apply_stop_all(&group);
    motor_group_send(&group);
    const CanFrame *f = last_sent();
    TEST_ASSERT_EQUAL_HEX8(0x00, f->data[4]); /* Kp 0 */
    TEST_ASSERT_EQUAL_HEX8(0x33, f->data[5]);
    TEST_ASSERT_EQUAL_HEX8(0x37, f->data[6]); /* Kd 低 4 位 3、力矩 0 的高 4 位 7 */
    TEST_ASSERT_EQUAL_HEX8(0xFF, f->data[7]);
}

/* 失能停机：已使能的电机发失能命令，并放弃使能请求 */
static void test_dm_stop_all_disable(void)
{
    MotorConfig c = dm_config;
    c.stop_action = SAFE_ACTION_DISABLE;
    static MotorConfig cfg;
    cfg = c;
    Motor *m = add(&cfg);
    feed_dm(0x1);
    motor_request_enable(m);
    motor_group_apply_stop_all(&group);
    motor_group_send(&group);
    TEST_ASSERT_TRUE(last_is_command(0xFD));
    TEST_ASSERT_FALSE(m->brand.dm.want_enabled);
}

/* 没人请求使能、电机却报告已使能（例如主控复位而电机保持使能）：发失能命令 */
static void test_dm_unrequested_enable_is_disabled(void)
{
    add(&dm_config);
    feed_dm(0x1);
    motor_group_send(&group);
    TEST_ASSERT_TRUE(last_is_command(0xFD));
}

/* 跨品牌冲突：达妙 Master ID 与 DJI 反馈 ID 相同 */
static void test_dm_conflicts(void)
{
    static MotorConfig dji;
    dji = cfg_of(MOTOR_M3508, CAN_BUS_2, 1, SAFE_ACTION_ZERO_TORQUE);
    static MotorConfig dm_clash;
    dm_clash = dm_config;
    dm_clash.id = 2u;
    dm_clash.dm.master_id = 0x201u;
    static MotorConfig dm_same_master;
    dm_same_master = dm_config;
    dm_same_master.id = 2u;
    const Motor *conflict;

    Motor *mj = add(&dji);
    add(&dm_config); /* 0x01 / 0x11 与 0x201 / 0x200 不冲突 */
    TEST_ASSERT_FALSE(motor_init(&pool[pool_used++], &dm_clash, &group, &conflict));
    TEST_ASSERT_EQUAL_PTR(mj, conflict);
    TEST_ASSERT_FALSE(motor_init(&pool[pool_used++], &dm_same_master, &group, &conflict));
    TEST_ASSERT_NOT_NULL(conflict);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_id_conflicts_rejected);
    RUN_TEST(test_feedback_online_and_timeout);
    RUN_TEST(test_frame_on_other_bus_ignored);
    RUN_TEST(test_short_frame_ignored);
    RUN_TEST(test_flush_packs_frame);
    RUN_TEST(test_unset_offline_and_cleared_slots_are_zero);
    RUN_TEST(test_stop_all_overrides_torque);
    RUN_TEST(test_disable_sends_zero_once);
    RUN_TEST(test_mixed_disable_keeps_sending);
    RUN_TEST(test_frames_per_bus_and_frame_id);
    RUN_TEST(test_send_failure_counted);
    RUN_TEST(test_caps_through_motor);
    RUN_TEST(test_dm_sends_zero_mit_every_cycle);
    RUN_TEST(test_dm_classic_bus_sends_classic_frames);
    RUN_TEST(test_dm_enable_sequence);
    RUN_TEST(test_dm_clear_error_once);
    RUN_TEST(test_dm_offline_cancels_enable);
    RUN_TEST(test_dm_stop_all_damp);
    RUN_TEST(test_dm_stop_all_disable);
    RUN_TEST(test_dm_unrequested_enable_is_disabled);
    RUN_TEST(test_dm_conflicts);
    return UNITY_END();
}
