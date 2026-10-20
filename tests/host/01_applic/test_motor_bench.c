#include "01_applic/config/motor_bench_robot.h"
#include "tests/host/fakes/fake_can.h"
#include "tests/host/fakes/fake_time.h"
#include "unity.h"
#include <math.h>
#include <string.h>

static MotorBench bench;
static MotorBenchInput input;

void setUp(void)
{
    TEST_ASSERT_TRUE(motor_bench_init(&bench, &motor_bench_config));
    input = (MotorBenchInput){ .feedback = { .online = true, .temperature_c = 26.5f },
                               .rc_online = true,
                               .dt_s = MOTOR_BENCH_PERIOD_S,
                               .settings = { .allow_motion = true, .direction = 1, .gain = 1u } };
    fake_can_reset();
    fake_time_set_us(1000000u);
}
void tearDown(void)
{
}

static void step(RcSwitch sw)
{
    input.sw = sw;
    motor_bench_update(&bench, &input);
}
static void arm(void)
{
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_READY, bench.mode);
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_RUNNING, bench.mode);
}

static void test_startup_and_motion_authorization(void)
{
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_LOCKED, bench.mode);
    step(RC_SW_MID);
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_LOCKED, bench.mode);
    input.settings.allow_motion = false;
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_LOCKED, bench.mode);
    input.settings.allow_motion = true;
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_LOCKED, bench.mode);
    arm();
    input.settings.allow_motion = false;
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_LOCKED, bench.mode);
}

static void test_profiles_finish_and_freeze_selection(void)
{
    for (int direction = -1; direction <= 1; direction += 2)
    {
        for (unsigned ff = 0u; ff < 2u; ++ff)
        {
            setUp();
            input.settings.direction = direction;
            input.settings.feedforward = ff != 0u;
            arm();
            input.settings.direction = -direction;
            input.settings.feedforward = ff == 0u;
            input.settings.gain = 2u;
            bool seen_motion_ff = false;
            for (unsigned k = 0u; k < 7000u && bench.mode == MOTOR_BENCH_RUNNING; ++k)
            {
                input.feedback.speed_rad_s = bench.reference_rad_s;
                step(RC_SW_UP);
                TEST_ASSERT_TRUE(fabsf(bench.reference_rad_s)
                                 <= motor_bench_config.high_rad_s + 1e-5f);
                TEST_ASSERT_TRUE(fabsf(bench.torque_nm) <= motor_bench_config.torque_limit_nm);
                if (bench.ff_active)
                {
                    seen_motion_ff = true;
                }
            }
            TEST_ASSERT_EQUAL(MOTOR_BENCH_DONE, bench.mode);
            TEST_ASSERT_EQUAL(direction, bench.active.direction);
            TEST_ASSERT_EQUAL(1u, bench.active.gain);
            TEST_ASSERT_EQUAL(ff != 0u, seen_motion_ff);
            TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
            TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.integral_nm);
            step(RC_SW_MID);
            step(RC_SW_UP);
            TEST_ASSERT_EQUAL(MOTOR_BENCH_DONE, bench.mode);
        }
    }
}

static void test_faults_latch_and_require_rearming(void)
{
    for (unsigned f = MOTOR_BENCH_RC_LOST; f <= MOTOR_BENCH_BAD_INPUT; ++f)
    {
        setUp();
        arm();
        switch ((MotorBenchFault)f)
        {
            case MOTOR_BENCH_RC_LOST:
                input.rc_online = false;
                break;
            case MOTOR_BENCH_MOTOR_LOST:
                input.feedback.online = false;
                break;
            case MOTOR_BENCH_DRIVER_ERROR:
                input.feedback.error_code = 8u;
                break;
            case MOTOR_BENCH_OVERSPEED:
                input.feedback.speed_rad_s = 11.0f;
                break;
            case MOTOR_BENCH_TEMPERATURE:
                input.feedback.temperature_c = motor_bench_config.stop_temperature_c;
                break;
            case MOTOR_BENCH_LATE:
                input.dt_s = 0.01f;
                break;
            case MOTOR_BENCH_TX_FAILED:
                input.tx_failed = true;
                break;
            case MOTOR_BENCH_TRACKING:
                bench.elapsed_s = 3.0f;
                input.feedback.speed_rad_s = 0.0f;
                break;
            case MOTOR_BENCH_BAD_INPUT:
                input.feedback.speed_rad_s = NAN;
                break;
            default:
                TEST_FAIL_MESSAGE("unexpected fault");
        }
        step(RC_SW_UP);
        TEST_ASSERT_EQUAL(MOTOR_BENCH_FAULT, bench.mode);
        TEST_ASSERT_EQUAL(f, bench.fault);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
        input.feedback = (MotorFeedback){ .online = true, .temperature_c = 26.5f };
        input.rc_online = true;
        input.tx_failed = false;
        input.dt_s = MOTOR_BENCH_PERIOD_S;
        step(RC_SW_UP);
        step(RC_SW_MID);
        step(RC_SW_UP);
        TEST_ASSERT_EQUAL(MOTOR_BENCH_FAULT, bench.mode);
        arm();
    }
    setUp();
    arm();
    input.previous_exec_s = 0.01f;
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_LATE, bench.fault);
}

