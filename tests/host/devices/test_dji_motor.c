/**
 * @file    test_dji_motor.c
 * @brief   dji_motor 的单元测试：配置检查、ID 与槽位、反馈换算、多圈计数、力矩 → 电流原始值
 */
#include "devices/motor/dji_motor.h"

#include "unity.h"

#define PI_F 3.14159265f

void setUp(void)
{
}

void tearDown(void)
{
}

static MotorConfig cfg_of(MotorType type, uint8_t id, int8_t dir, float gear)
{
    return (MotorConfig){ .name = "t",
                          .type = type,
                          .can_bus = CAN_BUS_1,
                          .id = id,
                          .direction = dir,
                          .gear_ratio = gear,
                          .stop_action = SAFE_ACTION_ZERO_TORQUE };
}

/* 反馈帧：编码器、rpm、电流原始值大端，温度在第 6 字节 */
static void frame_of(uint8_t d[8], uint16_t enc, int16_t rpm, int16_t current, uint8_t temp)
{
    d[0] = (uint8_t)(enc >> 8);
    d[1] = (uint8_t)enc;
    d[2] = (uint8_t)((uint16_t)rpm >> 8);
    d[3] = (uint8_t)rpm;
    d[4] = (uint8_t)((uint16_t)current >> 8);
    d[5] = (uint8_t)current;
    d[6] = temp;
    d[7] = 0u;
}

static void test_config_valid(void)
{
    MotorConfig c = cfg_of(MOTOR_M3508, 8, 1, DJI_M3508_GEAR_RATIO);
    TEST_ASSERT_TRUE(dji_config_valid(&c));
    c.id = 0;
    TEST_ASSERT_FALSE(dji_config_valid(&c));
    c.id = 9;
    TEST_ASSERT_FALSE(dji_config_valid(&c));
    c = cfg_of(MOTOR_GM6020, 7, -1, 1.0f);
    TEST_ASSERT_TRUE(dji_config_valid(&c));
    c.id = 8;
    TEST_ASSERT_FALSE(dji_config_valid(&c));
    c = cfg_of(MOTOR_M2006, 1, 0, DJI_M2006_GEAR_RATIO);
    TEST_ASSERT_FALSE(dji_config_valid(&c)); /* 方向必须是 ±1 */
    c = cfg_of(MOTOR_M2006, 1, 1, 0.0f);
    TEST_ASSERT_FALSE(dji_config_valid(&c));
    c = cfg_of(MOTOR_M2006, 1, 1, DJI_M2006_GEAR_RATIO);
    c.stop_action = SAFE_ACTION_DAMP; /* DJI 不支持阻尼（ADR 0031） */
    TEST_ASSERT_FALSE(dji_config_valid(&c));
    c.stop_action = SAFE_ACTION_DISABLE;
    TEST_ASSERT_TRUE(dji_config_valid(&c));
}

static void assert_slot(MotorType type, uint8_t id, uint32_t frame_id, uint8_t slot)
{
    const MotorConfig c = cfg_of(type, id, 1, 1.0f);
    uint8_t f, s;
    dji_ctrl_slot(&c, &f, &s);
    TEST_ASSERT_EQUAL_HEX32(frame_id, dji_ctrl_frame_id(f));
    TEST_ASSERT_EQUAL_UINT8(slot, s);
}

/* 附录 A.2 的控制帧与反馈 ID 对照 */
static void test_ids_and_slots(void)
{
    assert_slot(MOTOR_M3508, 1, 0x200, 0);
    assert_slot(MOTOR_M3508, 4, 0x200, 3);
    assert_slot(MOTOR_M2006, 5, 0x1FF, 0);
    assert_slot(MOTOR_M3508, 8, 0x1FF, 3);
    assert_slot(MOTOR_GM6020, 1, 0x1FF, 0);
    assert_slot(MOTOR_GM6020, 5, 0x2FF, 0);
    assert_slot(MOTOR_GM6020, 7, 0x2FF, 2);

    MotorConfig c = cfg_of(MOTOR_M3508, 1, 1, 1.0f);
    TEST_ASSERT_EQUAL_HEX32(0x201, dji_feedback_id(&c));
    c.id = 8;
    TEST_ASSERT_EQUAL_HEX32(0x208, dji_feedback_id(&c));
    c = cfg_of(MOTOR_GM6020, 1, 1, 1.0f);
    TEST_ASSERT_EQUAL_HEX32(0x205, dji_feedback_id(&c));
    c.id = 7;
    TEST_ASSERT_EQUAL_HEX32(0x20B, dji_feedback_id(&c));
}

static void test_caps(void)
{
    TEST_ASSERT_TRUE(dji_caps(MOTOR_M3508).torque_command);
    TEST_ASSERT_TRUE(dji_caps(MOTOR_M2006).torque_command);
    TEST_ASSERT_FALSE(dji_caps(MOTOR_GM6020).torque_command);
    TEST_ASSERT_FALSE(dji_caps(MOTOR_M3508).torque_feedback_exact);
    TEST_ASSERT_FALSE(dji_caps(MOTOR_M3508).needs_enable);
}

/* M3508 原装减速箱：20 A = 6 N·m；1000 rpm 转子 = 5.4533 rad/s 输出轴 */
static void test_m3508_feedback_units(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, 1, 1, DJI_M3508_GEAR_RATIO);
    DjiMotorState st = { 0 };
    MotorFeedback fb;
    uint8_t d[8];
    frame_of(d, 1000, 1000, 16384, 40);
    dji_decode_feedback(&c, &st, d, &fb);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, fb.angle_rad); /* 第一帧为零点 */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.4533f, fb.speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 6.0f, fb.torque_nm);
    TEST_ASSERT_TRUE(fb.torque_is_estimate);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, fb.temperature_c);
    TEST_ASSERT_EQUAL_UINT16(1000, fb.raw_encoder);
}

