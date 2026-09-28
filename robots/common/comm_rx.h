/**
 * @file    comm_rx.h
 * @brief   comm_rx 任务：收到通知后，在任务里分发各路 CAN 帧、把各串口和 USB 虚拟串口收到的字节交给对应设备（《架构设计》任务划分）
 * @note    中断只负责收数据和通知（comm_rx_notify_from_isr），订阅者和设备的解析都在本任务里执行。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/compiler.h"
#include "platform/uart.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 串口数据的接收者：data 是本次读到的一段字节，now_us 是读到的时刻 */
typedef void (*CommRxUartHandler)(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx);

/**
 * @brief   登记一个串口由谁解析（例如 DR16 的 UART5）
 * @return  false：登记数已满，或这个串口已经登记过
 * @pre     初始化阶段（robot_init 里）调用
 */
RM_NODISCARD bool comm_rx_add_uart(UartPort port, CommRxUartHandler handler, void *ctx);

/**
 * @brief   登记 USB 虚拟串口由谁解析（例如视觉链路）；不登记就不初始化 USB
 * @pre     初始化阶段（robot_init 里）调用；只登记一个
 */
void comm_rx_set_usb(CommRxUartHandler handler, void *ctx);

/**
 * @brief   静态创建 comm_rx 任务
 * @param   priority  FreeRTOS 优先级，由兵种的 robot_create_tasks() 统一给出（应高于 control 以外的任务）
 * @pre     调度器启动前、can_start() 之前调用
 */
void comm_rx_create_task(uint32_t priority);

/**
 * @brief   打开所有已登记串口的接收，收到数据时唤醒 comm_rx 任务
 * @pre     调度器已启动（在 startup 任务里调用）
 */
void comm_rx_start_uarts(void);

/**
 * @brief   登记过 USB 解析者时，初始化 USB 设备并打开接收
 * @pre     调度器已启动（在 startup 任务里调用）
 */
void comm_rx_start_usb(void);

/** 作为 can_start() / uart_rx_start() / usb_cdc_start() 的通知回调：在中断里唤醒 comm_rx 任务。ctx 未使用 */
void comm_rx_notify_from_isr(void *ctx);

#ifdef __cplusplus
}
#endif
