/**
 * @file    comm_rx_common.h
 * @brief   接收的公共部分：中断唤醒 comm_rx_task、打开各路接收（《架构设计》任务划分）
 * @note    中断只收数据并唤醒任务，解析都在 comm_rx_task 里做。“哪路 CAN、哪个串口交给哪个设备”不在这里，
 *          写在01_applic/robot/robot_comm_rx_task.c 里，打开接收和读取都在那一个文件中。
 */
#pragma once

#include "04_core/os/os.h"
#include "05_platform/uart/uart.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** comm_rx_task 的控制块：中断要用它唤醒任务，所以定义在 comm_rx_common.c；由robot.c 的任务表创建 */
extern RmTask comm_rx_task;

/** 打开每一路 CAN 的接收（全部标准帧）；失败记日志 @pre 在 comm_rx_task 里调用 */
void comm_rx_start_can(void);

/** 打开一个串口的 DMA 接收；失败记日志，之后读这个串口始终得到 0 字节 @pre 在 comm_rx_task 里调用 */
void comm_rx_start_uart(UartPort port);

/** 初始化 USB 设备并打开虚拟串口接收 @pre 在 comm_rx_task 里调用 */
void comm_rx_start_usb(void);

/** CAN 总线 bus-off 时重新启动控制器（同一路至少间隔 100 ms）；bus-off 期间这路电机全部离线 = 机构停 */
void comm_rx_recover_bus_off(void);

/** 等待中断通知“有新数据”；最多等 10 ms，防止某次通知丢失后数据积压 */
void comm_rx_wait(void);

#ifdef __cplusplus
}
#endif
