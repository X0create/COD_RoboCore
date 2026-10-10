/**
 * @file    tasks.h
 * @brief   全部任务的入口（相当于老模板 Application/Task 下各任务的声明），每个任务的实现在同目录的 xxx_task.c
 * @note    任务的名字、优先级、栈都在 01_applic/config/robot.c 的任务表 task_table[]，由 app_main() 逐个创建。
 *          优先级从高到低：ins 6 → comm_rx 5 → control 4 → detect 3 → indicator 2 → log 1。
 */
#pragma once

#include <stdbool.h>

#include "04_core/os/os.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   ins_task（1 kHz）：BMI088 → 加热 → 零偏标定 → EKF → 保存最新姿态
 * @param   arg  这台车的 Ins 对象（Ins *），已经 ins_init()
 */
void ins_task_entry(void *arg);

/** comm_rx_task（收到数据就运行）：打开接收，把 CAN、串口的数据交给对应解析器 */
void comm_rx_task_entry(void *arg);

/** comm_rx_task 的任务控制块：中断要用它唤醒任务，所以定义在 comm_rx_task.c */
extern RmTask comm_rx_task;

/** control_task（1 kHz）：读输入 → 安全门 → 底盘 → 发送 */
void control_task_entry(void *arg);

/** detect_task（100 Hz）：报告设备上线 / 离线，不参与安全判定 */
void detect_task_entry(void *arg);

/** indicator_task（25 ms）：状态灯、蜂鸣器、低电量检查 */
void indicator_task_entry(void *arg);

/** 最近一次测得的电池电压（V），供 log_task 打印；indicator_task 启动前为 0 */
float indicator_battery_v(void);

/** 当前是否低电量（判定规则见 indicator_task.c 的 battery_config） */
bool indicator_battery_low(void);

/** log_task（每 1 s）：通过 RTT 打印这台车的状态，只读不写 */
void log_task_entry(void *arg);

#ifdef __cplusplus
}
#endif
