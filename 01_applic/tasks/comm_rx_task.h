/**
 * @file    comm_rx_task.h
 * @brief   comm_rx_task（收到数据就运行）：打开接收，把 CAN、串口的数据交给对应解析器（接线写在 comm_rx_task.c）
 */
#pragma once

#include "04_core/os/os.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 任务控制块：中断要用它唤醒任务，所以定义在 comm_rx_task.c；由 robot.c 的任务表创建 */
extern RmTask comm_rx_task;

/** 任务入口，由 robot.c 的任务表创建（栈 512 字） */
void comm_rx_task_entry(void *arg);

#ifdef __cplusplus
}
#endif
