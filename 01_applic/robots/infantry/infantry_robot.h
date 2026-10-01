/**
 * @file    infantry_robot.h
 * @brief   步兵：全部对象和各任务入口的声明（对象定义、初始化、任务表在 infantry_robot.c）
 * @note    只给本目录的文件用，相当于老模板里各任务直接读的全局变量（remote_ctrl、Chassis_Motor[] 等）。
 *          其他层不 include 本文件（CODING_STANDARD 第 8 节的例外）。
 *          谁写谁读：
 *            rc_state            dr16（comm_rx_task）写；control、log 读
 *            imu_state           ins_task 写；control、log 读
 *            safety_gate、chassis、wheel_motor 的指令  只在 control_task 里写；log 只读来打印
 */
#pragma once

#include "01_applic/modules/chassis/chassis.h"
#include "01_applic/modules/ins/ins.h"
#include "01_applic/system/safety_gate.h"
#include "02_devices/motor/motor.h"
#include "02_devices/motor/motor_group.h"
#include "02_devices/remote/dr16.h"
#include "04_core/msg/imu_state.h"
#include "04_core/msg/rc_state.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---------------- 对象（定义在 infantry_robot.c） ---------------- */

extern RcStateTopic rc_state;
extern ImuStateTopic imu_state;

extern Dr16 dr16;
extern Motor wheel_motor[CHASSIS_WHEELS]; /* 轮 0–3：左前、左后、右后、右前 */
extern MotorGroup motors;                 /* 全部电机，control_task 最后统一发送 */

extern Chassis chassis;
extern Ins ins;

/* ---------------- 本目录的任务入口（ins、detect、indicator 各兵种相同，在 01_applic/tasks/） ---------------- */

/** infantry_comm_rx_task.c：收到数据就运行，打开接收，然后把 CAN、串口 的数据交给对应解析器（接线写在这个文件里） */
void comm_rx_task_entry(void *arg);

/** infantry_control_task.c：1 kHz，读输入 → 安全门 → 底盘 → 发送 */
void control_task_entry(void *arg);

/** infantry_log_task.c：每 1 s 通过 RTT 打印一次本兵种的状态 */
void log_task_entry(void *arg);

#ifdef __cplusplus
}
#endif
