/**
 * @file    time_internal.h
 * @brief   time 的芯片实现共用的辅助代码（实现在 time.c）：只给本目录的 time_<芯片>.c 和主机测试用，上层不要 include
 * @note    与芯片无关、不访问硬件，电脑上测试；stm32h7 / stm32f4 的芯片实现共用。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 把会回绕的 32 位周期计数（DWT CYCCNT）扩展成 64 位计数。
 * H723 @ 550 MHz 时 32 位计数约 7.8 s 回绕一次（2^32 / 550 MHz），所以要扩展成 64 位才能长时间计时。
 */
typedef struct
{
    uint64_t total;    /* 扩展后的 64 位计数 */
    uint32_t last_raw; /* 上一次读到的 32 位原始值 */
} CycleExtend;

/** 以当前原始值为起点：64 位计数从 raw 开始，与 rm_time_init() 清零后的 CYCCNT 一致 */
void cycle_extend_init(CycleExtend *ext, uint32_t raw);

/**
 * @brief   用新读到的原始值推进 64 位计数
 * @return  推进后的 64 位计数
 * @pre     两次调用之间原始计数前进不足 2^32 个周期，否则会少算整圈；
 *          任务和中断都会调用时，由调用方放进临界区。
 */
uint64_t cycle_extend_update(CycleExtend *ext, uint32_t raw);

#ifdef __cplusplus
}
#endif