static void test_combined_output_anti_windup_and_transfer(void)
{
    input.settings.direction = -1;
    input.settings.feedforward = true;
    arm();
    /* 反向运动但误差为正：前馈与积分抵消，总输出未饱和时积分自身也必须受限。 */
    for (unsigned k = 0u; k < 10000u; ++k)
    {
        bench.elapsed_s = 4.0f;
        input.feedback.speed_rad_s = -7.0f;
        step(RC_SW_UP);
        TEST_ASSERT_TRUE(fabsf(bench.integral_nm) <= motor_bench_config.torque_limit_nm);
    }
    TEST_ASSERT_TRUE(bench.saturated);
    for (int direction = -1; direction <= 1; direction += 2)
    {
        setUp();
        input.settings.direction = direction;
        input.settings.feedforward = true;
        arm();
        for (unsigned k = 0u; k < 5000u; ++k)
        {
            bench.elapsed_s = 4.0f; /* 固定在高档，模拟外部负载造成持续误差 */
            input.feedback.speed_rad_s = (float)direction * 5.0f;
            step(RC_SW_UP);
            TEST_ASSERT_TRUE(fabsf(bench.torque_nm) <= motor_bench_config.torque_limit_nm);
        }
        const float room = motor_bench_config.torque_limit_nm - fabsf(bench.friction_nm) - bench.kp;
        TEST_ASSERT_TRUE(fabsf(bench.integral_nm) <= room + 1e-4f);
        bench.elapsed_s = 4.0f;
        input.feedback.speed_rad_s = (float)direction * 6.0f;
        step(RC_SW_UP);
        TEST_ASSERT_FALSE(bench.saturated);
        TEST_ASSERT_TRUE(fabsf(bench.requested_nm) < motor_bench_config.torque_limit_nm);
        step(RC_SW_DOWN);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.integral_nm);
    }
    setUp();
    input.settings.feedforward = true;
    arm();
    bench.integral_nm = 0.1f;
    bench.elapsed_s = 2.801f;
    input.feedback.speed_rad_s = 4.005f;
    step(RC_SW_UP);
    TEST_ASSERT_TRUE(bench.ff_active);
    TEST_ASSERT_FLOAT_WITHIN(
        1e-5f, 0.1f + motor_bench_config.inertia_kg_m2 * 5.0f + motor_bench_config.b_pos * 0.005f,
        bench.torque_nm);
    bench.elapsed_s = 6.001f;
    input.feedback.speed_rad_s = 3.995f;
    step(RC_SW_UP);
    TEST_ASSERT_FALSE(bench.ff_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.1f, bench.torque_nm);
}

static void test_bad_configuration_and_settings(void)
{
    MotorBenchConfig c = motor_bench_config;
    c.tc_s[0] = NAN;
    TEST_ASSERT_FALSE(motor_bench_init(&bench, &c));
    c = motor_bench_config;
    c.high_rad_s = c.low_rad_s;
    TEST_ASSERT_FALSE(motor_bench_init(&bench, &c));
    setUp();
    input.settings.gain = 3u;
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_BAD_SETTINGS, bench.fault);
    setUp();
    input.settings.direction = 0;
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_BAD_SETTINGS, bench.fault);
    setUp();
    input.feedback.speed_rad_s = 0.2f;
    step(RC_SW_DOWN);
    step(RC_SW_MID);
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_LOCKED, bench.mode);
}

static void test_real_motor_frames_and_diagnostics(void)
{
    static Motor motor;
    MotorGroup group = { 0 };
    const Motor *conflict;
    TEST_ASSERT_TRUE(motor_init(&motor, &bench_motor_config, &group, &conflict));
    CanFrame frame = {
        .id = 0x201u, .len = 8u, .stamp_us = 1000000u, .data = { 0, 0, 0, 0, 0, 0, 26, 0 }
    };
    TEST_ASSERT_TRUE(motor_group_receive(&group, CAN_BUS_1, &frame));
    TEST_ASSERT_TRUE(motor_read_feedback(&motor, &input.feedback));
    TEST_ASSERT_EQUAL_UINT32(1u, input.feedback.sequence);
    TEST_ASSERT_EQUAL_UINT64(frame.stamp_us, input.feedback.stamp_us);
    arm();
    bench.elapsed_s = 3.0f;
    input.feedback.speed_rad_s = 4.0f;
    step(RC_SW_UP);
    motor_bench_apply(&bench, &motor);
    motor_group_send(&group);
    TEST_ASSERT_EQUAL(1u, fake_can_sent_count());
    const CanFrame *sent = fake_can_sent(0u);
    TEST_ASSERT_EQUAL_UINT32(0x200u, sent->id);
    TEST_ASSERT_EQUAL(CAN_BUS_1, fake_can_sent_bus(0u));
    TEST_ASSERT_FALSE(sent->is_fd);
    const int16_t raw = (int16_t)(((uint16_t)sent->data[0] << 8) | sent->data[1]);
    TEST_ASSERT_TRUE(raw > 0);
    for (unsigned i = 2u; i < 8u; ++i)
    {
        TEST_ASSERT_EQUAL_UINT8(0u, sent->data[i]);
    }
    MotorTxSnapshot tx;
    motor_group_read_tx(&group, &tx);
    TEST_ASSERT_EQUAL_INT16(raw, tx.raw_current[0]);
    TEST_ASSERT_TRUE(tx.accepted);
    TEST_ASSERT_EQUAL_UINT32(1u, tx.sequence);
    fake_can_set_send_fail(true);
    motor_bench_apply(&bench, &motor);
    motor_group_send(&group);
    motor_group_read_tx(&group, &tx);
    TEST_ASSERT_FALSE(tx.accepted);
    TEST_ASSERT_EQUAL_UINT32(2u, tx.sequence);
    TEST_ASSERT_EQUAL_UINT32(1u, group.tx_dropped);
    fake_can_set_send_fail(false);
    fake_time_advance_ms(MOTOR_TIMEOUT_MS + 1u);
    TEST_ASSERT_FALSE(motor_read_feedback(&motor, &input.feedback));
    step(RC_SW_UP);
    motor_bench_apply(&bench, &motor);
    motor_group_send(&group);
    sent = fake_can_sent(fake_can_sent_count() - 1u);
    for (unsigned i = 0u; i < 8u; ++i)
    {
        TEST_ASSERT_EQUAL_UINT8(0u, sent->data[i]);
    }
    TEST_ASSERT_EQUAL(MOTOR_BENCH_MOTOR_LOST, bench.fault);
}

static void test_temperature_start_and_stop_are_separate(void)
{
    input.feedback.temperature_c = motor_bench_config.start_temperature_c;
    arm();
    input.feedback.temperature_c = motor_bench_config.start_temperature_c + 1.0f;
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_RUNNING, bench.mode);
    input.feedback.temperature_c = motor_bench_config.stop_temperature_c;
    step(RC_SW_UP);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_TEMPERATURE, bench.fault);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
    setUp();
    input.feedback.temperature_c = motor_bench_config.start_temperature_c + 1.0f;
    step(RC_SW_DOWN);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_TEMPERATURE, bench.fault);
}

