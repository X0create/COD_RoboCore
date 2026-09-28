/**
 * @file    dma_ring.h
 * @brief   从 DMA 循环接收缓冲区里取出新数据（纯计算，电脑上可测）
 * @note    DMA 在循环模式下不停地往缓冲区里写，写到末尾自动回到开头；
 *          任务记住自己读到哪里（read_pos），每次取走 [read_pos, write_pos) 之间的新字节。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   取出 [*read_pos, write_pos) 之间的新数据，跨过缓冲区末尾时分两段拷贝
 * @param   buf        DMA 循环缓冲区
 * @param   size       缓冲区长度
 * @param   read_pos   读位置，取完后更新；范围 [0, size)
 * @param   write_pos  DMA 当前写位置，范围 [0, size)
 * @param   out        输出缓冲区
 * @param   max_len    最多取多少字节；新数据更多时剩下的留到下次
 * @return  实际取出的字节数
 * @pre     两次调用之间新写入的数据不超过 size - 1 字节，否则 DMA 已经追上并覆盖了未读数据，
 *          取出的内容会乱（由设备层的帧长度、CRC 检查丢弃，不另做检测）
 */
uint32_t dma_ring_take(const uint8_t *buf, uint32_t size, uint32_t *read_pos, uint32_t write_pos,
                       uint8_t *out, uint32_t max_len);

#ifdef __cplusplus
}
#endif
