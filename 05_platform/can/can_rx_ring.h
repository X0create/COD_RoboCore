/**
 * @file    can_rx_ring.h
 * @brief   CAN 接收帧的单生产者、单消费者环形缓冲：中断写、任务读（纯计算，电脑上可测）
 * @note    生产者（中断）只改 head，消费者（任务）只改 tail，不需要关中断（《架构设计》运行时契约第 2 节）。
 *          容量为 CAN_RX_RING_SIZE - 1 帧（空一格区分空和满）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "05_platform/can/can.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CAN_RX_RING_SIZE 32u /* 必须是 2 的幂；1 kHz 反馈时约 30 ms 的余量 */

typedef struct
{
    CanFrame frames[CAN_RX_RING_SIZE];
    volatile uint32_t head;    /* 只由生产者（中断）改 */
    volatile uint32_t tail;    /* 只由消费者（任务）改 */
    volatile uint32_t dropped; /* 满了被丢弃的帧数，只由生产者改 */
} CanRxRing;

/**
 * @brief   放入一帧（生产者调用）
 * @return  false：已满，这一帧被丢弃并计数
 */
bool can_rx_ring_push(CanRxRing *ring, const CanFrame *frame);

/**
 * @brief   取出一帧（消费者调用）
 * @return  false：没有帧
 */
bool can_rx_ring_pop(CanRxRing *ring, CanFrame *out);

#ifdef __cplusplus
}
#endif
