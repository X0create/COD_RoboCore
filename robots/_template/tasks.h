/**
 * @file    tasks.h
 * @brief   样板各任务的入口函数。一个任务一个文件，全部在 robot.c 的 robot_create_tasks() 里创建
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/** ins_task.c：1 kHz，BMI088 → 零偏标定 → EKF → 发布 imu_state */
void ins_task_entry(void *arg);

/** control_task.c：1 kHz，读输入 → 安全门 → 速度环 → 发送 */
void control_task_entry(void *arg);

/** heartbeat_task.c：25 ms 一步，状态灯、蜂鸣器、电池；每秒 RTT 打印一次 */
void heartbeat_task_entry(void *arg);

#ifdef __cplusplus
}
#endif