static void test_configuration_is_atomic_and_requires_stopped_axis(void)
{
    MotorBenchParameters p = motor_bench_parameters(&bench);
    input.sw = RC_SW_DOWN;
    p.torque_limit_nm *= 0.5f;
    p.low_rad_s = 4.5f;
    p.high_rad_s = 5.5f;
    p.speed_limit_rad_s = 8.0f;
    TEST_ASSERT_TRUE(motor_bench_configure(&bench, &p, &input.settings, &input));
    TEST_ASSERT_EQUAL_FLOAT(p.torque_limit_nm, bench.cfg->torque_limit_nm);
    MotorBenchParameters invalid = p;
    invalid.stop_temperature_c = 80.0f;
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &invalid, &input.settings, &input));
    TEST_ASSERT_EQUAL_FLOAT(p.stop_temperature_c, bench.cfg->stop_temperature_c);
    invalid = p;
    invalid.torque_limit_nm = NAN;
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &invalid, &input.settings, &input));
    invalid = p;
    invalid.torque_limit_nm = motor_bench_config.torque_limit_nm + 0.01f;
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &invalid, &input.settings, &input));
    invalid = p;
    invalid.low_rad_s = motor_bench_config.low_rad_s - 0.1f;
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &invalid, &input.settings, &input));
    invalid = p;
    invalid.speed_limit_rad_s = motor_bench_config.speed_limit_rad_s + 0.1f;
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &invalid, &input.settings, &input));
    invalid = p;
    invalid.accel_rad_s2 = motor_bench_config.accel_rad_s2 + 0.1f;
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &invalid, &input.settings, &input));
    invalid = p;
    invalid.start_temperature_c = motor_bench_config.start_temperature_c + 1.0f;
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &invalid, &input.settings, &input));
    input.feedback.speed_rad_s = 0.2f;
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &p, &input.settings, &input));
    input.feedback.speed_rad_s = 0.0f;
    input.rc_online = false;
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &p, &input.settings, &input));
    input.rc_online = true;
    arm();
    TEST_ASSERT_FALSE(motor_bench_configure(&bench, &p, &input.settings, &input));
}

static void test_parameter_mailbox_returns_actual_values_and_disarms(void)
{
    MotorBenchLink link;
    motor_bench_link_init(&link);
    uint8_t request[MOTOR_BENCH_MESSAGE_SIZE] = { 'M', 'B', '0', '2', 17u };
    uint8_t reply[MOTOR_BENCH_MESSAGE_SIZE];
    TEST_ASSERT_FALSE(motor_bench_link_reply(&link, reply));
    motor_bench_link_receive(&link, request, sizeof(request));
    motor_bench_link_control(&link, &bench, &input);
    TEST_ASSERT_TRUE(motor_bench_link_reply(&link, reply));
    TEST_ASSERT_EQUAL_UINT8(17u, reply[4]);
    TEST_ASSERT_EQUAL_UINT8(0u, reply[8]);
    TEST_ASSERT_EQUAL_UINT8(0u, reply[9]);
    memcpy(request, reply, sizeof(request));
    request[8] = 1u;
    request[9] = 3u;
    request[10] = 255u;
    input.sw = RC_SW_DOWN;
    motor_bench_link_receive(&link, request, sizeof(request));
    motor_bench_link_control(&link, &bench, &input);
    TEST_ASSERT_TRUE(motor_bench_link_reply(&link, reply));
    TEST_ASSERT_EQUAL_UINT8(0u, reply[8]);
    TEST_ASSERT_TRUE(input.settings.allow_motion);
    TEST_ASSERT_TRUE(input.settings.feedforward);
    TEST_ASSERT_EQUAL_INT32(-1, input.settings.direction);
    arm();
    motor_bench_link_receive(&link, request, sizeof(request));
    motor_bench_link_control(&link, &bench, &input);
    TEST_ASSERT_TRUE(motor_bench_link_reply(&link, reply));
    TEST_ASSERT_NOT_EQUAL(0u, reply[8]);
    request[8] = 2u;
    motor_bench_link_receive(&link, request, sizeof(request));
    motor_bench_link_control(&link, &bench, &input);
    TEST_ASSERT_TRUE(motor_bench_link_reply(&link, reply));
    step(RC_SW_UP);
    TEST_ASSERT_FALSE(input.settings.allow_motion);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
    request[0] = 'X';
    motor_bench_link_receive(&link, request, sizeof(request));
    motor_bench_link_control(&link, &bench, &input);
    TEST_ASSERT_TRUE(motor_bench_link_reply(&link, reply));
    TEST_ASSERT_EQUAL_UINT8(1u, reply[8]);
    motor_bench_link_receive(&link, request, 8u);
    motor_bench_link_control(&link, &bench, &input);
    TEST_ASSERT_TRUE(motor_bench_link_reply(&link, reply));
    TEST_ASSERT_EQUAL_UINT8(1u, reply[8]);
    /* SET 尚未处理时撤销到达：SET 可以被回执，但不能覆盖撤销。 */
    memcpy(request, reply, sizeof(request));
    request[8] = 1u;
    request[9] = 3u;
    input.sw = RC_SW_DOWN;
    input.feedback.speed_rad_s = 0.0f;
    motor_bench_link_receive(&link, request, sizeof(request));
    request[8] = 2u;
    motor_bench_link_receive(&link, request, sizeof(request));
    motor_bench_link_control(&link, &bench, &input);
    TEST_ASSERT_FALSE(input.settings.allow_motion);
    TEST_ASSERT_TRUE(motor_bench_link_reply(&link, reply));
    TEST_ASSERT_EQUAL_UINT8(0u, reply[8]);
    TEST_ASSERT_EQUAL_UINT8(2u, reply[9]);
}

