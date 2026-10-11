/**
 * @file    uart.c
 * @brief   uart 的芯片实现共用的辅助代码（与芯片无关，电脑上测试），见 uart.h 末尾一节
 */
#include "05_platform/uart/uart.h"

#include <string.h>

void dma_ring_on_event(DmaRingWriter *w, uint32_t size, uint32_t dma_pos, uint64_t now_us)
{
    const uint32_t pos = dma_pos % size; /* 全满事件给的是 size，即回到开头 */
    /* 位置差按 size 取模：两次事件之间不超过半圈（半满、全满中断保证），没有歧义 */
    w->total += (pos + size - w->pos) % size;
    w->pos = pos;
    w->stamp_us = now_us;
}

void dma_ring_on_restart(DmaRingWriter *w)
{
    w->restarts++;
    w->restart_total = w->total;
    w->pos = 0u;
}

uint32_t dma_ring_read(DmaRingReader *r, const DmaRingWriter *w, const uint8_t *buf, uint32_t size,
                       uint8_t *out, uint32_t max_len)
{
    /* 重新启动过接收：DMA 从开头重新写，重启前没读的数据作废 */
    if (r->restarts != w->restarts)
    {
        r->restarts = w->restarts;
        r->pos = 0u;
        r->total = w->restart_total;
    }

    /* 未读超过一圈：最老的数据已被 DMA 覆盖，剩下的字节也对不上帧了，整段丢弃 */
    const uint32_t unread = w->total - r->total;
    if (unread >= size)
    {
        r->overruns++;
        r->pos = w->pos;
        r->total = w->total;
        return 0u;
    }

    const uint32_t n = (unread < max_len) ? unread : max_len;
    const uint32_t first = (n < size - r->pos) ? n : size - r->pos; /* 到缓冲区末尾为止的一段 */
    memcpy(out, &buf[r->pos], first);
    memcpy(&out[first], buf, n - first); /* 跨过末尾时，从开头再拷剩下的 */
    r->pos = (r->pos + n) % size;
    r->total += n;
    return n;
}
