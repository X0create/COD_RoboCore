/**
 * @file    uart.c
 * @brief   uart 的芯片实现共用的辅助代码（与芯片无关，电脑上测试），见 uart.h 末尾一节
 */
#include "uart.h"

#include <string.h>

uint32_t dma_ring_take(const uint8_t *buf, uint32_t size, uint32_t *read_pos, uint32_t write_pos,
                       uint8_t *out, uint32_t max_len)
{
    uint32_t taken = 0u;
    uint32_t pos = *read_pos;

    /* 写位置在读位置前面时，新数据是一整段；在后面时说明 DMA 已经绕回开头，分两段 */
    while (pos != write_pos && taken < max_len)
    {
        const uint32_t end = (write_pos > pos) ? write_pos : size;
        uint32_t chunk = end - pos;
        if (chunk > max_len - taken)
        {
            chunk = max_len - taken;
        }
        memcpy(&out[taken], &buf[pos], chunk);
        taken += chunk;
        pos += chunk;
        if (pos == size)
        {
            pos = 0u;
        }
    }

    *read_pos = pos;
    return taken;
}