static void usb_request(MotorBenchLink *link, uint8_t *request, uint8_t action, uint8_t status)
{
    uint8_t reply[MOTOR_BENCH_MESSAGE_SIZE];
    request[8] = action;
    motor_bench_link_receive(link, request, MOTOR_BENCH_MESSAGE_SIZE);
    motor_bench_link_control(link, &bench, &input);
    TEST_ASSERT_TRUE(motor_bench_link_reply(link, reply));
    TEST_ASSERT_EQUAL_UINT8(status, reply[8]);
    motor_bench_update(&bench, &input);
}

static void usb_prepare(MotorBenchLink *link, uint8_t *request)
{
    motor_bench_link_init(link);
    input.rc_online = false;
    input.sw = RC_SW_UP;
    /* GET 填充当前实际浮点值，随后用 USB SET 准备。 */
    usb_request(link, request, 0u, 0u);
    MotorBenchParameters p = motor_bench_parameters(&bench);
    const float values[] = { p.torque_limit_nm,   p.low_rad_s,         p.high_rad_s,
                             p.accel_rad_s2,      p.speed_limit_rad_s, p.start_temperature_c,
                             p.stop_temperature_c };
    memcpy(request + 12, values, sizeof(values));
    request[9] = 5u;
    request[10] = 1u;
    request[11] = 1u;
    usb_request(link, request, 1u, 0u);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_READY, bench.mode);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
}

static void test_usb_start_stop_timeout_and_session(void)
{
    MotorBenchLink link;
    uint8_t r[MOTOR_BENCH_MESSAGE_SIZE] = { 'M', 'B', '0', '2', 17u };
    usb_prepare(&link, r);
    /* 拨杆无论怎样切换都不启动 USB 模式。 */
    for (unsigned sw = 1u; sw <= 3u; ++sw)
    {
        motor_bench_link_control(&link, &bench, &input);
        step((RcSwitch)sw);
        TEST_ASSERT_EQUAL(MOTOR_BENCH_READY, bench.mode);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
    }
    r[4] = 18u;
    usb_request(&link, r, 3u, 3u);
    r[4] = 17u;
    usb_request(&link, r, 3u, 0u);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_RUNNING, bench.mode);
    usb_request(&link, r, 3u, 3u); /* START 不得重置已运行轨迹。 */
    fake_time_advance_ms(499u);
    motor_bench_link_control(&link, &bench, &input);
    motor_bench_update(&bench, &input);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_RUNNING, bench.mode);
    fake_time_advance_ms(1u);
    motor_bench_link_control(&link, &bench, &input);
    motor_bench_update(&bench, &input);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_USB_LOST, bench.fault);
    TEST_ASSERT_FALSE(bench.settings.allow_motion);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
    r[8] = 4u;
    motor_bench_link_receive(&link, r, sizeof(r));
    usb_request(&link, r, 3u, 3u); /* 恢复心跳不能续跑。 */
    usb_request(&link, r, 1u, 0u);
    /* 启动请求占邮箱时到达停止，停止必须优先。 */
    r[8] = 3u;
    motor_bench_link_receive(&link, r, sizeof(r));
    r[8] = 2u;
    motor_bench_link_receive(&link, r, sizeof(r));
    motor_bench_link_control(&link, &bench, &input);
    motor_bench_update(&bench, &input);
    TEST_ASSERT_FALSE(input.usb_start);
    TEST_ASSERT_FALSE(bench.settings.allow_motion);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
}

