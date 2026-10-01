/**
 * @file    comm_rx_task.h
 * @brief   comm_rx 任务（收到数据就运行）：分发各路 CAN 帧，把各串口和 USB 虚拟串口收到的字节交给对应设备
 *          （《架构设计》任务划分）
 * @note    各兵种相同。中断只负责收数据和唤醒本任务，订阅者和设备的解析都在本任务里执行。
 *          用法（都在兵种的 robot.c 里）：
 *            初始化时   comm_rx_add_uart() / comm_rx_set_usb() 登记谁来解析
 *            任务表里   用 comm_rx_task 和 comm_rx_task_entry 创建本任务
 *            startup 里 comm_rx_start() 打开全部接收
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "04_core/os/os.h"
#include "05_platform/compiler.h"
#include "05_platform/uart.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 本任务的控制块：中断要用它唤醒本任务，所以定义在 comm_rx_task.c；由兵种 robot.c 的任务表创建 */
extern RmTask comm_rx_task;

/** 任务入口，由兵种 robot.c 的任务表创建（优先级应高于 control 以外的任务，栈 512 字够用） */
void comm_rx_task_entry(void *arg);

/** 串口数据的接收者：data 是本次读到的一段字节，now_us 是读到的时刻 */
typedef void (*CommRxUartHandler)(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx);

/**
 * @brief   登记一个串口由谁解析（例如 DR16 的 UART5）
 * @return  false：登记数已满，或这个串口已经登记过
 * @pre     初始化阶段（调度器启动前）调用
 */
RM_NODISCARD bool comm_rx_add_uart(UartPort port, CommRxUartHandler handler, void *ctx);

/**
 * @brief   登记 USB 虚拟串口由谁解析（例如视觉链路）；不登记就不初始化 USB
 * @pre     初始化阶段（调度器启动前）调用；只登记一个
 */
void comm_rx_set_usb(CommRxUartHandler handler, void *ctx);

/**
 * @brief   打开全部接收：每路 CAN（按初始化时登记的订阅配置滤波器）、登记过的串口、登记过的 USB。
 *          收到数据时由中断唤醒本任务
 * @pre     调度器已启动、本任务已创建（在 startup 任务里调用）
 */
void comm_rx_start(void);

#ifdef __cplusplus
}
#endif
