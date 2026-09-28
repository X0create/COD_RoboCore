/**
 * @file    safety_gate.c
 * @brief   安全门，见 safety_gate.h
 */
#include "safety_gate.h"

#include <stddef.h>

void safety_gate_init(SafetyGate *gate, uint8_t arm_switch)
{
    *gate = (SafetyGate){ .arm_switch = arm_switch, .mode = ROBOT_MODE_INIT };
}

void safety_gate_set_system_ready(SafetyGate *gate)
{
    gate->system_ready = true;
}

SafetyDecision safety_gate_update(SafetyGate *gate, const RcState *rc, bool imu_ready,
                                  uint64_t now_us)
{
    /* IMU 未就绪与遥控丢失同样处理：拨杆位置不可用来解锁 */
    const bool rc_online = rc != NULL && imu_ready;
    const bool stop_position = rc_online && rc->sw[gate->arm_switch] == RC_SW_DOWN;
    SafetyDecision d = { .stop_all = true, .entered_manual = false };

    switch (gate->mode)
    {
        case ROBOT_MODE_INIT:
            if (gate->system_ready)
            {
                gate->mode = ROBOT_MODE_SAFE;
                gate->saw_stop_position = false;
            }
            break;

        case ROBOT_MODE_SAFE:
            if (!rc_online)
            {
                /* 遥控丢失（或 IMU 未就绪）期间的拨杆位置不作数，恢复后重新拨一次 */
                gate->saw_stop_position = false;
            }
            else if (stop_position)
            {
                gate->saw_stop_position = true;
            }
            else if (gate->saw_stop_position)
            {
                gate->mode = ROBOT_MODE_MANUAL; /* 从“下”拨上来：解锁 */
                gate->manual_since_us = now_us;
                d.entered_manual = true;
            }
            break;

        case ROBOT_MODE_MANUAL:
            if (!rc_online || stop_position)
            {
                gate->mode = ROBOT_MODE_SAFE;
                gate->saw_stop_position = stop_position; /* 急停时已在“下”，拨上即可重新解锁 */
            }
            break;
    }

    d.stop_all = gate->mode != ROBOT_MODE_MANUAL;
    return d;
}

float safety_gate_output_scale(const SafetyGate *gate, uint64_t now_us)
{
    const uint64_t ramp_us = (uint64_t)SAFETY_RAMP_MS * 1000u;
    const uint64_t elapsed_us = now_us - gate->manual_since_us;
    if (gate->mode != ROBOT_MODE_MANUAL || elapsed_us >= ramp_us)
    {
        return 1.0f;
    }
    return (float)elapsed_us / (float)ramp_us;
}
