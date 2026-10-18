#include "01_applic/config/hand_guide_robot.h"
#include "05_platform/time/time.h"
#include "tests/host/fakes/fake_can.h"
#include "tests/host/fakes/fake_time.h"
#include "unity.h"
#include <math.h>

static HandGuide guide;
static MotorFeedback fb;
void setUp(void)
{
    TEST_ASSERT_TRUE(hand_guide_init(&guide, &hand_guide_config));
    fb = (MotorFeedback){ .online = true, .enabled = true, .temperature_c = 30.0f };
}
void tearDown(void)
{
}
static void step(RcSwitch sw)
{
    hand_guide_update(&guide, &fb, true, sw, 0.001f, false);
}
static void arm(void)
{
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    step(RC_SW_UP);
}
static void test_boot_and_sequence(void)
{
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);
    step(RC_SW_DOWN);
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);
    TEST_ASSERT_EQUAL(HAND_GUIDE_ARM_MOVING, guide.fault);
    step(RC_SW_MID);
    TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);
    arm();
    TEST_ASSERT_EQUAL(HAND_GUIDE_ASSIST, guide.mode);
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.0f, guide.torque_nm);
}
static void test_baseline_no_assist(void)
{
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    TEST_ASSERT_TRUE(guide.request_enable);
    fb.speed_rad_s = 1.0f;
    step(RC_SW_MID);
    TEST_ASSERT_FALSE(guide.request_enable);
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.0f, guide.torque_nm);
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(HAND_GUIDE_ARM_MOVING, guide.fault);
    TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);
}
static void test_compensation_and_reversal(void)
{
    arm();
    fb.speed_rad_s = 0.5f;
    step(RC_SW_UP);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.09285f, guide.torque_nm);
    fb.speed_rad_s = 0.125f;
    step(RC_SW_UP);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.046425f, guide.torque_nm);
    fb.speed_rad_s = 0.01f;
    step(RC_SW_UP);
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.0f, guide.torque_nm);
    fb.speed_rad_s = -0.5f;
    step(RC_SW_UP);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.09285f, guide.torque_nm);
    step(RC_SW_MID);
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.0f, guide.torque_nm);
}
static void test_faults_latched_and_rearm(void)
{
    for (unsigned k = 0u; k < 8u; k++)
    {
        setUp();
        arm();
        bool rc_ok = true, tx_failed = false;
        float dt_s = 0.001f;
        const HandGuideFault expected[] = { HAND_GUIDE_RC_LOST,      HAND_GUIDE_MOTOR_LOST,
                                            HAND_GUIDE_DRIVER_ERROR, HAND_GUIDE_OVERSPEED,
                                            HAND_GUIDE_TEMPERATURE,  HAND_GUIDE_LATE,
                                            HAND_GUIDE_TX_FAILED,    HAND_GUIDE_DISABLED };
        switch (k)
        {
            case 0u:
                rc_ok = false;
                break;
            case 1u:
                fb.online = false;
                break;
            case 2u:
                fb.error_code = 8u;
                break;
            case 3u:
                fb.speed_rad_s = 1.5f;
                break;
            case 4u:
                fb.temperature_c = 43.0f;
                break;
            case 5u:
                dt_s = 0.006f;
                break;
            case 6u:
                tx_failed = true;
                break;
            default:
                fb.enabled = false;
                break;
        }
        hand_guide_update(&guide, &fb, rc_ok, RC_SW_UP, dt_s, tx_failed);
        TEST_ASSERT_EQUAL(expected[k], guide.fault);
        TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);
        TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.0f, guide.torque_nm);
        fb = (MotorFeedback){ .online = true, .enabled = true, .temperature_c = 30.0f };
        step(RC_SW_UP);
        step(RC_SW_MID);
        TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);
        TEST_ASSERT_EQUAL(expected[k], guide.fault);
        arm();
        TEST_ASSERT_EQUAL(HAND_GUIDE_ASSIST, guide.mode);
    }
}
static void test_enable_timeout_and_cold_lock(void)
{
    fb.enabled = false;
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    for (unsigned i = 0u; i < 210u; i++)
    {
        step(RC_SW_MID);
    }
    TEST_ASSERT_EQUAL(HAND_GUIDE_ENABLE_FAILED, guide.fault);
    TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);
    fb.temperature_c = 23.0f;
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);
}
static void test_invalid_config(void)
{
    HandGuideConfig c = hand_guide_config;
    c.fraction = 1.0f;
    TEST_ASSERT_FALSE(hand_guide_init(&guide, &c));
    c = hand_guide_config;
    c.coulomb_nm = NAN;
    TEST_ASSERT_FALSE(hand_guide_init(&guide, &c));
    c = hand_guide_config;
    c.full_assist_rad_s = c.deadband_rad_s;
    TEST_ASSERT_FALSE(hand_guide_init(&guide, &c));
}
static void test_real_motor_output_and_stop(void)
{
    static Motor motor;
    MotorGroup group = { 0 };
    const Motor *conflict;
    fake_can_reset();
    fake_can_set_bus_fd(CAN_BUS_3, true);
    fake_time_set_us(1000000u);
    TEST_ASSERT_TRUE(motor_init(&motor, &hand_motor_config, &group, &conflict));
    TEST_ASSERT_NULL(motor.next);
    CanFrame feedback = { .id = 0x11u,
                          .len = 8u,
                          .is_fd = true,
                          .stamp_us = 1000000u,
                          .data = { 0x11u, 0x80u, 0u, 0x80u, 0x08u, 0u, 30u, 30u } };
    TEST_ASSERT_TRUE(motor_group_receive(&group, CAN_BUS_3, &feedback));
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    hand_guide_apply(&guide, &motor); /* 不漏掉进入基线时唯一一次使能请求 */
    step(RC_SW_UP);
    fb.speed_rad_s = 0.5f;
    step(RC_SW_UP);
    hand_guide_apply(&guide, &motor);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL(1u, fake_can_sent_count());
    const CanFrame *sent = fake_can_sent(0u);
    TEST_ASSERT_EQUAL(CAN_BUS_3, fake_can_sent_bus(0u));
    TEST_ASSERT_EQUAL(1u, sent->id);
    TEST_ASSERT_TRUE(sent->is_fd);
    /* MIT Kp/Kd 位必须全为零；沿用设备层截尾编码，量化误差小于一个力矩刻度。 */
    TEST_ASSERT_EQUAL(0u, sent->data[3] & 0x0Fu);
    TEST_ASSERT_EQUAL(0u, sent->data[4]);
    TEST_ASSERT_EQUAL(0u, sent->data[5]);
    TEST_ASSERT_EQUAL(0u, sent->data[6] & 0xF0u);
    const unsigned raw = ((unsigned)(sent->data[6] & 0x0Fu) << 8) | sent->data[7];
    const float sent_tau = (float)raw * (80.0f / 4095.0f) - 40.0f;
    TEST_ASSERT_FLOAT_WITHIN(80.0f / 4095.0f + 1e-5f, guide.torque_nm, sent_tau);
    /* 实际设备时间戳过期：仍保持上档，模块也必须撤力且不自动恢复。 */
    fake_time_advance_ms(MOTOR_TIMEOUT_MS + 1u);
    MotorFeedback stale;
    TEST_ASSERT_FALSE(motor_read_feedback(&motor, &stale));
    hand_guide_update(&guide, &stale, true, RC_SW_UP, 0.001f, false);
    TEST_ASSERT_EQUAL(HAND_GUIDE_MOTOR_LOST, guide.fault);
    TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);

    hand_guide_apply(&guide, &motor);
    motor_group_send(&group);
    sent = fake_can_sent(1u);
    TEST_ASSERT_EQUAL(0u, sent->data[5]);
    TEST_ASSERT_EQUAL(0x07u, sent->data[6]);
    TEST_ASSERT_EQUAL(0xFFu, sent->data[7]); /* 离线时既有设备层发零请求 MIT 轮询 */
    TEST_ASSERT_EQUAL(MOTOR_OUT_DISABLE, motor.out.kind);
    feedback.stamp_us = rm_time_now_us();
    TEST_ASSERT_TRUE(motor_group_receive(&group, CAN_BUS_3, &feedback));
    TEST_ASSERT_TRUE(motor_read_feedback(&motor, &stale));
    hand_guide_update(&guide, &stale, true, RC_SW_UP, 0.001f, false);
    TEST_ASSERT_EQUAL(HAND_GUIDE_OFF, guide.mode);
    TEST_ASSERT_FALSE(guide.request_enable);
    hand_guide_apply(&guide, &motor);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL(0xFDu, fake_can_sent(2u)->data[7]); /* 重新有反馈后执行失能，不重启 */
}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_and_sequence);
    RUN_TEST(test_baseline_no_assist);
    RUN_TEST(test_compensation_and_reversal);
    RUN_TEST(test_faults_latched_and_rearm);
    RUN_TEST(test_enable_timeout_and_cold_lock);
    RUN_TEST(test_invalid_config);
    RUN_TEST(test_real_motor_output_and_stop);
    return UNITY_END();
}
