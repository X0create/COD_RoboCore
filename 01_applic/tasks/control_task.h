/**
 * @file    control_task.h
 * @brief   control_task（1 kHz）：读输入 → 安全门 → 底盘 → 发送（步骤写在 control_task.c 的循环里）
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/** 任务入口，由 task_table.c 创建（栈 1024 字） */
void control_task_entry(void *arg);

#ifdef __cplusplus
}
#endif