static void test_usb_motor_faults_and_ready_timeout(void)
{
    for (unsigned f = 0u; f < 4u; ++f)
    {
        setUp();
        MotorBenchLink link;
        uint8_t r[MOTOR_BENCH_MESSAGE_SIZE] = { 'M', 'B', '0', '2', 31u };
        usb_prepare(&link, r);
        if (f == 0u)
        {
            fake_time_advance_ms(400u);
            r[4] = 99u;
            r[8] = 4u;
            motor_bench_link_receive(&link, r, sizeof(r));
            r[4] = 31u;
            /* 旧会话心跳不能续期；尚未 START 也必须超时撤销准备。 */
            fake_time_advance_ms(100u);
        }
        else
        {
            usb_request(&link, r, 3u, 0u);
            if (f == 1u)
            {
                input.feedback.online = false;
            }
            else if (f == 2u)
            {
                input.feedback.temperature_c = motor_bench_config.stop_temperature_c;
            }
            else
            {
                input.feedback.speed_rad_s = motor_bench_config.speed_limit_rad_s + 1.0f;
            }
        }
        motor_bench_link_control(&link, &bench, &input);
        motor_bench_update(&bench, &input);
        TEST_ASSERT_EQUAL(MOTOR_BENCH_FAULT, bench.mode);
        TEST_ASSERT_FALSE(bench.settings.allow_motion);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.torque_nm);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, bench.integral_nm);
        input.feedback = (MotorFeedback){ .online = true, .temperature_c = 26.5f };
        r[8] = 4u;
        motor_bench_link_receive(&link, r, sizeof(r));
        usb_request(&link, r, 3u, 3u);
    }
}

static void test_usb_heartbeat_wrap_and_completion(void)
{
    MotorBenchLink link;
    uint8_t r[MOTOR_BENCH_MESSAGE_SIZE] = { 'M', 'B', '0', '2', 23u };
    fake_time_set_us(UINT32_MAX - 100000u);
    usb_prepare(&link, r);
    fake_time_advance_ms(400u);
    r[8] = 4u;
    motor_bench_link_receive(&link, r, sizeof(r));
    fake_time_advance_ms(400u);
    usb_request(&link, r, 3u, 0u);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_RUNNING, bench.mode);
    bench.elapsed_s = 100.0f;
    motor_bench_link_control(&link, &bench, &input);
    motor_bench_update(&bench, &input);
    TEST_ASSERT_EQUAL(MOTOR_BENCH_DONE, bench.mode);
    TEST_ASSERT_FALSE(bench.settings.allow_motion);
    usb_request(&link, r, 3u, 3u);
    r[4] = 24u;
    usb_request(&link, r, 1u, 0u);
    r[4] = 23u;
    usb_request(&link, r, 3u, 3u);
    /* 旧 MB01 不能触发新固件运动。 */
    r[3] = '1';
    usb_request(&link, r, 3u, 1u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_startup_and_motion_authorization);
    RUN_TEST(test_profiles_finish_and_freeze_selection);
    RUN_TEST(test_faults_latch_and_require_rearming);
    RUN_TEST(test_combined_output_anti_windup_and_transfer);
    RUN_TEST(test_bad_configuration_and_settings);
    RUN_TEST(test_real_motor_frames_and_diagnostics);
    RUN_TEST(test_temperature_start_and_stop_are_separate);
    RUN_TEST(test_configuration_is_atomic_and_requires_stopped_axis);
    RUN_TEST(test_parameter_mailbox_returns_actual_values_and_disarms);
    RUN_TEST(test_usb_start_stop_timeout_and_session);
    RUN_TEST(test_usb_heartbeat_wrap_and_completion);
    RUN_TEST(test_usb_motor_faults_and_ready_timeout);
    return UNITY_END();
}
