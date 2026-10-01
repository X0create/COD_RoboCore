/**
 * @file    usb_cdc.h
 * @brief   USB 虚拟串口（CDC）：与上位机（视觉）通信的默认通道（ADR 0027）
 * @note    接收：USB 中断把数据放进环形缓冲并通知任务，任务用 usb_cdc_read() 取；
 *          发送：usb_cdc_write() 拷贝进发送缓冲后交给 USB，上一包还没发完时返回 false（调用者丢弃或下次再发）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 收到新数据时的通知，在**中断**里调用；只能做“通知任务”这类事 */
typedef void (*UsbCdcRxNotify)(void *ctx);

/**
 * @brief   初始化 USB 设备（开始枚举）并打开接收
 * @return  false：初始化失败
 * @pre     调度器已启动（在 startup 任务里调用）；只调用一次
 */
RM_NODISCARD bool usb_cdc_start(UsbCdcRxNotify notify, void *ctx);

/**
 * @brief   取出收到的字节
 * @return  取出的字节数，最多 max_len
 * @pre     usb_cdc_start() 已成功；只由一个任务调用
 */
uint32_t usb_cdc_read(uint8_t *out, uint32_t max_len);

/** 发送缓冲的大小：一次 usb_cdc_write() 最多这么多字节 */
#define USB_CDC_TX_MAX 512u

/**
 * @brief   发送
 * @return  false：USB 还没连上、上一包没发完，或 len 超过 USB_CDC_TX_MAX；这次数据没有发出
 * @pre     只由一个任务调用（发送缓冲不加锁）
 */
RM_NODISCARD bool usb_cdc_write(const uint8_t *data, uint32_t len);

/** 接收环形缓冲写不下、被丢弃的字节数（调试用） */
uint32_t usb_cdc_rx_dropped(void);

#ifdef __cplusplus
}
#endif
