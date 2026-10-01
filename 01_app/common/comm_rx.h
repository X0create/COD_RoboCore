/**
 * @file    comm_rx.h
 * @brief   接收的公共部分：中断唤醒 comm_rx 任务、分发 CAN 帧、打开各路接收（《架构设计》任务划分）
 * @note    中断只收数据并唤醒任务，解析都在 comm_rx 任务里做。“哪个串口交给哪个解析器”不在这里，
 *          写在每个兵种自己的 comm_rx_task.c 里，打开接收和读取都在那一个文件中。
 *          CAN 帧按 ID 分给订阅者：每个电机在 motor_init() 里订阅自己的反馈 ID（ID 冲突在初始化时报错）。
 */
#pragma once

#include "04_core/os/os.h"
#include "05_platform/uart.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** comm_rx 任务的控制块：中断要用它唤醒任务，所以定义在 comm_rx.c；由兵种 robot.c 的任务表创建 */
extern RmTask comm_rx_task;

/** 打开每一路 CAN 的接收（按初始化时登记的订阅配置滤波器）；失败记日志 @pre 在 comm_rx 任务里调用 */
void comm_rx_start_can(void);

/** 打开一个串口的 DMA 接收；失败记日志，之后读这个串口始终得到 0 字节 @pre 在 comm_rx 任务里调用 */
void comm_rx_start_uart(UartPort port);

/** 初始化 USB 设备并打开虚拟串口接收 @pre 在 comm_rx 任务里调用 */
void comm_rx_start_usb(void);

/** 等待中断通知“有新数据”；最多等 10 ms，防止某次通知丢失后数据积压 */
void comm_rx_wait(void);

/** 分发每一路 CAN 收到的全部帧（交给订阅者，如电机反馈解码） */
void comm_rx_can_all(void);

#ifdef __cplusplus
}
#endif
