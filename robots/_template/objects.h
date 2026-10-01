/**
 * @file    objects.h
 * @brief   样板的全部对象（话题、设备、子系统、安全门），定义在 robot.c
 * @note    只给本目录的任务文件（control_task.c、ins_task.c、heartbeat_task.c）用，相当于老模板里各任务
 *          直接读的全局变量（remote_ctrl、Chassis_Motor[] 等）。其他层不 include 本文件（CODING_STANDARD 第 8 节的例外）。
 *          谁写谁读：
 *            rc_state、vt_rc_state、kbm_state  dr16 / vt_link（comm_rx 任务）写；control、heartbeat 读
 *            imu_state                         ins 任务写；control、heartbeat 读
 *            gate、电机指令                     只在 control 任务里写；heartbeat 只读来打印
 *            usb_rx_bytes、usb_echo_dropped    comm_rx 任务累加、heartbeat 读（32 位读写是原子的，只用于调试统计）
 */
#pragma once

#include <stdint.h>

#include "devices/motor/motor.h"
#include "devices/motor/motor_group.h"
#include "devices/remote/dr16.h"
#include "devices/remote/vt_link.h"
#include "devices/vision/vision_link.h"
#include "msgs/imu_state.h"
#include "msgs/kbm_state.h"
#include "msgs/rc_state.h"
#include "msgs/vt_rc_state.h"
#include "safety_gate.h"
#include "subsystems/ins/ins.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern RcStateTopic rc_state;
extern ImuStateTopic imu_state;
extern VtRcStateTopic vt_rc_state;
extern KbmStateTopic kbm_state;

extern Dr16 dr16;
extern VtLink vt_link;
extern VisionLink vision_link;
extern volatile uint32_t usb_rx_bytes;
extern volatile uint32_t usb_echo_dropped;

extern Motor chassis_motor; /* M3508，FDCAN1 ID 1 */
extern Motor joint_motor;   /* 达妙 DM8009，FDCAN2 */
extern MotorGroup motors;   /* 全部电机，control 任务最后统一发送 */

extern SafetyGate gate;
extern Ins ins;

#ifdef __cplusplus
}
#endif
