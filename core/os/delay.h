/**
 * @file    delay.h
 * @brief   延时（不含 FreeRTOS 头文件，设备驱动和电脑测试都能用）
 * @note    固件里由 os.c 实现；电脑测试里由 tests/host/fakes 实现（让假时钟前进）。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** 当前任务睡眠 ms 毫秒，让出 CPU @pre 在任务里、调度器已启动 */
void rm_delay_ms(uint32_t ms);

/** 忙等 us 微秒（不让出 CPU），只用于几百微秒以内的等待，如传感器寄存器写入间隔 */
void rm_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif
