/**
 * @file    test_motor_codec.c
 * @brief   motor 的编解码单元测试（只走公开接口：送反馈帧 → 读反馈；写力矩 → 看发出的 CAN 帧）：
 *          DJI：配置检查、反馈 ID 与控制帧槽位、反馈换算、多圈计数、力矩 → 电流原始值；
 *          达妙：配置检查、MIT 编码（含截断与方向）、反馈解析与状态码
 * @note    达妙期望字节按附录 A.3 的位布局手算，范围取旧工程 DM8009 的 ±π / 45 / 54
 */
#include "02_devices/motor/motor.h"

#include <string.h>

#include "03_algorithm/math/rm_math.h"
#include "05_platform/time/time.h"
#include "fake_can.h"
#include "fake_time.h"
#include "unity.h"

/* 每个测试用新的 Motor 和配置：看门狗每个实例只能登记一次，电机保存配置指针 */
static Motor pool[64];
static MotorConfig cfgs[64];
static unsigned used;
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

static MotorConfig dji_cfg(MotorType type, uint8_t id, int8_t dir, float gear)
{
    return (MotorConfig){ .name = "t",
                          .type = type,
                          .can_bus = CAN_BUS_1,
                          .id = id,
                          .direction = dir,
                          .gear_ratio = gear,
                          .stop_action = STOP_ACTION_ZERO_TORQUE };
}

static MotorConfig dm_cfg(int8_t dir)
{
    return (MotorConfig){ .name = "dm",
                          .type = MOTOR_DM,
                          .can_bus = CAN_BUS_2,
                          .id = 1u,
                          .direction = dir,
                          .gear_ratio = 1.0f,
                          .stop_action = STOP_ACTION_DAMP,
                          .dm = { .master_id = 0x11u,
                                  .p_max = 3.141593f,
                                  .v_max = 45.0f,
                                  .t_max = 54.0f,
                                  .damp_kd = 1.0f } };
}

/* 初始化一个电机；返回 NULL 表示 motor_init 拒绝了这个配置 */
static Motor *try_add(MotorConfig c)
{
    cfgs[used] = c;
    Motor *m = &pool[used];
    const MotorConfig *cfg = &cfgs[used];
    used++;
    const Motor *conflict;
    return motor_init(m, cfg, &group, &conflict) ? m : NULL;
}

static Motor *add(MotorConfig c)
{
    Motor *m = try_add(c);
    TEST_ASSERT_NOT_NULL(m);
    return m;
}

/* 把一帧反馈送进电机组，返回是否有电机认领 */
static bool deliver(CanBusId bus, uint32_t id, const uint8_t d[8])
{
    CanFrame frame = { .id = id, .len = 8u, .stamp_us = rm_time_now_us() };
    memcpy(frame.data, d, 8u);
    return motor_group_receive(&group, bus, &frame);
}

/* DJI 反馈帧：编码器、rpm、电流原始值大端，温度在第 6 字节；送进去后读出反馈 */
static MotorFeedback dji_rx(const Motor *m, uint16_t enc, int16_t rpm, int16_t current,
                            uint8_t temp)
{
    const uint8_t d[8] = { (uint8_t)(enc >> 8),
                           (uint8_t)enc,
                           (uint8_t)((uint16_t)rpm >> 8),
                           (uint8_t)rpm,
                           (uint8_t)((uint16_t)current >> 8),
                           (uint8_t)current,
                           temp,
                           0u };
    const uint32_t id = (m->cfg->type == MOTOR_GM6020 ? 0x204u : 0x200u) + m->cfg->id;
    TEST_ASSERT_TRUE(deliver(m->cfg->can_bus, id, d));
    MotorFeedback fb;
    TEST_ASSERT_TRUE(motor_read_feedback(m, &fb));
    return fb;
}

/* 写一个力矩、发送一次，返回这个电机槽位上的电流原始值（电机须在线） */
static int16_t dji_tx_raw(Motor *m, float torque_nm)
{
    fake_can_reset();
    motor_set_torque(m, torque_nm);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_UINT32(1, fake_can_sent_count());
    const uint8_t slot = (uint8_t)((m->cfg->id - 1u) % 4u);
    const uint8_t *d = fake_can_sent(0)->data;
    return (int16_t)(((uint16_t)d[2u * slot] << 8) | d[2u * slot + 1u]);
}

