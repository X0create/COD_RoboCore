/**
 * @file    ramp.c
 * @brief   斜坡，见 ramp.h
 */
#include "ramp.h"

float ramp_step(float current, float target, float step)
{
    const float error = target - current;
    if (error > step)
    {
        return current + step;
    }
    if (error < -step)
    {
        return current - step;
    }
    return target;
}
