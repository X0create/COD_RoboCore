/**
 * @file    byte_ring.c
 * @brief   字节环形缓冲，见 byte_ring.h
 */
#include "byte_ring.h"

_Static_assert((BYTE_RING_SIZE & (BYTE_RING_SIZE - 1u)) == 0u,
               "BYTE_RING_SIZE must be a power of 2");

#define RING_MASK (BYTE_RING_SIZE - 1u)

uint32_t byte_ring_push(ByteRing *ring, const uint8_t *data, uint32_t len)
{
    uint32_t head = ring->head;
    const uint32_t tail = ring->tail;
    uint32_t n = 0u;
    while (n < len && ((head + 1u) & RING_MASK) != tail)
    {
        ring->data[head] = data[n++];
        head = (head + 1u) & RING_MASK;
    }
    ring->dropped += len - n;
    /* 内存屏障：数据先写完，再让消费者看到新的 head */
    __sync_synchronize();
    ring->head = head;
    return n;
}

uint32_t byte_ring_pop(ByteRing *ring, uint8_t *out, uint32_t max_len)
{
    const uint32_t head = ring->head;
    uint32_t tail = ring->tail;
    uint32_t n = 0u;
    /* 内存屏障：先确认看到了新的 head，再读数据 */
    __sync_synchronize();
    while (n < max_len && tail != head)
    {
        out[n++] = ring->data[tail];
        tail = (tail + 1u) & RING_MASK;
    }
    __sync_synchronize();
    ring->tail = tail;
    return n;
}
