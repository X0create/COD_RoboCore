/**
 * @file    ramp.c
 * @brief   斜坡，见 ramp.h
 */
#include "ramp.h"

/* 每次最多向目标走 step；离目标不到一步就直接到目标 */
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
