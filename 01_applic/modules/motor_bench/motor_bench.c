#include "01_applic/modules/motor_bench/motor_bench.h"
#include <math.h>

static float limit(float value, float bound)
{
    return fmaxf(-bound, fminf(bound, value));
}

bool motor_bench_init(MotorBench *self, const MotorBenchConfig *c)
{
    if (!isfinite(c->inertia_kg_m2 + c->fc_pos_nm + c->fc_neg_nm + c->b_pos + c->b_neg
                  + c->torque_limit_nm + c->low_rad_s + c->high_rad_s + c->accel_rad_s2
                  + c->settle_s + c->hold_s + c->moving_min_rad_s + c->arm_speed_rad_s
                  + c->speed_limit_rad_s + c->start_temperature_c + c->stop_temperature_c
                  + c->max_period_s + c->max_exec_s + c->max_tracking_error_rad_s)
        || !(c->inertia_kg_m2 > 0.0f && c->fc_pos_nm >= 0.0f && c->fc_neg_nm >= 0.0f
             && c->b_pos > 0.0f && c->b_neg > 0.0f && c->torque_limit_nm > 0.0f
             && c->low_rad_s > 0.0f && c->high_rad_s > c->low_rad_s && c->accel_rad_s2 > 0.0f
             && c->settle_s > 0.0f && c->hold_s > 0.0f && c->moving_min_rad_s > 0.0f
             && c->moving_min_rad_s < c->low_rad_s && c->arm_speed_rad_s >= 0.0f
             && c->speed_limit_rad_s > c->high_rad_s && c->arm_speed_rad_s < c->low_rad_s
             && c->stop_temperature_c > c->start_temperature_c && c->max_period_s > 0.0f
             && c->max_exec_s > 0.0f && c->max_tracking_error_rad_s > 0.0f))
    {
        return false;
    }
    for (unsigned i = 0u; i < 3u; ++i)
    {
        if (!(isfinite(c->tc_s[i]) && c->tc_s[i] > 0.0f))
        {
            return false;
        }
    }
    *self = (MotorBench){ .bounds = c, .config = *c, .settings = { .direction = 1, .gain = 1u } };
    self->cfg = &self->config;
    return true;
}

MotorBenchParameters motor_bench_parameters(const MotorBench *self)
{
    const MotorBenchConfig *c = self->cfg;
    return (MotorBenchParameters){ c->torque_limit_nm,   c->low_rad_s,
                                   c->high_rad_s,        c->accel_rad_s2,
                                   c->speed_limit_rad_s, c->start_temperature_c,
                                   c->stop_temperature_c };
}

bool motor_bench_configure(MotorBench *self, const MotorBenchParameters *p,
                           const MotorBenchSettings *s, const MotorBenchInput *in)
{
    const MotorBenchConfig *b = self->bounds;
    if (self->mode == MOTOR_BENCH_RUNNING
        || (!s->usb_control && (!in->rc_online || in->sw != RC_SW_DOWN)) || !in->feedback.online
        || !isfinite(in->feedback.speed_rad_s)
        || fabsf(in->feedback.speed_rad_s) > b->arm_speed_rad_s)
    {
        return false;
    }
    if (!isfinite(p->torque_limit_nm + p->low_rad_s + p->high_rad_s + p->accel_rad_s2
                  + p->speed_limit_rad_s + p->start_temperature_c + p->stop_temperature_c)
        || !(p->torque_limit_nm > 0.0f && p->torque_limit_nm <= b->torque_limit_nm
             && p->low_rad_s >= b->low_rad_s && p->high_rad_s <= b->high_rad_s
             && p->low_rad_s < p->high_rad_s && p->accel_rad_s2 > 0.0f
             && p->accel_rad_s2 <= b->accel_rad_s2 && p->speed_limit_rad_s > p->high_rad_s
             && p->speed_limit_rad_s <= b->speed_limit_rad_s && p->start_temperature_c >= 0.0f
             && p->start_temperature_c <= b->start_temperature_c
             && p->stop_temperature_c > p->start_temperature_c
             && p->stop_temperature_c <= b->stop_temperature_c)
        || (s->direction != 1 && s->direction != -1) || s->gain >= 3u)
    {
        return false;
    }
    self->config.torque_limit_nm = p->torque_limit_nm;
    self->config.low_rad_s = p->low_rad_s;
    self->config.high_rad_s = p->high_rad_s;
    self->config.accel_rad_s2 = p->accel_rad_s2;
    self->config.speed_limit_rad_s = p->speed_limit_rad_s;
    self->config.start_temperature_c = p->start_temperature_c;
    self->config.stop_temperature_c = p->stop_temperature_c;
    self->settings = *s;
    self->mode = s->usb_control && s->allow_motion ? MOTOR_BENCH_READY : MOTOR_BENCH_LOCKED;
    self->fault = MOTOR_BENCH_OK;
    self->down_seen = false;
    return true;
}

