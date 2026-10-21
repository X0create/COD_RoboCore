/** 1 kHz MCU 闭环与记录生产者；USB 发送在接收任务，不阻塞控制。 */
#include "01_applic/config/motor_bench_robot.h"
#include "01_applic/modules/motor_bench/telemetry/sysid/sysid.h"
#include "01_applic/tasks/tasks.h"
#include "05_platform/time/time.h"
#include "05_platform/trace/trace.h"
#include "05_platform/usb_cdc/usb_cdc.h"

volatile uint32_t control_heartbeat;
static struct
{
    float q, w, tau, temperature, reference, acceleration, pi, integral, friction, inertia;
    float requested, command, raw_current, feedback_current, period_us, exec_us, elapsed, direction;
    uint32_t mode, phase, fault, gain, ff_selected, ff_active, saturated;
    uint32_t fb_sequence, fb_age_us, rc_online, rc_switch, tx_sequence, tx_accepted, tx_dropped;
    uint32_t fb_stamp_us, tx_stamp_us, motion_allowed, error_code;
    uint32_t usb_control, usb_online, session;
} record;
static const SysIdChannel channels[] = {
    SYSID_F32("motor.q", "rad", &record.q),
    SYSID_F32("motor.w", "rad/s", &record.w),
    SYSID_F32("motor.tau", "N.m", &record.tau),
    SYSID_F32("motor.temperature", "degC", &record.temperature),
    SYSID_F32("control.reference", "rad/s", &record.reference),
    SYSID_F32("control.acceleration", "rad/s2", &record.acceleration),
    SYSID_F32("control.pi", "N.m", &record.pi),
    SYSID_F32("control.integral", "N.m", &record.integral),
    SYSID_F32("ff.friction", "N.m", &record.friction),
    SYSID_F32("ff.inertia", "N.m", &record.inertia),
    SYSID_F32("control.requested", "N.m", &record.requested),
    SYSID_F32("control.command", "N.m", &record.command),
    SYSID_F32("can.raw_current", "raw", &record.raw_current),
    SYSID_F32("motor.current", "A", &record.feedback_current),
    SYSID_F32("control.period", "us", &record.period_us),
    SYSID_F32("control.exec", "us", &record.exec_us),
    SYSID_F32("bench.elapsed", "s", &record.elapsed),
    SYSID_F32("bench.direction", "", &record.direction),
    SYSID_U32("bench.mode", "", &record.mode),
    SYSID_U32("bench.phase", "", &record.phase),
    SYSID_U32("bench.fault", "", &record.fault),
    SYSID_U32("bench.gain", "", &record.gain),
    SYSID_U32("ff.selected", "", &record.ff_selected),
    SYSID_U32("ff.active", "", &record.ff_active),
    SYSID_U32("control.saturated", "", &record.saturated),
    SYSID_U32("motor.fb_sequence", "count", &record.fb_sequence),
    SYSID_U32("motor.fb_age", "us", &record.fb_age_us),
    SYSID_U32("rc.online", "", &record.rc_online),
    SYSID_U32("rc.switch", "", &record.rc_switch),
    SYSID_U32("can.tx_sequence", "count", &record.tx_sequence),
    SYSID_U32("can.tx_accepted", "", &record.tx_accepted),
    SYSID_U32("can.tx_dropped", "count", &record.tx_dropped),
    SYSID_U32("motor.fb_stamp", "tick", &record.fb_stamp_us),
    SYSID_U32("can.tx_stamp", "tick", &record.tx_stamp_us),
    SYSID_U32("bench.motion_allowed", "", &record.motion_allowed),
    SYSID_U32("motor.error_code", "", &record.error_code),
    SYSID_U32("bench.usb_control", "", &record.usb_control),
    SYSID_U32("usb.online", "", &record.usb_online),
    SYSID_U32("bench.session", "", &record.session),
};

static bool send_record(const uint8_t *frame, uint16_t length)
{
#if defined(RM_MOTOR_BENCH_TIMING)
    RM_TRACE_EVENT(RM_TRACE_FRAME_BEGIN, ((uint32_t)frame[3] << 16) | length);
    const bool accepted = usb_cdc_write(frame, length);
    RM_TRACE_EVENT(RM_TRACE_FRAME_END, accepted);
    return accepted;
#else
    return usb_cdc_write(frame, length);
#endif
}

static void receive_command(uint8_t type, const uint8_t *data, uint16_t length)
{
    if (type == MOTOR_BENCH_COMMAND)
    {
        motor_bench_link_receive(&bench_link, data, length);
    }
}

bool motor_bench_log_init(void)
{
    static const SysIdConfig cfg = { .source = "MC02 M3508 control bench",
                                     .channels = channels,
                                     .channel_count = sizeof(channels) / sizeof(channels[0]),
                                     .period_s = MOTOR_BENCH_PERIOD_S,
                                     .clock_hz = 1000000u,
                                     .send = send_record,
                                     .command = receive_command };
    return sysid_init(&cfg);
}

