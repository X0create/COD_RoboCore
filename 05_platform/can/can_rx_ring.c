/**
 * @file    can_rx_ring.c
 * @brief   CAN 接收帧环形缓冲，见 can_rx_ring.h
 */
#include "can_rx_ring.h"

_Static_assert((CAN_RX_RING_SIZE & (CAN_RX_RING_SIZE - 1u)) == 0u,
               "CAN_RX_RING_SIZE must be a power of 2");

#define RING_MASK (CAN_RX_RING_SIZE - 1u)

bool can_rx_ring_push(CanRxRing *ring, const CanFrame *frame)
{
    const uint32_t head = ring->head;
    const uint32_t next = (head + 1u) & RING_MASK;
    if (next == ring->tail) /* 满了：丢弃新帧并计数（单生产者写 head、单消费者写 tail，不用加锁） */
    {
        ring->dropped++;
        return false;
    }
    ring->frames[head] = *frame;
    /* 内存屏障：帧内容必须先写完，再让消费者看到新的 head；否则可能读到写了一半的帧 */
    __sync_synchronize();
    ring->head = next;
    return true;
}

bool can_rx_ring_pop(CanRxRing *ring, CanFrame *out)
{
    const uint32_t tail = ring->tail;
    if (tail == ring->head)
    {
        return false;
    }
    /* 内存屏障：先确认看到了新的 head，再读帧内容 */
    __sync_synchronize();
    *out = ring->frames[tail];
    __sync_synchronize();
    ring->tail = (tail + 1u) & RING_MASK;
    return true;
}