static void zero_output(MotorBench *self)
{
    self->integral_nm = self->pi_nm = self->friction_nm = self->inertia_nm = 0.0f;
    self->requested_nm = self->torque_nm = 0.0f;
    self->reference_rad_s = self->acceleration_rad_s2 = 0.0f;
    self->ff_active = self->saturated = false;
}

static void fail(MotorBench *self, MotorBenchFault fault)
{
    self->mode = MOTOR_BENCH_FAULT;
    self->fault = fault;
    self->down_seen = false;
    if (self->settings.usb_control)
    {
        self->settings.allow_motion = false;
    }
    zero_output(self);
}

static MotorBenchFault input_fault(const MotorBench *self, const MotorBenchInput *in)
{
    const MotorFeedback *f = &in->feedback;
    const MotorBenchConfig *c = self->cfg;
    if (in->settings.usb_control && !in->usb_online)
    {
        return MOTOR_BENCH_USB_LOST;
    }
    if (!in->settings.usb_control && !in->rc_online)
    {
        return MOTOR_BENCH_RC_LOST;
    }
    if (!f->online)
    {
        return MOTOR_BENCH_MOTOR_LOST;
    }
    if (!isfinite(in->dt_s + in->previous_exec_s + f->speed_rad_s + f->temperature_c)
        || in->dt_s <= 0.0f || in->previous_exec_s < 0.0f
        || (!in->settings.usb_control && in->sw != RC_SW_DOWN && in->sw != RC_SW_MID
            && in->sw != RC_SW_UP))
    {
        return MOTOR_BENCH_BAD_INPUT;
    }
    if (f->error_code != 0u)
    {
        return MOTOR_BENCH_DRIVER_ERROR;
    }
    if (fabsf(f->speed_rad_s) > c->speed_limit_rad_s)
    {
        return MOTOR_BENCH_OVERSPEED;
    }
    if (f->temperature_c >= c->stop_temperature_c
        || (self->mode != MOTOR_BENCH_RUNNING && f->temperature_c > c->start_temperature_c))
    {
        return MOTOR_BENCH_TEMPERATURE;
    }
    if (in->dt_s > c->max_period_s || in->previous_exec_s > c->max_exec_s)
    {
        return MOTOR_BENCH_LATE;
    }
    if (in->tx_failed)
    {
        return MOTOR_BENCH_TX_FAILED;
    }
    return MOTOR_BENCH_OK;
}

/* 梯形参考：起步 / 稳定 → 低到高 / 保持 → 高到低 / 保持 → 停止。 */
static bool reference(MotorBench *self)
{
    const MotorBenchConfig *c = self->cfg;
    const float ramp = c->low_rad_s / c->accel_rad_s2;
    const float change = (c->high_rad_s - c->low_rad_s) / c->accel_rad_s2;
    const float durations[7] = { ramp, c->settle_s, change, c->hold_s, change, c->hold_s, ramp };
    float t = self->elapsed_s;
    unsigned phase = 0u;
    while (phase < 7u && t >= durations[phase])
    {
        t -= durations[phase++];
    }
    self->phase = phase;
    float w = 0.0f, a = 0.0f;
    switch (phase)
    {
        case 0u:
            w = c->accel_rad_s2 * t;
            a = c->accel_rad_s2;
            break;
        case 1u:
            w = c->low_rad_s;
            break;
        case 2u:
            w = c->low_rad_s + c->accel_rad_s2 * t;
            a = c->accel_rad_s2;
            break;
        case 3u:
            w = c->high_rad_s;
            break;
        case 4u:
            w = c->high_rad_s - c->accel_rad_s2 * t;
            a = -c->accel_rad_s2;
            break;
        case 5u:
            w = c->low_rad_s;
            break;
        case 6u:
            w = c->low_rad_s - c->accel_rad_s2 * t;
            a = -c->accel_rad_s2;
            break;
        default:
            return false;
    }
    const float direction = (float)self->active.direction;
    self->reference_rad_s = direction * w;
    self->acceleration_rad_s2 = direction * a;
    return true;
}