/* ---------------- DJI ---------------- */

static void test_dji_config_valid(void)
{
    TEST_ASSERT_NOT_NULL(try_add(dji_cfg(MOTOR_M3508, 8, 1, DJI_M3508_GEAR_RATIO)));
    TEST_ASSERT_NULL(try_add(dji_cfg(MOTOR_M3508, 0, 1, DJI_M3508_GEAR_RATIO)));
    TEST_ASSERT_NULL(try_add(dji_cfg(MOTOR_M3508, 9, 1, DJI_M3508_GEAR_RATIO)));
    TEST_ASSERT_NOT_NULL(try_add(dji_cfg(MOTOR_GM6020, 7, -1, 1.0f)));
    group = (MotorGroup){ 0 };
    TEST_ASSERT_NULL(try_add(dji_cfg(MOTOR_GM6020, 8, 1, 1.0f)));
    TEST_ASSERT_NULL(try_add(dji_cfg(MOTOR_M2006, 1, 0, DJI_M2006_GEAR_RATIO))); /* 方向必须是 ±1 */
    TEST_ASSERT_NULL(try_add(dji_cfg(MOTOR_M2006, 1, 1, 0.0f)));
    MotorConfig c = dji_cfg(MOTOR_M2006, 1, 1, DJI_M2006_GEAR_RATIO);
    c.stop_action = STOP_ACTION_DAMP; /* DJI 不支持阻尼（ADR 0031） */
    TEST_ASSERT_NULL(try_add(c));
    c.stop_action = STOP_ACTION_DISABLE;
    TEST_ASSERT_NOT_NULL(try_add(c));
}

/* 附录 A.2：反馈 ID C6x0 为 0x200 + ID、GM6020 为 0x204 + ID；控制帧 C6x0 1–4 号 0x200、5–8 号 0x1FF */
static void test_dji_ids_and_slots(void)
{
    const uint8_t d[8] = { 0 };
    add(dji_cfg(MOTOR_M3508, 1, 1, 1.0f));
    add(dji_cfg(MOTOR_M3508, 8, 1, 1.0f));
    add(dji_cfg(MOTOR_GM6020, 7, 1, 1.0f));
    TEST_ASSERT_TRUE(deliver(CAN_BUS_1, 0x201, d));
    TEST_ASSERT_TRUE(deliver(CAN_BUS_1, 0x208, d));
    TEST_ASSERT_TRUE(deliver(CAN_BUS_1, 0x20B, d));
    TEST_ASSERT_FALSE(deliver(CAN_BUS_1, 0x205, d));

    group = (MotorGroup){ 0 };
    Motor *m5 = add(dji_cfg(MOTOR_M2006, 5, 1, DJI_M2006_GEAR_RATIO));
    (void)dji_rx(m5, 0, 0, 0, 0);
    motor_set_torque(m5, 0.9f);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_UINT32(1, fake_can_sent_count());
    TEST_ASSERT_EQUAL_HEX32(0x1FF, fake_can_sent(0)->id);
    TEST_ASSERT_NOT_EQUAL(0, fake_can_sent(0)->data[0] | fake_can_sent(0)->data[1]); /* 槽位 0 */
}

static void test_dji_supports_torque(void)
{
    TEST_ASSERT_TRUE(motor_supports_torque(add(dji_cfg(MOTOR_M3508, 1, 1, 1.0f))));
    TEST_ASSERT_TRUE(motor_supports_torque(add(dji_cfg(MOTOR_M2006, 2, 1, 1.0f))));
    TEST_ASSERT_FALSE(motor_supports_torque(add(dji_cfg(MOTOR_GM6020, 5, 1, 1.0f))));
}

