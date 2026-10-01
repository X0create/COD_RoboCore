/**
 * @file    bench_robot.h
 * @brief   台架验证固件：全部对象和各任务入口的声明（对象定义、初始化、任务表在 bench_robot.c）
 * @note    用来在台架上逐项验证设备和链路（docs/VERIFICATION_TODO.md），不是一台真车：
 *          一台 M3508 速度环、一台达妙 DM8009、DR16、图传链路、USB 视觉链路。做新兵种时复制 01_applic/infantry/。
 *          只给本目录的文件用，相当于老模板里各任务直接读的全局变量。其他层不 include 本文件（CODING_STANDARD 第 8 节的例外）。
 *          谁写谁读：
 *            rc_state、vt_rc_state、kbm_state  dr16 / vt_link（comm_rx_task）写；control、log 读
 *            imu_state                         ins_task 写；control、log 读
 *            safety_gate、电机指令                     只在 control_task 里写；log 只读来打印
 *            usb_rx_bytes、usb_echo_dropped    comm_rx_task 累加、log 读（32 位读写是原子的，只用于调试统计）
 */
#pragma once

#include <stdint.h>

#include "01_applic/ins/ins.h"
#include "01_applic/system/safety_gate.h"
#include "02_devices/motor/motor.h"
#include "02_devices/motor/motor_group.h"
#include "02_devices/remote/dr16.h"
#include "02_devices/remote/vt_link.h"
#include "02_devices/vision/vision_link.h"
#include "04_core/msg/imu_state.h"
#include "04_core/msg/kbm_state.h"
#include "04_core/msg/rc_state.h"
#include "04_core/msg/vt_rc_state.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---------------- 对象（定义在 bench_robot.c） ---------------- */

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
extern MotorGroup motors;   /* 全部电机，control_task 最后统一发送 */

extern Ins ins;

/* ---------------- 本目录的任务入口（ins、detect、indicator 各兵种相同，在 01_applic/ins/、01_applic/system/） ---------------- */

/** bench_comm_rx_task.c：收到数据就运行，打开接收，然后把 CAN、串口、USB 的数据交给对应解析器（接线写在这个文件里） */
void comm_rx_task_entry(void *arg);

/** bench_control_task.c：1 kHz，读输入 → 安全门 → 速度环 → 发送 */
void control_task_entry(void *arg);

/** bench_log_task.c：每 1 s 通过 RTT 打印一次本兵种的状态 */
void log_task_entry(void *arg);

#ifdef __cplusplus
}
#endif
