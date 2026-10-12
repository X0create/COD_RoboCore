/**
 * @file    usb_cdc.h
 * @brief   USB 虚拟串口（CDC）：与上位机（视觉）通信的默认通道
 * @note    接收：USB 中断把数据放进环形缓冲并通知任务，任务用 usb_cdc_read() 取；
 *          发送：usb_cdc_write() 拷贝进发送缓冲后交给 USB，上一包还没发完时返回 false（调用者丢弃或下次再发）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 收到新数据时的通知，在**中断**里调用；只能做“通知任务”这类事 */
typedef void (*UsbCdcRxNotify)(void *ctx);

/**
 * @brief   初始化 USB 设备（开始枚举）并打开接收
 * @return  false：初始化失败
 * @pre     调度器已启动（在 startup_task 里调用）；只调用一次
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

/* ---------------- 芯片实现共用的辅助代码（实现在 usb_cdc.c） ---------------- */
/* 与芯片无关，电脑上测试；只给本目录的 *_<芯片>.c 用，上层不要调用 */

/*
 * 字节环形缓冲：单生产者、单消费者，中断写、任务读。
 * 生产者（中断）只改 head，消费者（任务）只改 tail，两边不会同时改同一个变量，所以不需要关中断。
 * 容量为 BYTE_RING_SIZE - 1 字节。写不下的字节丢弃并计数。
 */

#define BYTE_RING_SIZE 1024u /* 必须是 2 的幂；USB 全速每 1 ms 最多 64 字节，约 16 ms 的余量 */

typedef struct
{
    uint8_t data[BYTE_RING_SIZE];
    volatile uint32_t head;    /* 只由生产者改 */
    volatile uint32_t tail;    /* 只由消费者改 */
    volatile uint32_t dropped; /* 写不下被丢弃的字节数，只由生产者改 */
} ByteRing;

/** 放入最多 len 字节（生产者调用），返回放入的字节数 */
uint32_t byte_ring_push(ByteRing *ring, const uint8_t *data, uint32_t len);

/** 取出最多 max_len 字节（消费者调用），返回取出的字节数 */
uint32_t byte_ring_pop(ByteRing *ring, uint8_t *out, uint32_t max_len);

#ifdef __cplusplus
}
#endif
