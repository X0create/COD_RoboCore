/**
 * @file    byte_ring.h
 * @brief   字节的单生产者、单消费者环形缓冲：中断写、任务读（纯计算，电脑上可测）
 * @note    生产者（中断）只改 head，消费者（任务）只改 tail，不需要关中断（《架构设计》运行时契约第 2 节）。
 *          容量为 BYTE_RING_SIZE - 1 字节。写不下的字节丢弃并计数。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

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
