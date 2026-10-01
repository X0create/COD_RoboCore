/**
 * @file    ins_task.h
 * @brief   ins_task（1 kHz）：BMI088 → 加热 → 零偏标定 → EKF → 保存最新姿态，通用
 * @note    相当于老模板的 INS_Task.c；一个周期的具体步骤在 ins.c 的 ins_step()。
 *          标定完成前 ins_read() 返回 false，安全门据此全车停。本任务把 ins_step() 返回的事件记进日志。
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   任务入口，由 task_table.c 创建（优先级最高，栈 1024 字：EKF 的矩阵运算在栈上有临时变量）
 * @param   arg  这台车的 Ins 对象（Ins *），已经 ins_init()
 */
void ins_task_entry(void *arg);

#ifdef __cplusplus
}
#endif