/* M3508 原装减速箱：20 A = 6 N·m；1000 rpm 转子 = 5.4533 rad/s 输出轴 */
static void test_m3508_feedback_units(void)
{
    const Motor *m = add(dji_cfg(MOTOR_M3508, 1, 1, DJI_M3508_GEAR_RATIO));
    const MotorFeedback fb = dji_rx(m, 1000, 1000, 16384, 40);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, fb.angle_rad); /* 第一帧为零点 */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.4533f, fb.speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 6.0f, fb.torque_nm);
    TEST_ASSERT_TRUE(fb.torque_is_estimate);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, fb.temperature_c);
    TEST_ASSERT_EQUAL_UINT16(1000, fb.raw_encoder);
}

static void test_dji_direction_flips_signs(void)
{
    const Motor *m = add(dji_cfg(MOTOR_M3508, 1, -1, DJI_M3508_GEAR_RATIO));
    MotorFeedback fb = dji_rx(m, 0, 1000, 8192, 0);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -5.4533f, fb.speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -3.0f, fb.torque_nm);
    fb = dji_rx(m, 2048, 0, 0, 0); /* 转子正转 1/4 圈 */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -(RM_PI / 2.0f) / DJI_M3508_GEAR_RATIO, fb.angle_rad);
}

/* 过零：8100 → 200 算正转过一圈，200 → 8000 算反转回来 */
static void test_dji_multi_turn(void)
{
    const Motor *m = add(dji_cfg(MOTOR_M3508, 1, 1, 1.0f));
    const uint16_t seq[] = { 1000, 5000, 8100, 200 };
    MotorFeedback fb = { 0 };
    for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
    {
        fb = dji_rx(m, seq[i], 0, 0, 0);
    }
    TEST_ASSERT_EQUAL_INT32(1, m->brand.dji.turns);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f * RM_PI * (1.0f - 800.0f / 8192.0f), fb.angle_rad);

    fb = dji_rx(m, 8000, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT32(0, m->brand.dji.turns);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f * RM_PI * 7000.0f / 8192.0f, fb.angle_rad);
}

/* GM6020 直驱：角度直接是编码器位置；单圈角在 [-π, π) */
static void test_gm6020_absolute_angle(void)
{
    const Motor *m = add(dji_cfg(MOTOR_GM6020, 1, 1, 1.0f));
    MotorFeedback fb = dji_rx(m, 2048, 0, 0, 30);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, RM_PI / 2.0f, fb.angle_rad);
    fb = dji_rx(m, 6144, 0, 0, 30);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -RM_PI / 2.0f, fb.single_angle_rad);
    fb = dji_rx(m, 4096, 0, 0, 30);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -RM_PI, fb.single_angle_rad);
}

static void test_m2006_has_no_temperature(void)
{
    const Motor *m = add(dji_cfg(MOTOR_M2006, 1, 1, DJI_M2006_GEAR_RATIO));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dji_rx(m, 0, 0, 0, 55).temperature_c);
}

