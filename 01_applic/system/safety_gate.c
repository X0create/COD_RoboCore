/**
 * @file    safety_gate.c
 * @brief   安全门，见 safety_gate.h
 */
#include "safety_gate.h"

#include <stddef.h>

SafetyGate safety_gate;

/* 上电从 Init 开始：startup_task 调用 safety_gate_set_system_ready() 之前一直全车停 */
void safety_gate_init(SafetyGate *gate, uint8_t arm_switch)
{
    *gate = (SafetyGate){ .arm_switch = arm_switch, .mode = SAFETY_MODE_INIT };
}

void safety_gate_set_system_ready(SafetyGate *gate)
{
    gate->system_ready = true;
}

SafetyDecision safety_gate_update(SafetyGate *gate, const RcState *rc, uint64_t now_us)
{
    /* 遥控丢失时拨杆位置未知：既不用来解锁，也不算急停 */
    const bool rc_online = rc != NULL;
    const bool stop_position = rc_online && rc->sw[gate->arm_switch] == RC_SW_DOWN;
    SafetyDecision d = { .stop_all = true, .entered_manual = false };

    /*
     * 模式转换（每个周期最多转一次）：
     *   Init   --启动完成-->               Safe
     *   Safe   --先看到“下”、再拨离“下”--> Manual（解锁）
     *   Manual --拨到“下”（急停）-->       Safe（之后必须重新解锁）；遥控丢失仍留在 Manual
     */
    switch (gate->mode)
    {
        case SAFETY_MODE_INIT:
            if (gate->system_ready)
            {
                gate->mode = SAFETY_MODE_SAFE;
                gate->saw_stop_position = false;
            }
            break;

        case SAFETY_MODE_SAFE:
            if (!rc_online)
            {
                /* 遥控丢失期间的拨杆位置不作数，恢复后重新拨一次 */
                gate->saw_stop_position = false;
            }
            else if (stop_position)
            {
                gate->saw_stop_position = true;
            }
            else if (gate->saw_stop_position)
            {
                gate->mode = SAFETY_MODE_MANUAL; /* 从“下”拨上来：解锁 */
                gate->manual_since_us = now_us;
                d.entered_manual = true;
            }
            break;

        case SAFETY_MODE_MANUAL:
            if (stop_position)
            {
                gate->mode = SAFETY_MODE_SAFE;
                gate->saw_stop_position = stop_position; /* 急停时已在“下”，拨上即可重新解锁 */
            }
            break;
    }

    /* 只有 Manual 能动；其他模式（含本周期刚退出 Manual）全车停 */
    d.stop_all = gate->mode != SAFETY_MODE_MANUAL;
    return d;
}

/* 解锁后输出限幅从 0 线性升到 1：避免积压的目标让车在解锁瞬间猛冲 */
float safety_gate_output_scale(const SafetyGate *gate, uint64_t now_us)
{
    const uint64_t ramp_us = (uint64_t)SAFETY_RAMP_MS * 1000u;
    const uint64_t elapsed_us = now_us - gate->manual_since_us;
    if (gate->mode != SAFETY_MODE_MANUAL || elapsed_us >= ramp_us)
    {
        return 1.0f;
    }
    return (float)elapsed_us / (float)ramp_us;
}

const char *safety_gate_mode_name(SafetyMode mode)
{
    switch (mode)
    {
        case SAFETY_MODE_INIT:
            return "init";
        case SAFETY_MODE_SAFE:
            return "safe";
        case SAFETY_MODE_MANUAL:
            return "manual";
    }
    return "?";
}
