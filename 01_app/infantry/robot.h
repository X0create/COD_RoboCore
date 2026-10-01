/**
 * @file    robot.h
 * @brief   步兵：全部对象和各任务入口的声明（对象定义、启动顺序、任务表都在 robot.c）
 * @note    只给本目录的文件用，相当于老模板里各任务直接读的全局变量（remote_ctrl、Chassis_Motor[] 等）。
 *          其他层不 include 本文件（CODING_STANDARD 第 8 节的例外）。
 *          谁写谁读：
 *            rc_state            dr16（comm_rx 任务）写；control、heartbeat 读
 *            imu_state           ins 任务写；control、heartbeat 读
 *            gate、chassis、wheel_motor 的指令  只在 control 任务里写；heartbeat 只读来打印
 *            battery、buzzer     只在 heartbeat 任务里用
 */
#pragma once

#include "01_app/chassis/chassis.h"
#include "01_app/common/safety_gate.h"
#include "01_app/ins/ins.h"
#include "02_devices/battery/battery.h"
#include "02_devices/buzzer/buzzer.h"
#include "02_devices/motor/motor.h"
#include "02_devices/motor/motor_group.h"
#include "02_devices/remote/dr16.h"
#include "04_core/msg/imu_state.h"
#include "04_core/msg/rc_state.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---------------- 对象（定义在 robot.c） ---------------- */

extern RcStateTopic rc_state;
extern ImuStateTopic imu_state;

extern Dr16 dr16;
extern Motor wheel_motor[CHASSIS_WHEELS]; /* 轮 0–3：左前、左后、右后、右前 */
extern MotorGroup motors;                 /* 全部电机，control 任务最后统一发送 */
extern Battery battery;
extern Buzzer buzzer;

extern SafetyGate gate;
extern Chassis chassis;
extern Ins ins;

/* ---------------- 任务入口（一个任务一个文件；detect 各兵种相同，在 01_app/common/） ---------------- */

/** comm_rx_task.c：收到数据就运行，打开接收，然后把 CAN、串口 的数据交给对应解析器（接线写在这个文件里） */
void comm_rx_task_entry(void *arg);

/** control_task.c：1 kHz，读输入 → 安全门 → 底盘 → 发送 */
void control_task_entry(void *arg);

/** ins_task.c：1 kHz，BMI088 → 零偏标定 → EKF → 发布 imu_state */
void ins_task_entry(void *arg);

/** heartbeat_task.c：25 ms 一步，状态灯、蜂鸣器、电池；每秒 RTT 打印一次 */
void heartbeat_task_entry(void *arg);

/* ---------------- 启动（robot.c） ---------------- */

/**
 * @brief   调度器启动前：初始化日志、全部对象，并创建全部任务
 * @note    由 CubeMX 生成的 freertos.c 在 MX_FREERTOS_Init() 的 USER CODE 区里调用；
 *          返回后由生成的 main() 调用 osKernelStart()
 */
void app_main(void);

/**
 * @brief   启动任务：调度器启动后第一个运行，打开 ADC、蜂鸣器，允许解锁，然后删除自己
 * @note    CubeMX 以最高优先级静态创建它（ADR 0025 修订），生成的是弱定义，robot.c 里是真正的实现
 */
void startup_task(void *argument);

#ifdef __cplusplus
}
#endif
