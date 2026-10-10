/**
 * @file    status_led.h
 * @brief   板载状态灯：每块板一份实现（MC02 是 SPI6 上的一颗 WS2812，C 板是三色 LED）
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   设置状态灯颜色，立即生效
 * @pre     只由一个任务调用（阶段 0 为心跳任务）；不在中断里调用
 * @note    MC02 上是约 165 µs 的阻塞发送；发送失败时忽略，状态灯不影响控制
 */
void status_led_set(uint8_t red, uint8_t green, uint8_t blue);

/* ---------------- 芯片实现共用的辅助代码（实现在 status_led.c） ---------------- */
/* 与芯片无关，电脑上测试；只给本目录的 *_<芯片>.c 用，上层不要调用 */

/*
 * WS2812 的 SPI 编码：每个颜色位编成一个 SPI 字节（编码方式参考 COD_UniCFramework `dev_ws2812.c`）。
 * 要求 SPI 时钟 6 MHz、8 位数据、MSB 先发（《架构设计》附录 A.1，UniC 实测）：
 * 一个 SPI 位约 167 ns，0x60 高电平 2 位 = 333 ns（0 码），0x78 高电平 4 位 = 667 ns（1 码）。
 * 两个码都以低电平开头和结尾，连续发送时相邻两位的脉冲不会连成一个。
 */

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
