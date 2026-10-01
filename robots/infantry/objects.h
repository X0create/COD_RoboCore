/**
 * @file    objects.h
 * @brief   步兵的全部对象（话题、设备、子系统、安全门），定义在 robot.c
 * @note    只给本目录的任务文件（control_task.c、ins_task.c、heartbeat_task.c）用，相当于老模板里各任务
 *          直接读的全局变量（remote_ctrl、Chassis_Motor[] 等）。其他层不 include 本文件（CODING_STANDARD 第 8 节的例外）。
 *          谁写谁读：
 *            rc_state   dr16（comm_rx 任务）写；control、heartbeat 读
 *            imu_state  ins 任务写；control、heartbeat 读
 *            gate、chassis、wheel_motor 的指令  只在 control 任务里写；heartbeat 只读来打印
 */
#pragma once

#include "devices/motor/motor.h"
#include "devices/motor/motor_group.h"
#include "devices/remote/dr16.h"
#include "msgs/imu_state.h"
#include "msgs/rc_state.h"
#include "safety_gate.h"
#include "subsystems/chassis/chassis.h"
#include "subsystems/ins/ins.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern RcStateTopic rc_state;
extern ImuStateTopic imu_state;

extern Dr16 dr16;
extern Motor wheel_motor[CHASSIS_WHEELS]; /* 轮 0–3：左前、左后、右后、右前 */
extern MotorGroup motors;                 /* 全部电机，control 任务最后统一发送 */

extern SafetyGate gate;
extern Chassis chassis;
extern Ins ins;

#ifdef __cplusplus
}
#endif