static void update_record(const MotorBenchInput *in, const MotorTxSnapshot *tx, uint64_t now_us)
{
    const MotorFeedback *fb = &in->feedback;
    record.q = fb->angle_rad;
    record.w = fb->speed_rad_s;
    record.tau = fb->torque_nm;
    record.temperature = fb->temperature_c;
    record.reference = motor_bench.reference_rad_s;
    record.acceleration = motor_bench.acceleration_rad_s2;
    record.pi = motor_bench.pi_nm;
    record.integral = motor_bench.integral_nm;
    record.friction = motor_bench.friction_nm;
    record.inertia = motor_bench.inertia_nm;
    record.requested = motor_bench.requested_nm;
    record.command = motor_bench.torque_nm;
    record.raw_current = (float)tx->raw_current[0];
    record.feedback_current = fb->torque_nm / DJI_M3508_NM_PER_A;
    record.period_us = in->dt_s * 1.0e6f;
    record.elapsed = motor_bench.mode == MOTOR_BENCH_RUNNING ? motor_bench.elapsed_s - in->dt_s
                                                             : motor_bench.elapsed_s;
    record.direction = (float)motor_bench.active.direction;
    record.mode = (uint32_t)motor_bench.mode;
    record.phase = motor_bench.phase;
    record.fault = (uint32_t)motor_bench.fault;
    record.gain = motor_bench.active.gain;
    record.ff_selected = motor_bench.active.feedforward ? 1u : 0u;
    record.ff_active = motor_bench.ff_active ? 1u : 0u;
    record.saturated = motor_bench.saturated ? 1u : 0u;
    record.fb_sequence = fb->sequence;
    record.fb_age_us = (uint32_t)(now_us - fb->stamp_us);
    record.rc_online = in->rc_online ? 1u : 0u;
    record.rc_switch = (uint32_t)in->sw;
    record.tx_sequence = tx->sequence;
    record.tx_accepted = tx->accepted ? 1u : 0u;
    record.tx_dropped = bench_motors.tx_dropped;
    record.fb_stamp_us = (uint32_t)fb->stamp_us;
    record.tx_stamp_us = (uint32_t)tx->stamp_us;
    record.motion_allowed = motor_bench.settings.allow_motion ? 1u : 0u;
    record.error_code = fb->error_code;
    record.usb_control = in->settings.usb_control ? 1u : 0u;
    record.usb_online = in->usb_online ? 1u : 0u;
    record.session = motor_bench_link_session(&bench_link);
}

void motor_bench_task_entry(void *argument)
{
    (void)argument;
    RmTaskPeriod wake = rm_task_period_start();
    uint64_t previous_us = rm_time_now_us();
    bool first = true;
    uint32_t previous_drops = 0u;
    float previous_exec_s = 0.0f;
    for (;;)
    {
        const uint64_t start_us = rm_time_now_us();
        RM_TRACE_ENTER(start_us);
        MotorBenchInput input = { .dt_s = first ? MOTOR_BENCH_PERIOD_S
                                                : (float)(start_us - previous_us) * 1.0e-6f,
                                  .previous_exec_s = previous_exec_s,
                                  .tx_failed = bench_motors.tx_dropped != previous_drops };
        first = false;
        previous_us = start_us;
        previous_drops = bench_motors.tx_dropped;
        input.feedback.online = motor_read_feedback(&bench_motor, &input.feedback);
        RcState rc;
        input.rc_online = dr16_read(&bench_rc, &rc);
        input.sw = rc.sw[MOTOR_BENCH_SWITCH];
        const uint64_t sample_us = rm_time_now_us();
        RM_TRACE_EVENT(RM_TRACE_SAMPLE, sample_us);
        motor_bench_link_control(&bench_link, &motor_bench, &input);
        motor_bench_update(&motor_bench, &input);
        RM_TRACE_STATE(motor_bench_link_session(&bench_link), motor_bench.settings.allow_motion);
        motor_bench_apply(&motor_bench, &bench_motor);
        RM_TRACE_EVENT(RM_TRACE_SUBMIT_BEGIN, 0u);
        motor_group_send(&bench_motors);
        RM_TRACE_EVENT(RM_TRACE_SUBMIT_END, 0u);
        MotorTxSnapshot tx;
        motor_group_read_tx(&bench_motors, &tx);
        update_record(&input, &tx, sample_us);
        record.exec_us = (float)(rm_time_now_us() - start_us);
        RM_TRACE_EVENT(RM_TRACE_RECORD_BEGIN, 0u);
        sysid_record((uint32_t)sample_us);
        RM_TRACE_EVENT(RM_TRACE_RECORD_END, 0u);
        previous_exec_s = (float)(rm_time_now_us() - start_us) * 1.0e-6f;
        control_heartbeat++;
        RM_TRACE_END();
        rm_task_delay_until(&wake, MOTOR_BENCH_PERIOD_MS);
    }
}
