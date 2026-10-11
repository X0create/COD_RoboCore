/**
 * @file    pid.c
 * @brief   位置式 / 增量式 PID，见 pid.h
 */
#include "pid.h"

#include <math.h>

/* 把 x 限制在 [-limit, +limit] */
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

/* d_alpha 在 (0, 1) 之间才对微分项做一阶低通；0 表示不滤波 */
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

/* 清掉历史误差、积分和输出（解锁、停机时调用，避免带着旧积分重新开始） */
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

float pid_step(Pid *pid, float target, float measure)
{
    const PidParam *p = &pid->param;

    /* 误差历史：err[0] 本次、err[1] 上次、err[2] 上上次（增量式要用到三次） */
    pid->target = target;
    pid->measure = measure;
    pid->err[2] = pid->err[1];
    pid->err[1] = pid->err[0];
    pid->err[0] = target - measure;

    if (fabsf(pid->err[0]) < p->deadband) /* 误差在死区内：保持上次输出不变 */
    {
        return pid->output;
    }

    /* 位置式：输出 = Kp·e + Ki·Σe + Kd·(e − e上次)，积分带限幅和抗饱和 */
    if (pid->type == PID_POSITION)
    {
        pid->p_out = p->kp * pid->err[0];
        pid->d_out = p->kd * (pid->err[0] - pid->err[1]);
        if (d_filter_enabled(p))
        {
            pid->d_out = lpf1_update(&pid->d_lpf, pid->d_out);
        }
        const float integral =
            (p->ki != 0.0f) ? clamp(pid->integral + pid->err[0], p->integral_limit) : 0.0f;
        /* 抗积分饱和：累加后输出会越限、且误差与输出同向时，积分只加到输出刚好等于限幅为止 */
        const float sum = pid->p_out + p->ki * integral + pid->d_out;
        const bool winding = fabsf(sum) > p->output_limit && (sum > 0.0f) == (pid->err[0] > 0.0f);
        if (!winding || p->ki == 0.0f)
        {
            pid->integral = integral;
        }
        else
        {
            const float edge = (sum > 0.0f) ? p->output_limit : -p->output_limit;
            const float room = (edge - pid->p_out - pid->d_out) / p->ki;
            /* 只沿误差方向补到边界；P、D 已经超限时积分保持不动 */
            if ((pid->err[0] > 0.0f) ? (room > pid->integral) : (room < pid->integral))
            {
                pid->integral = room;
            }
        }
        pid->i_out = p->ki * pid->integral;
        pid->output = clamp(pid->p_out + pid->i_out + pid->d_out, p->output_limit);
        return pid->output;
    }

    /* 增量式：每次只算输出的变化量 Δu = Kp·Δe + Ki·e + Kd·Δ²e，累加到上次输出上 */
    pid->p_out = p->kp * (pid->err[0] - pid->err[1]);
    pid->i_out = p->ki * pid->err[0];
    pid->d_out = p->kd * (pid->err[0] - 2.0f * pid->err[1] + pid->err[2]);
    if (d_filter_enabled(p))
    {
        pid->d_out = lpf1_update(&pid->d_lpf, pid->d_out);
    }
    pid->output = clamp(pid->output + pid->p_out + pid->i_out + pid->d_out, p->output_limit);
    return pid->output;
}
