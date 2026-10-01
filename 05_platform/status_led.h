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
void rm_status_led_set(uint8_t red, uint8_t green, uint8_t blue);

#ifdef __cplusplus
}
#endif
