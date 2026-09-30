/**
 * @file    debug.h
 * @brief   步兵的心跳任务：状态灯、蜂鸣器、低电量提示、每秒通过 RTT 打印一次状态
 * @note    只读控制相关的对象（通过 DebugView 传进来），不参与控制；打印的数值只供观察，
 *          单个 float 读写是原子的，但不同字段可能来自不同周期。
 */
#pragma once

#include <stdint.h>

#include "devices/motor/motor.h"
#include "devices/remote/dr16.h"
#include "msgs/imu_state.h"
#include "msgs/rc_state.h"
#include "safety_gate.h"
#include "subsystems/chassis/chassis.h"

/** 心跳任务要看的对象，全部由 robot.c 持有 */
typedef struct
{
    const SafetyGate *gate;
    const RcStateTopic *rc_state;
    const ImuStateTopic *imu_state;
    const Dr16 *dr16;
    const Chassis *chassis;
    const Motor *wheel; /* CHASSIS_WHEELS 个驱动轮电机 */
} DebugView;

/** 静态创建心跳任务 @pre 调度器启动前调用；view 在整个运行期间有效 */
void debug_create_task(uint32_t priority, const DebugView *view);
