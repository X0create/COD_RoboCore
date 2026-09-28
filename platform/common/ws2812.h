/**
 * @file    ws2812.h
 * @brief   WS2812 的 SPI 编码：每个颜色位编成一个 SPI 字节（纯计算，电脑上可测）
 * @note    要求 SPI 时钟 6 MHz、8 位数据、MSB 先发（《架构设计》附录 A.1，UniC 实测）：
 *          一个 SPI 位约 167 ns，0x60 高电平 2 位 = 333 ns（0 码），0x78 高电平 4 位 = 667 ns（1 码）。
 *          两个码都以低电平开头和结尾，连续发送时相邻两位的脉冲不会连成一个。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define WS2812_CODE_0        0x60u
#define WS2812_CODE_1        0x78u
#define WS2812_BYTES_PER_LED 24u /* 8 位 × 3 种颜色，每位一个字节 */
#define WS2812_LATCH_BYTES   100u /* 末尾的低电平复位时间：100 字节 ≈ 133 µs（要求 > 50 µs） */

/**
 * @brief   把一个灯的颜色编码成 24 个 SPI 字节
 * @note    WS2812 先收绿色，所以按 G、R、B 的顺序编码；弄成 RGB 时红绿会对调，看起来像接线错了
 */
void ws2812_encode(uint8_t out[WS2812_BYTES_PER_LED], uint8_t red, uint8_t green, uint8_t blue);

#ifdef __cplusplus
}
#endif
