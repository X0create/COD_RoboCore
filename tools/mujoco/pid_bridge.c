/**
 * @file    pid_bridge.c
 * @brief   只负责把 Python 的数值传给现有 PID，见 pid_bridge.h
 */
#include "pid_bridge.h"

#include "03_algorithm/control/pid.h"

static Pid speed_pid;

void pid_bridge_init(float kp, float ki, float kd, float integral_limit, float output_limit_nm)
{
    const PidParam param = { .kp = kp,
                             .ki = ki,
                             .kd = kd,
                             .integral_limit = integral_limit,
                             .output_limit = output_limit_nm };
    pid_init(&speed_pid, PID_POSITION, &param);
}

float pid_bridge_step(float target_rad_s, float measure_rad_s, int enabled)
{
    if (!enabled)
    {
        pid_reset(&speed_pid);
        return 0.0f;
    }
    return pid_step(&speed_pid, target_rad_s, measure_rad_s);
}
