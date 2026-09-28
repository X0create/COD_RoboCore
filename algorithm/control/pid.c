/**
 * @file    pid.c
 * @brief   位置式 / 增量式 PID，见 pid.h
 */
#include "pid.h"

#include <math.h>

static float clamp(float x, float limit)
{
    if (x > limit)
    {
        return limit;
    }
    if (x < -limit)
    {
        return -limit;
    }
    return x;
}

static bool d_filter_enabled(const PidParam *param)
{
    return param->d_alpha > 0.0f && param->d_alpha < 1.0f;
}

void pid_init(Pid *pid, PidType type, const PidParam *param)
{
    pid->type = type;
    pid->param = *param;
    pid_reset(pid);
}

void pid_reset(Pid *pid)
{
    pid->target = 0.0f;
    pid->measure = 0.0f;
    pid->err[0] = 0.0f;
    pid->err[1] = 0.0f;
    pid->err[2] = 0.0f;
    pid->integral = 0.0f;
    pid->p_out = 0.0f;
    pid->i_out = 0.0f;
    pid->d_out = 0.0f;
    pid->output = 0.0f;
    lpf1_init(&pid->d_lpf, pid->param.d_alpha);
}

float pid_calc(Pid *pid, float target, float measure)
{
    const PidParam *p = &pid->param;

    pid->target = target;
    pid->measure = measure;
    pid->err[2] = pid->err[1];
    pid->err[1] = pid->err[0];
    pid->err[0] = target - measure;

    if (fabsf(pid->err[0]) < p->deadband)
    {
        return pid->output;
    }

    if (pid->type == PID_POSITION)
    {
        pid->integral =
            (p->ki != 0.0f) ? clamp(pid->integral + pid->err[0], p->integral_limit) : 0.0f;
        pid->p_out = p->kp * pid->err[0];
        pid->i_out = p->ki * pid->integral;
        pid->d_out = p->kd * (pid->err[0] - pid->err[1]);
    }
    else
    {
        pid->p_out = p->kp * (pid->err[0] - pid->err[1]);
        pid->i_out = p->ki * pid->err[0];
        pid->d_out = p->kd * (pid->err[0] - 2.0f * pid->err[1] + pid->err[2]);
    }

    if (d_filter_enabled(p))
    {
        pid->d_out = lpf1_update(&pid->d_lpf, pid->d_out);
    }

    const float sum = pid->p_out + pid->i_out + pid->d_out;
    pid->output = clamp((pid->type == PID_POSITION) ? sum : pid->output + sum, p->output_limit);
    return pid->output;
}
