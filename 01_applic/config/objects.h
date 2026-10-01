/**
 * @file    objects.h
 * @brief   这台车的全部对象（定义和初始化在 objects.c，任务表在 task_table.c，任务入口的声明在 01_applic/tasks/ 各自的 .h）
 * @note    给 01_applic/config/ 和 01_applic/tasks/ 的文件用，相当于老模板里各任务直接读的全局变量
 *          （remote_ctrl、Chassis_Motor[] 等）。其他层不 include 本文件（CODING_STANDARD 第 8 节的例外）。
 *          谁写谁读：
 *            dr16（遥控）        comm_rx_task 写；control、log 用 dr16_read() 读
 *            ins（姿态）         ins_task 写；control、log 用 ins_read() 读
 *            wheel_motor 的反馈  comm_rx_task 写；chassis、log 用 motor_read_feedback() 读
 *            chassis、wheel_motor 的指令  只在 control_task 里写；log 只读来打印
 */
#pragma once

#include "01_applic/modules/chassis/chassis.h"
#include "01_applic/modules/ins/ins.h"
#include "01_applic/system/safety_gate.h"
#include "02_devices/motor/motor.h"
#include "02_devices/motor/motor_group.h"
#include "02_devices/remote/dr16.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---------------- 对象（定义在 objects.c） ---------------- */

extern Dr16 dr16;
extern Motor wheel_motor[CHASSIS_WHEELS]; /* 轮 0–3：左前、左后、右后、右前 */
extern MotorGroup motors;                 /* 全部电机，control_task 最后统一发送 */

extern Chassis chassis;
extern Ins ins;

#ifdef __cplusplus
}
#endif
