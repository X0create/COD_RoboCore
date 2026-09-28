/**
 * @file    comm_rx.h
 * @brief   comm_rx 任务：收到通知后，在任务里把各路 CAN 收下的帧分发给订阅者（《架构设计》任务划分）
 * @note    中断只负责收帧和通知（comm_rx_notify_from_isr），订阅者的回调都在本任务里执行。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   静态创建 comm_rx 任务
 * @param   priority  FreeRTOS 优先级，由兵种的 robot_create_tasks() 统一给出（应高于 control 以外的任务）
 * @pre     调度器启动前、can_start() 之前调用
 */
void comm_rx_create_task(uint32_t priority);

/** 作为 can_start() / uart_rx_start() 的通知回调：在中断里唤醒 comm_rx 任务。ctx 未使用 */
void comm_rx_notify_from_isr(void *ctx);

#ifdef __cplusplus
}
#endif