/* 3 N·m = 10 A = 8192；超量程截到 ±16384；方向取反；M2006 0.9 N·m = 5 A = 5000 */
static void test_dji_torque_to_raw(void)
{
    Motor *m = add(dji_cfg(MOTOR_M3508, 1, 1, DJI_M3508_GEAR_RATIO));
    (void)dji_rx(m, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT16(8192, dji_tx_raw(m, 3.0f));
    TEST_ASSERT_EQUAL_INT16(-4096, dji_tx_raw(m, -1.5f));
    TEST_ASSERT_EQUAL_INT16(16384, dji_tx_raw(m, 100.0f));
    TEST_ASSERT_EQUAL_INT16(-16384, dji_tx_raw(m, -100.0f));

    Motor *r = add(dji_cfg(MOTOR_M3508, 2, -1, DJI_M3508_GEAR_RATIO));
    (void)dji_rx(r, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT16(-8192, dji_tx_raw(r, 3.0f));

    group = (MotorGroup){ 0 };
    Motor *s = add(dji_cfg(MOTOR_M2006, 1, 1, DJI_M2006_GEAR_RATIO));
    (void)dji_rx(s, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT16(5000, dji_tx_raw(s, 0.9f));
    TEST_ASSERT_EQUAL_INT16(10000, dji_tx_raw(s, 50.0f));
}

/* 反馈 → 指令往返：同一电流原始值换成力矩再换回来不变 */
static void test_dji_feedback_and_command_scales_agree(void)
{
    Motor *m = add(dji_cfg(MOTOR_M3508, 1, 1, DJI_M3508_GEAR_RATIO));
    const MotorFeedback fb = dji_rx(m, 0, 0, 1234, 0);
    TEST_ASSERT_EQUAL_INT16(1234, dji_tx_raw(m, fb.torque_nm));
}

/* ---------------- 达妙 ---------------- */

static void test_dm_config_valid(void)
{
    TEST_ASSERT_NOT_NULL(try_add(dm_cfg(1)));
    group = (MotorGroup){ 0 };
    MotorConfig c = dm_cfg(1);
    c.id = 0u;
    TEST_ASSERT_NULL(try_add(c));
    c.id = 16u; /* 反馈第 0 字节只有 4 位放 ID */
    TEST_ASSERT_NULL(try_add(c));
    c = dm_cfg(1);
    c.gear_ratio = 10.0f; /* 反馈已是输出轴 */
    TEST_ASSERT_NULL(try_add(c));
    c = dm_cfg(1);
    c.dm.master_id = 1u; /* 与 CAN ID 相同 */
    TEST_ASSERT_NULL(try_add(c));
    c = dm_cfg(1);
    c.dm.damp_kd = 6.0f;
    TEST_ASSERT_NULL(try_add(c));
    c = dm_cfg(1);
    c.dm.t_max = 0.0f;
    TEST_ASSERT_NULL(try_add(c));
    TEST_ASSERT_NULL(try_add(dm_cfg(0)));
}

/* 达妙每个周期都发一帧 MIT；返回发出的 8 字节 */
static const uint8_t *dm_tx(void)
{
    fake_can_reset();
    motor_group_send(&group);
    TEST_ASSERT_EQUAL_UINT32(1, fake_can_sent_count());
    return fake_can_sent(0)->data;
}

/* 送一帧达妙反馈让它在线（状态 0：失能，不会触发失能命令） */
static void dm_online(void)
{
    const uint8_t d[8] = { 0x01, 0x80, 0x00, 0x80, 0x08, 0x00, 30, 30 };
    TEST_ASSERT_TRUE(deliver(CAN_BUS_2, 0x11u, d));
}

/* 零力矩：位置 32767、速度 2047、Kp 0、Kd 0、力矩 2047 */
static void test_dm_encode_zero(void)
{
    add(dm_cfg(1));
    const uint8_t expect[8] = { 0x7F, 0xFF, 0x7F, 0xF0, 0x00, 0x00, 0x07, 0xFF };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, dm_tx(), 8);
}

/* 力矩 10 N·m：(10 + 54) × 4095 / 108 = 2426.6 → 2426 = 0x97A */
static void test_dm_encode_torque(void)
{
    Motor *m = add(dm_cfg(1));
    dm_online();
    motor_set_torque(m, 10.0f);
    const uint8_t *d = dm_tx();
    TEST_ASSERT_EQUAL_HEX8(0x09, d[6] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x7A, d[7]);
}

/* 阻尼停机：只给 Kd 1.0：1 / 5 × 4095 = 819 = 0x333，力矩 0 → 2047 = 0x7FF */
static void test_dm_encode_damp_kd(void)
{
    Motor *m = add(dm_cfg(1));
    motor_apply_stop_action(m, STOP_ACTION_DAMP);
    const uint8_t *d = dm_tx();
    TEST_ASSERT_EQUAL_HEX8(0x33, d[5]);
    TEST_ASSERT_EQUAL_HEX8(0x37, d[6]); /* Kd 低 4 位 3、力矩高 4 位 7 */
    TEST_ASSERT_EQUAL_HEX8(0xFF, d[7]);
}

/* 超出范围截到端点：旧代码不截断，-100 N·m 会绕回成很大的数 */
static void test_dm_encode_clamps(void)
{
    Motor *m = add(dm_cfg(1));
    dm_online();
    motor_set_torque(m, -100.0f);
    const uint8_t *d = dm_tx();
    TEST_ASSERT_EQUAL_HEX8(0x00, d[6] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x00, d[7]);
    motor_set_torque(m, 100.0f);
    d = dm_tx();
    TEST_ASSERT_EQUAL_HEX8(0x0F, d[6] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0xFF, d[7]);
}

/* 方向为 -1：+10 N·m 按 -10 编码：(−10 + 54) × 4095 / 108 = 1668.3 → 1668 = 0x684 */
static void test_dm_encode_direction(void)
{
    Motor *m = add(dm_cfg(-1));
    dm_online();
    motor_set_torque(m, 10.0f);
    const uint8_t *d = dm_tx();
    TEST_ASSERT_EQUAL_HEX8(0x06, d[6] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x84, d[7]);
}

static MotorFeedback dm_rx(const Motor *m, const uint8_t d[8])
{
    TEST_ASSERT_TRUE(deliver(CAN_BUS_2, 0x11u, d));
    MotorFeedback fb;
    TEST_ASSERT_TRUE(motor_read_feedback(m, &fb));
    return fb;
}

static void test_dm_decode_feedback(void)
{
    const Motor *m = add(dm_cfg(1));
    /* 状态 1（使能）、ID 1；位置满量程、速度 0x800、力矩 0x000；MOS 30 °C、线圈 40 °C */
    const uint8_t d[8] = { 0x11, 0xFF, 0xFF, 0x80, 0x00, 0x00, 30, 40 };
    const MotorFeedback fb = dm_rx(m, d);
    TEST_ASSERT_TRUE(fb.enabled);
    TEST_ASSERT_EQUAL_UINT8(0, fb.error_code);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.141593f, fb.angle_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, fb.speed_rad_s); /* 0x800 比中点多半个量化单位 */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -54.0f, fb.torque_nm);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, fb.temperature_c);
}

