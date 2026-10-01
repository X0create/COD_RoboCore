/**
 * @file    uart.h
 * @brief   串口接收：DMA 循环接收 + 空闲中断（《架构设计》运行时契约第 2 节）
 * @note    中断里只调用通知回调，不处理数据；帧同步、校验、解析都在任务里调用 uart_read() 之后做。
 *          阶段 0 只有接收；发送以后需要时再加。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 按芯片上的串口编号命名；某块板没有接出或没有配置的编号，uart_rx_start() 返回 false */
typedef enum
{
    UART_1,
    UART_2,
    UART_3,
    UART_4,
    UART_5,
    UART_6,
    UART_7,
    UART_8,
    UART_9,
    UART_10,
    UART_COUNT,
} UartPort;

/**
 * 收到新数据时的通知，在**中断**里调用（空闲中断、DMA 半满、全满）。
 * 只能做“通知任务”这类事（如 vTaskNotifyGiveFromISR），不能处理数据、不能打日志。
 */
typedef void (*UartRxNotify)(void *ctx);

/**
 * @brief   开始 DMA 循环接收
 * @param   notify  新数据通知，可以为 NULL（只靠任务定时 uart_read() 轮询）
 * @return  false：这块板没有这个串口，或它的接收 DMA 没有配置成循环模式（CubeMX 配置问题）
 * @pre     调度器已经启动（《架构设计》：接收在 xxx_start() 阶段打开）；每个串口只调用一次
 */
RM_NODISCARD bool uart_rx_start(UartPort port, UartRxNotify notify, void *ctx);

/**
 * @brief   取出上次调用以来收到的新字节
 * @return  取出的字节数，最多 max_len；新数据更多时剩下的留到下次
 * @pre     uart_rx_start() 已成功；只由一个任务调用；两次调用间隔内收到的数据不超过接收缓冲区（256 字节），
 *          否则旧数据被覆盖、取出的内容会乱（由设备层的帧检查丢弃）
 */
uint32_t uart_read(UartPort port, uint8_t *out, uint32_t max_len);

#ifdef __cplusplus
}
#endif