static void test_direction_flips_signs(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, 1, -1, DJI_M3508_GEAR_RATIO);
    DjiMotorState st = { 0 };
    MotorFeedback fb;
    uint8_t d[8];
    frame_of(d, 0, 1000, 8192, 0);
    dji_decode_feedback(&c, &st, d, &fb);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -5.4533f, fb.speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -3.0f, fb.torque_nm);
    frame_of(d, 2048, 0, 0, 0); /* 转子正转 1/4 圈 */
    dji_decode_feedback(&c, &st, d, &fb);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -(PI_F / 2.0f) / DJI_M3508_GEAR_RATIO, fb.angle_rad);
}

/* 过零：8100 → 200 算正转过一圈，200 → 8000 算反转回来 */
static void test_multi_turn(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, 1, 1, 1.0f);
    DjiMotorState st = { 0 };
    MotorFeedback fb;
    uint8_t d[8];
    const uint16_t seq[] = { 1000, 5000, 8100, 200 };
    for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
    {
        frame_of(d, seq[i], 0, 0, 0);
        dji_decode_feedback(&c, &st, d, &fb);
    }
    TEST_ASSERT_EQUAL_INT32(1, st.turns);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f * PI_F * (1.0f - 800.0f / 8192.0f), fb.angle_rad);

    frame_of(d, 8000, 0, 0, 0);
    dji_decode_feedback(&c, &st, d, &fb);
    TEST_ASSERT_EQUAL_INT32(0, st.turns);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f * PI_F * 7000.0f / 8192.0f, fb.angle_rad);
}

/* GM6020 直驱：角度直接是编码器位置；单圈角在 [-π, π) */
static void test_gm6020_absolute_angle(void)
{
    const MotorConfig c = cfg_of(MOTOR_GM6020, 1, 1, 1.0f);
    DjiMotorState st = { 0 };
    MotorFeedback fb;
    uint8_t d[8];
    frame_of(d, 2048, 0, 0, 30);
    dji_decode_feedback(&c, &st, d, &fb);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, PI_F / 2.0f, fb.angle_rad);
    frame_of(d, 6144, 0, 0, 30);
    dji_decode_feedback(&c, &st, d, &fb);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -PI_F / 2.0f, fb.single_angle_rad);
    frame_of(d, 4096, 0, 0, 30);
    dji_decode_feedback(&c, &st, d, &fb);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -PI_F, fb.single_angle_rad);
}

static void test_m2006_has_no_temperature(void)
{
    const MotorConfig c = cfg_of(MOTOR_M2006, 1, 1, DJI_M2006_GEAR_RATIO);
    DjiMotorState st = { 0 };
    MotorFeedback fb;
    uint8_t d[8];
    frame_of(d, 0, 0, 0, 55);
    dji_decode_feedback(&c, &st, d, &fb);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fb.temperature_c);
}

/* 3 N·m = 10 A = 8192；超量程截到 ±16384；方向取反；M2006 0.9 N·m = 5 A = 5000；GM6020 恒 0 */
static void test_torque_to_raw(void)
{
    MotorConfig c = cfg_of(MOTOR_M3508, 1, 1, DJI_M3508_GEAR_RATIO);
    TEST_ASSERT_EQUAL_INT16(8192, dji_torque_to_raw(&c, 3.0f));
    TEST_ASSERT_EQUAL_INT16(-4096, dji_torque_to_raw(&c, -1.5f));
    TEST_ASSERT_EQUAL_INT16(16384, dji_torque_to_raw(&c, 100.0f));
    TEST_ASSERT_EQUAL_INT16(-16384, dji_torque_to_raw(&c, -100.0f));
    c.direction = -1;
    TEST_ASSERT_EQUAL_INT16(-8192, dji_torque_to_raw(&c, 3.0f));

    c = cfg_of(MOTOR_M2006, 1, 1, DJI_M2006_GEAR_RATIO);
    TEST_ASSERT_EQUAL_INT16(5000, dji_torque_to_raw(&c, 0.9f));
    TEST_ASSERT_EQUAL_INT16(10000, dji_torque_to_raw(&c, 50.0f));

    c = cfg_of(MOTOR_GM6020, 1, 1, 1.0f);
    TEST_ASSERT_EQUAL_INT16(0, dji_torque_to_raw(&c, 1.0f));
}

/* 反馈 → 指令往返：同一电流原始值换成力矩再换回来不变 */
static void test_feedback_and_command_scales_agree(void)
{
    const MotorConfig c = cfg_of(MOTOR_M3508, 1, 1, DJI_M3508_GEAR_RATIO);
    DjiMotorState st = { 0 };
    MotorFeedback fb;
    uint8_t d[8];
    frame_of(d, 0, 0, 1234, 0);
    dji_decode_feedback(&c, &st, d, &fb);
    TEST_ASSERT_EQUAL_INT16(1234, dji_torque_to_raw(&c, fb.torque_nm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_config_valid);
    RUN_TEST(test_ids_and_slots);
    RUN_TEST(test_caps);
    RUN_TEST(test_m3508_feedback_units);
    RUN_TEST(test_direction_flips_signs);
    RUN_TEST(test_multi_turn);
    RUN_TEST(test_gm6020_absolute_angle);
    RUN_TEST(test_m2006_has_no_temperature);
    RUN_TEST(test_torque_to_raw);
    RUN_TEST(test_feedback_and_command_scales_agree);
    return UNITY_END();
}
