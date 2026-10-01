/**
 * @file    test_dm_motor.c
 * @brief   dm_motor 的单元测试：配置检查、MIT 编码（含截断与方向）、反馈解析与状态码、命令帧
 * @note    期望字节按附录 A.3 的位布局手算，范围取旧工程 DM8009 的 ±π / 45 / 54
 */
#include "02_devices/motor/dm_motor.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
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

static void test_config_valid(void)
{
    MotorConfig c = dm_cfg(1);
    TEST_ASSERT_TRUE(dm_config_valid(&c));
    c.id = 0u;
    TEST_ASSERT_FALSE(dm_config_valid(&c));
    c.id = 16u; /* 反馈第 0 字节只有 4 位放 ID */
    TEST_ASSERT_FALSE(dm_config_valid(&c));
    c = dm_cfg(1);
    c.gear_ratio = 10.0f; /* 反馈已是输出轴 */
    TEST_ASSERT_FALSE(dm_config_valid(&c));
    c = dm_cfg(1);
    c.dm.master_id = 1u; /* 与 CAN ID 相同 */
    TEST_ASSERT_FALSE(dm_config_valid(&c));
    c = dm_cfg(1);
    c.dm.damp_kd = 6.0f;
    TEST_ASSERT_FALSE(dm_config_valid(&c));
    c = dm_cfg(1);
    c.dm.t_max = 0.0f;
    TEST_ASSERT_FALSE(dm_config_valid(&c));
    c = dm_cfg(0);
    TEST_ASSERT_FALSE(dm_config_valid(&c));
}

/* 全 0：位置 32767、速度 2047、Kp 0、Kd 0、力矩 2047 */
static void test_encode_zero_mit(void)
{
    const MotorConfig c = dm_cfg(1);
    uint8_t d[8];
    dm_encode_mit(&c, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, d);
    const uint8_t expect[8] = { 0x7F, 0xFF, 0x7F, 0xF0, 0x00, 0x00, 0x07, 0xFF };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, d, 8);
}

/* 力矩 10 N·m：(10 + 54) × 4095 / 108 = 2426.6 → 2426 = 0x97A；Kd 1.0：1 / 5 × 4095 = 819 = 0x333 */
static void test_encode_torque_and_kd(void)
{
    const MotorConfig c = dm_cfg(1);
    uint8_t d[8];
    dm_encode_mit(&c, 0.0f, 0.0f, 0.0f, 1.0f, 10.0f, d);
    TEST_ASSERT_EQUAL_HEX8(0x33, d[5]);
    TEST_ASSERT_EQUAL_HEX8(0x39, d[6]); /* Kd 低 4 位 3、力矩高 4 位 9 */
    TEST_ASSERT_EQUAL_HEX8(0x7A, d[7]);
}

/* 超出范围截到端点：旧代码不截断，-100 N·m 会绕回成很大的数 */
static void test_encode_clamps(void)
{
    const MotorConfig c = dm_cfg(1);
    uint8_t d[8];
    dm_encode_mit(&c, 10.0f, 100.0f, 1000.0f, 10.0f, -100.0f, d);
    const uint8_t expect[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x00 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, d, 8);
}

/* 方向为 -1：+10 N·m 按 -10 编码：(−10 + 54) × 4095 / 108 = 1668.3 → 1668 = 0x684 */
static void test_encode_direction(void)
{
    const MotorConfig c = dm_cfg(-1);
    uint8_t d[8];
    dm_encode_mit(&c, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f, d);
    TEST_ASSERT_EQUAL_HEX8(0x06, d[6] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x84, d[7]);
}

static void test_decode_feedback(void)
{
    const MotorConfig c = dm_cfg(1);
    MotorFeedback fb;
    /* 状态 1（使能）、ID 1；位置满量程、速度 0x800、力矩 0x000；MOS 30 °C、线圈 40 °C */
    const uint8_t d[8] = { 0x11, 0xFF, 0xFF, 0x80, 0x00, 0x00, 30, 40 };
    dm_decode_feedback(&c, d, &fb);
    TEST_ASSERT_TRUE(fb.enabled);
    TEST_ASSERT_EQUAL_UINT8(0, fb.error_code);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.141593f, fb.angle_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, fb.speed_rad_s); /* 0x800 比中点多半个量化单位 */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -54.0f, fb.torque_nm);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, fb.temperature_c);
}

static void test_decode_state_codes(void)
{
    const MotorConfig c = dm_cfg(1);
    MotorFeedback fb;
    uint8_t d[8] = { 0x01, 0x80, 0x00, 0x80, 0x08, 0x00, 0, 0 };
    dm_decode_feedback(&c, d, &fb);
    TEST_ASSERT_FALSE(fb.enabled); /* 状态 0：失能 */
    TEST_ASSERT_EQUAL_UINT8(0, fb.error_code);
    d[0] = 0xD1; /* 0xD 通信丢失 */
    dm_decode_feedback(&c, d, &fb);
    TEST_ASSERT_FALSE(fb.enabled);
    TEST_ASSERT_EQUAL_HEX8(0x0D, fb.error_code);
}

static void test_decode_direction(void)
{
    const MotorConfig c = dm_cfg(-1);
    MotorFeedback fb;
    const uint8_t d[8] = { 0x11, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0 };
    dm_decode_feedback(&c, d, &fb);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -3.141593f, fb.angle_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -45.0f, fb.speed_rad_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -54.0f, fb.torque_nm);
}

static void test_command_frames(void)
{
    uint8_t d[8];
    dm_encode_command(DM_CMD_ENABLE, d);
    const uint8_t enable[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(enable, d, 8);
    dm_encode_command(DM_CMD_DISABLE, d);
    TEST_ASSERT_EQUAL_HEX8(0xFD, d[7]);
    dm_encode_command(DM_CMD_CLEAR_ERROR, d);
    TEST_ASSERT_EQUAL_HEX8(0xFB, d[7]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_config_valid);
    RUN_TEST(test_encode_zero_mit);
    RUN_TEST(test_encode_torque_and_kd);
    RUN_TEST(test_encode_clamps);
    RUN_TEST(test_encode_direction);
    RUN_TEST(test_decode_feedback);
    RUN_TEST(test_decode_state_codes);
    RUN_TEST(test_decode_direction);
    RUN_TEST(test_command_frames);
    return UNITY_END();
}