static void test_dm_decode_state_codes(void)
{
    const Motor *m = add(dm_cfg(1));
    uint8_t d[8] = { 0x01, 0x80, 0x00, 0x80, 0x08, 0x00, 0, 0 };
    MotorFeedback fb = dm_rx(m, d);
    TEST_ASSERT_FALSE(fb.enabled); /* 状态 0：失能 */
    TEST_ASSERT_EQUAL_UINT8(0, fb.error_code);
    d[0] = 0xD1; /* 0xD 通信丢失 */
    fb = dm_rx(m, d);
    TEST_ASSERT_FALSE(fb.enabled);
    TEST_ASSERT_EQUAL_HEX8(0x0D, fb.error_code);
}

static void test_dm_decode_direction(void)
{
    const Motor *m = add(dm_cfg(-1));
    const uint8_t d[8] = { 0x11, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0 };
    const MotorFeedback fb = dm_rx(m, d);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -3.141593f, fb.angle_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -45.0f, fb.speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -54.0f, fb.torque_nm);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dji_config_valid);
    RUN_TEST(test_dji_ids_and_slots);
    RUN_TEST(test_dji_supports_torque);
    RUN_TEST(test_m3508_feedback_units);
    RUN_TEST(test_dji_direction_flips_signs);
    RUN_TEST(test_dji_multi_turn);
    RUN_TEST(test_gm6020_absolute_angle);
    RUN_TEST(test_m2006_has_no_temperature);
    RUN_TEST(test_dji_torque_to_raw);
    RUN_TEST(test_dji_feedback_and_command_scales_agree);
    RUN_TEST(test_dm_config_valid);
    RUN_TEST(test_dm_encode_zero);
    RUN_TEST(test_dm_encode_torque);
    RUN_TEST(test_dm_encode_damp_kd);
    RUN_TEST(test_dm_encode_clamps);
    RUN_TEST(test_dm_encode_direction);
    RUN_TEST(test_dm_decode_feedback);
    RUN_TEST(test_dm_decode_state_codes);
    RUN_TEST(test_dm_decode_direction);
    return UNITY_END();
}
