/**
 * @file    indicator_task.h
 * @brief   indicator_task（25 ms）：状态灯、蜂鸣器、低电量检查，各兵种共用（照 COD_UniCFramework 的 app_indicator）
 * @note    状态灯和蜂鸣器只有本任务操作，ADC、蜂鸣器 PWM 也在本任务开头打开。
 *          提示依据的状态直接读权威来源，不另存副本：模式读 safety_gate.mode，电量由本任务读 ADC 判断。
 *          提示：绿灯每秒闪两下（一长一短）= 正常；启动音；解锁音 / 上锁音；低电量每 2 s 响一次。
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** 任务入口，由兵种 <兵种>_robot.c 的任务表创建（栈 256 字够用） */
void indicator_task_entry(void *arg);

/** 最近一次测得的电池电压（V），供 log_task 打印；本任务启动前为 0 */
float indicator_battery_v(void);

/** 当前是否低电量（判定规则见 indicator_task.c 的 battery_config） */
bool indicator_battery_low(void);

#ifdef __cplusplus
}
#endif