static void control(MotorBench *self, const MotorBenchInput *in)
{
    const MotorBenchConfig *c = self->cfg;
    const bool use_ff = self->active.feedforward && self->phase >= 2u && self->phase <= 5u;
    const bool positive = self->active.direction > 0;
    const float fc = positive ? c->fc_pos_nm : c->fc_neg_nm;
    const float b = positive ? c->b_pos : c->b_neg;
    const float low_friction = (float)self->active.direction * (fc + b * c->low_rad_s);
    /* 起步 / 停止只用 PI；进入 / 离开运动模型区间时转移恒速摩擦份额，保持总出力连续。
     * 加速度项仍按规划加速度变化；不是每拍把前馈变化抵消掉。 */
    if (use_ff != self->ff_active)
    {
        self->integral_nm =
            limit(self->integral_nm + (use_ff ? -low_friction : low_friction), c->torque_limit_nm);
    }
    self->ff_active = use_ff;
    self->friction_nm =
        use_ff ? (float)self->active.direction * fc + b * self->reference_rad_s : 0.0f;
    self->inertia_nm = use_ff ? c->inertia_kg_m2 * self->acceleration_rad_s2 : 0.0f;
    const float ff = self->friction_nm + self->inertia_nm;
    const float error = self->reference_rad_s - in->feedback.speed_rad_s;
    const float p = self->kp * error;
    const float integral = self->integral_nm + self->ki * error * in->dt_s;
    const float trial = p + integral + ff;
    const bool winding = (trial > c->torque_limit_nm && error > 0.0f)
                         || (trial < -c->torque_limit_nm && error < 0.0f);
    if (!winding)
    {
        self->integral_nm = limit(integral, c->torque_limit_nm);
    }
    self->pi_nm = p + self->integral_nm;
    self->requested_nm = self->pi_nm + ff;
    self->torque_nm = limit(self->requested_nm, c->torque_limit_nm);
    self->saturated = winding || fabsf(integral) > c->torque_limit_nm
                      || fabsf(self->requested_nm) > c->torque_limit_nm;
}

void motor_bench_update(MotorBench *self, const MotorBenchInput *in)
{
    if (!in->settings.allow_motion)
    {
        self->mode = MOTOR_BENCH_LOCKED;
        self->down_seen = false;
        zero_output(self);
        return;
    }
    const MotorBenchFault fault = input_fault(self, in);
    if (fault != MOTOR_BENCH_OK)
    {
        fail(self, fault);
        return;
    }
    if (in->settings.usb_control && self->mode != MOTOR_BENCH_RUNNING
        && !(self->mode == MOTOR_BENCH_READY && in->usb_start))
    {
        zero_output(self);
        return;
    }
    if (!in->settings.usb_control && in->sw == RC_SW_DOWN)
    {
        self->mode = MOTOR_BENCH_LOCKED;
        self->fault = MOTOR_BENCH_OK;
        self->down_seen = fabsf(in->feedback.speed_rad_s) <= self->cfg->arm_speed_rad_s;
        zero_output(self);
        return;
    }
    if (self->mode == MOTOR_BENCH_FAULT || self->mode == MOTOR_BENCH_DONE)
    {
        return;
    }
    if (!in->settings.usb_control && in->sw == RC_SW_MID)
    {
        if (self->mode == MOTOR_BENCH_RUNNING)
        {
            self->down_seen = false;
        }
        self->mode = self->down_seen ? MOTOR_BENCH_READY : MOTOR_BENCH_LOCKED;
        zero_output(self);
        return;
    }
    if (self->mode == MOTOR_BENCH_READY)
    {
        if (fabsf(in->feedback.speed_rad_s) > self->cfg->arm_speed_rad_s)
        {
            self->mode = MOTOR_BENCH_LOCKED;
            self->down_seen = false;
            return;
        }
        if ((in->settings.direction != 1 && in->settings.direction != -1)
            || in->settings.gain >= 3u)
        {
            fail(self, MOTOR_BENCH_BAD_SETTINGS);
            return;
        }
        self->active = in->settings;
        const float tc = self->cfg->tc_s[self->active.gain];
        self->kp = self->cfg->inertia_kg_m2 / tc;
        self->ki = 0.5f * (self->cfg->b_pos + self->cfg->b_neg) / tc;
        self->mode = MOTOR_BENCH_RUNNING;
        self->down_seen = false;
        self->elapsed_s = 0.0f;
    }
    if (self->mode != MOTOR_BENCH_RUNNING)
    {
        return;
    }
    if (!reference(self))
    {
        self->mode = MOTOR_BENCH_DONE;
        if (in->settings.usb_control)
        {
            self->settings.allow_motion = false;
        }
        zero_output(self);
        return;
    }
    /* 起步尚未验证静摩擦，独立记录；稳定期结束后才评价运动跟踪。 */
    if (self->phase >= 2u
        && fabsf(self->reference_rad_s - in->feedback.speed_rad_s)
               > self->cfg->max_tracking_error_rad_s)
    {
        fail(self, MOTOR_BENCH_TRACKING);
        return;
    }
    if (self->phase >= 2u && self->phase <= 5u
        && fabsf(in->feedback.speed_rad_s) < self->cfg->moving_min_rad_s)
    {
        fail(self, MOTOR_BENCH_TRACKING);
        return;
    }
    control(self, in);
    self->elapsed_s += in->dt_s;
}

void motor_bench_apply(const MotorBench *self, Motor *motor)
{
    if (self->mode == MOTOR_BENCH_RUNNING)
    {
        motor_set_torque(motor, self->torque_nm);
    }
    else
    {
        motor_apply_stop_action(motor, STOP_ACTION_ZERO_TORQUE);
    }
}
