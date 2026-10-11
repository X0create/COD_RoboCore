/**
 * @file    uart.h
 * @brief   串口接收：DMA 循环接收 + 空闲中断（《架构设计》运行时契约第 2 节）
 * @note    中断里只调用通知回调，不处理数据；帧同步、校验、解析都在任务里调用 uart_read() 之后做。
 *          阶段 0 只有接收；发送以后需要时再加。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "05_platform/compiler.h"

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
 * @brief   取出到最近一次接收事件（空闲、DMA 半满、全满中断）为止收到的新字节
 * @param   rx_us  输出：最近一次接收事件的时刻，即这批数据中最后一段实际收到的时刻（返回 0 时不写）。
 *                 喂设备的看门狗要用它，不能用任务处理数据的时刻：任务被耽误时，旧数据会被当成刚收到的
 * @return  取出的字节数，最多 max_len；新数据更多时剩下的留到下次；串口没有成功打开时返回 0
 * @note    两次调用间隔内收到的数据超过接收缓冲区（256 字节）时，未读数据已被覆盖：
 *          整段丢弃、从最新位置重新开始，返回 0（设备层按时间间隔重新找帧头）
 * @pre     只由一个任务调用
 */
uint32_t uart_read(UartPort port, uint8_t *out, uint32_t max_len, uint64_t *rx_us);

/* ---------------- 芯片实现共用的辅助代码（实现在 uart.c） ---------------- */
/* 与芯片无关，电脑上测试；只给本目录的 *_<芯片>.c 用，上层不要调用 */

/*
 * DMA 循环接收缓冲区的读写记账。DMA 在循环模式下不停地往缓冲区里写，写到末尾自动回到开头。
 * 只比较读写位置分不出“空”和“正好绕了一整圈”，所以中断里另记累计写入的字节数：
 * 每次接收事件按位置差累加；半满、全满中断保证两次事件之间不超过半圈，位置差不会有歧义。
 * 任务按累计数算出未读多少，超过一圈就知道数据被覆盖了。
 */

/** 中断写（最近一次接收事件）；任务读之前要在关中断时整份拷贝 */
typedef struct
{
    uint32_t total;         /* 累计写入的字节数（按 2^32 回绕） */
    uint32_t pos;           /* 最近一次事件时 DMA 的写位置，[0, size) */
    uint64_t stamp_us;      /* 最近一次事件的时刻 */
    uint32_t restarts;      /* 出错后重新启动接收的次数 */
    uint32_t restart_total; /* 最近一次重新启动时的 total */
} DmaRingWriter;

/** 任务读 */
typedef struct
{
    uint32_t total;    /* 累计取走（含丢弃）的字节数 */
    uint32_t pos;      /* 读位置，[0, size) */
    uint32_t restarts; /* 已经处理过的重启次数 */
    uint32_t overruns; /* 未读数据被覆盖、整段丢弃的次数（调试用） */
} DmaRingReader;

/** 中断里：一次接收事件，dma_pos 是 DMA 当前写到的位置（HAL 回调的 Size，可以等于 size） */
void dma_ring_on_event(DmaRingWriter *w, uint32_t size, uint32_t dma_pos, uint64_t now_us);

/** 中断里：出错后重新启动了接收，DMA 从缓冲区开头重新写 */
void dma_ring_on_restart(DmaRingWriter *w);

/**
 * @brief   任务里：取出 w 记录的事件为止的新数据，跨过缓冲区末尾时分两段拷贝
 * @param   w  中断记录的整份拷贝
 * @return  实际取出的字节数；未读超过一圈时丢弃、返回 0
 */
uint32_t dma_ring_read(DmaRingReader *r, const DmaRingWriter *w, const uint8_t *buf, uint32_t size,
                       uint8_t *out, uint32_t max_len);

#ifdef __cplusplus
}
#endif
