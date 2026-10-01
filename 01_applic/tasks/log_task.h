/**
 * @file    log_task.h
 * @brief   log_task（每 1 s）：通过 RTT 打印这台车的状态，只读不写
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/** 任务入口，由 task_table.c 创建（栈 256 字） */
void log_task_entry(void *arg);

#ifdef __cplusplus
}
#endif
