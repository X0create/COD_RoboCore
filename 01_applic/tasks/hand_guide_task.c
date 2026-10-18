/** 1 kHz MCU 控制；USB 只记录，不参与方向判断或使能。 */
#include "01_applic/config/hand_guide_robot.h"
#include "01_applic/modules/hand_guide/telemetry/sysid/sysid.h"
#include "01_applic/tasks/tasks.h"
#include "05_platform/time/time.h"
#include "05_platform/usb_cdc/usb_cdc.h"

volatile uint32_t control_heartbeat;
static struct
{
    float q, w, tau_feedback, tau_command, temperature, period_us, exec_us;
    uint32_t mode, fault, online, enabled, rc_online, switch_position, tx_dropped;
} record;
static const SysIdChannel channels[] = {
    SYSID_F32("motor.q", "rad", &record.q),
    SYSID_F32("motor.w", "rad/s", &record.w),
    SYSID_F32("motor.tau", "N.m", &record.tau_feedback),
    SYSID_F32("assist.command", "N.m", &record.tau_command),
    SYSID_F32("motor.temperature", "degC", &record.temperature),
    SYSID_F32("control.period", "us", &record.period_us),
    SYSID_F32("control.exec", "us", &record.exec_us),
    SYSID_U32("hand.mode", "", &record.mode),
    SYSID_U32("hand.fault", "", &record.fault),
    SYSID_U32("motor.online", "", &record.online),
    SYSID_U32("motor.enabled", "", &record.enabled),
    SYSID_U32("rc.online", "", &record.rc_online),
    SYSID_U32("rc.switch", "", &record.switch_position),
    SYSID_U32("can.tx_dropped", "", &record.tx_dropped),
};

static bool send_record(const uint8_t *frame, uint16_t length)
{
    return usb_cdc_write(frame, length);
}

bool hand_guide_log_init(void)
{
    static const SysIdConfig cfg = { .source = "MC02 DM4340 hand guide",
                                     .channels = channels,
                                     .channel_count = sizeof(channels) / sizeof(channels[0]),
                                     .period_s = 0.001f * (float)HAND_GUIDE_RECORD_DIVIDER,
                                     .clock_hz = 1000000u,
                                     .send = send_record };
    return sysid_init(&cfg);
}

void hand_guide_task_entry(void *argument)
{
    (void)argument;
    RmTaskPeriod wake = rm_task_period_start();
    uint64_t previous_us = rm_time_now_us();
    bool first_cycle = true;
    uint32_t previous_drops = 0u, divider = 0u;
    for (;;)
    {
        const uint64_t start_us = rm_time_now_us();
        const float period_s = first_cycle ? 0.001f : (float)(start_us - previous_us) * 1.0e-6f;
        first_cycle = false;
        previous_us = start_us;
        MotorFeedback fb;
        const bool motor_online = motor_read_feedback(&hand_motor, &fb);
        RcState rc;
        const bool rc_online = dr16_read(&hand_rc, &rc);
        const bool tx_failed = hand_motors.tx_dropped != previous_drops;
        previous_drops = hand_motors.tx_dropped;
        hand_guide_update(&hand_guide, &fb, rc_online, rc.sw[HAND_GUIDE_SWITCH], period_s,
                          tx_failed);
        hand_guide_apply(&hand_guide, &hand_motor);
        motor_group_send(&hand_motors);
        record.q = fb.angle_rad;
        record.w = fb.speed_rad_s;
        record.tau_feedback = fb.torque_nm;
        record.tau_command = hand_guide.torque_nm;
        record.temperature = fb.temperature_c;
        record.period_us = period_s * 1.0e6f;
        record.exec_us = (float)(rm_time_now_us() - start_us);
        record.mode = (uint32_t)hand_guide.mode;
        record.fault = (uint32_t)hand_guide.fault;
        record.online = motor_online ? 1u : 0u;
        record.enabled = fb.enabled ? 1u : 0u;
        record.rc_online = rc_online ? 1u : 0u;
        record.switch_position = (uint32_t)rc.sw[HAND_GUIDE_SWITCH];
        record.tx_dropped = hand_motors.tx_dropped;
        divider++;
        if (divider == HAND_GUIDE_RECORD_DIVIDER)
        {
            divider = 0u;
            sysid_record((uint32_t)start_us);
        }
        control_heartbeat++;
        rm_task_delay_until(&wake, 1u);
    }
}
