/**
 * @file    can.c
 * @brief   can 的芯片实现共用的辅助代码（与芯片无关，电脑上测试），见 can.h 末尾一节
 */
#include "05_platform/can/can.h"

/* DLC 编码 0–15 对应的字节数 */
static const uint8_t dlc_len[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

uint8_t can_dlc_to_len(uint8_t dlc)
{
    return dlc_len[(dlc > 15u) ? 15u : dlc];
}

/* 反查：只有表里的长度（0–8、12、16、20、24、32、48、64）能发，其他长度返回 false */
bool can_len_to_dlc(uint8_t len, uint8_t *dlc)
{
    for (uint8_t code = 0u; code < 16u; code++)
    {
        if (dlc_len[code] == len)
        {
            *dlc = code;
            return true;
        }
    }
    return false;
}

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
