#include "01_applic/modules/hand_guide/hand_guide.h"
#include <math.h>
#include <stddef.h>

bool hand_guide_init(HandGuide *self, const HandGuideConfig *cfg)
{
    if (self == NULL || cfg == NULL || !isfinite(cfg->coulomb_nm) || !isfinite(cfg->fraction)
        || !isfinite(cfg->deadband_rad_s) || !isfinite(cfg->full_assist_rad_s)
        || !isfinite(cfg->arm_speed_rad_s) || !isfinite(cfg->speed_limit_rad_s)
        || !isfinite(cfg->min_temperature_c) || !isfinite(cfg->max_temperature_c)
        || !isfinite(cfg->max_period_s) || !isfinite(cfg->enable_timeout_s)
        || cfg->coulomb_nm <= 0.0f || cfg->fraction <= 0.0f || cfg->fraction >= 1.0f
        || cfg->deadband_rad_s <= 0.0f || cfg->full_assist_rad_s <= cfg->deadband_rad_s
        || cfg->arm_speed_rad_s <= 0.0f || cfg->arm_speed_rad_s > cfg->full_assist_rad_s
        || cfg->speed_limit_rad_s <= cfg->full_assist_rad_s
        || cfg->min_temperature_c >= cfg->max_temperature_c || cfg->max_period_s <= 0.0f
        || cfg->enable_timeout_s <= cfg->max_period_s)
    {
        return false;
    }
    *self = (HandGuide){ .cfg = cfg };
    return true;
}

static void trip(HandGuide *self, HandGuideFault fault)
{
    if (self->fault == HAND_GUIDE_OK)
    {
        self->fault = fault;
    }
    self->mode = HAND_GUIDE_OFF;
    self->armed = false;
    self->seen_enabled = false;
    self->waiting_s = 0.0f;
    self->torque_nm = 0.0f;
}

void hand_guide_update(HandGuide *self, const MotorFeedback *fb, bool rc_online, RcSwitch sw,
                       float dt_s, bool tx_failed)
{
    const HandGuideConfig *c = self->cfg;
    self->request_enable = false;
    self->torque_nm = 0.0f;
    HandGuideFault fault = HAND_GUIDE_OK;
    if (!rc_online)
    {
        fault = HAND_GUIDE_RC_LOST;
    }
    else if (!fb->online)
    {
        fault = HAND_GUIDE_MOTOR_LOST;
    }
    else if (fb->error_code != 0u)
    {
        fault = HAND_GUIDE_DRIVER_ERROR;
    }
    else if (fabsf(fb->speed_rad_s) >= c->speed_limit_rad_s)
    {
        fault = HAND_GUIDE_OVERSPEED;
    }
    else if (fb->temperature_c < c->min_temperature_c || fb->temperature_c > c->max_temperature_c)
    {
        fault = HAND_GUIDE_TEMPERATURE;
    }
    else if (dt_s <= 0.0f || dt_s > c->max_period_s)
    {
        fault = HAND_GUIDE_LATE;
    }
    else if (tx_failed)
    {
        fault = HAND_GUIDE_TX_FAILED;
    }
    if (fault != HAND_GUIDE_OK)
    {
        trip(self, fault);
        return;
    }

    if (sw == RC_SW_DOWN)
    {
        self->mode = HAND_GUIDE_OFF;
        self->seen_enabled = false;
        self->waiting_s = 0.0f;
        self->armed = fabsf(fb->speed_rad_s) <= c->arm_speed_rad_s;
        if (self->armed)
        {
            self->fault = HAND_GUIDE_OK;
        }
        return;
    }
    if (!self->armed || self->fault != HAND_GUIDE_OK)
    {
        return;
    }
    if (self->mode == HAND_GUIDE_OFF)
    {
        /* 不能从“下”直接进入助力。先静止进入零力矩基线。 */
        if (sw != RC_SW_MID || fabsf(fb->speed_rad_s) > c->arm_speed_rad_s)
        {
            trip(self, HAND_GUIDE_ARM_MOVING);
            return;
        }
        self->mode = HAND_GUIDE_BASELINE;
        self->request_enable = true;
    }
    else if (sw == RC_SW_UP && self->mode == HAND_GUIDE_BASELINE)
    {
        if (!fb->enabled || fabsf(fb->speed_rad_s) > c->arm_speed_rad_s)
        {
            trip(self, HAND_GUIDE_ARM_MOVING);
            return;
        }
        self->mode = HAND_GUIDE_ASSIST;
    }
    else if (sw == RC_SW_MID)
    {
        self->mode = HAND_GUIDE_BASELINE;
    }

    if (!fb->enabled)
    {
        if (self->seen_enabled)
        {
            trip(self, HAND_GUIDE_DISABLED);
            return;
        }
        self->waiting_s += dt_s;
        if (self->waiting_s >= c->enable_timeout_s)
        {
            trip(self, HAND_GUIDE_ENABLE_FAILED);
        }
        return;
    }
    self->seen_enabled = true;
    self->waiting_s = 0.0f;
    if (self->mode != HAND_GUIDE_ASSIST)
    {
        return;
    }
    const float speed = fabsf(fb->speed_rad_s);
    if (speed <= c->deadband_rad_s)
    {
        return;
    }
    float blend = (speed - c->deadband_rad_s) / (c->full_assist_rad_s - c->deadband_rad_s);
    if (blend > 1.0f)
    {
        blend = 1.0f;
    }
    /* 无滤波器/积分记忆：换向、停下时不会残留上一方向的助力。 */
    self->torque_nm = copysignf(c->fraction * c->coulomb_nm * blend, fb->speed_rad_s);
}

void hand_guide_apply(const HandGuide *self, Motor *motor)
{
    if (self->mode == HAND_GUIDE_OFF)
    {
        motor_request_disable(motor);
        motor_apply_stop_action(motor, STOP_ACTION_DISABLE);
    }
    else
    {
        if (self->request_enable)
        {
            motor_request_enable(motor);
        }
        motor_set_torque(motor, self->torque_nm);
    }
}
