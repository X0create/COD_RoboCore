/**
 * @file    cycle_extend.h
 * @brief   把会回绕的 32 位周期计数（DWT CYCCNT）扩展成不回绕的 64 位计数
 * @note    纯计算，不访问硬件：stm32h7 / stm32f4 的 time.c 共用，并在电脑上测试。
 *          H723 @ 550 MHz 时 32 位计数约 7.8 s 回绕一次（ADR 0020 单一时间基准）。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    uint64_t total;    /* 扩展后的 64 位计数 */
    uint32_t last_raw; /* 上一次读到的 32 位原始值 */
} CycleExtender;

/** 以当前原始值为起点：64 位计数从 raw 开始，与上电后的 CYCCNT 一致。 */
void cycle_extend_init(CycleExtender *ext, uint32_t raw);

/**
 * @brief   用新读到的原始值推进 64 位计数
 * @return  推进后的 64 位计数
 * @pre     两次调用之间原始计数前进不足 2^32 个周期，否则会少算整圈；
 *          任务和中断都会调用时，由调用方放进临界区。
 */
uint64_t cycle_extend_update(CycleExtender *ext, uint32_t raw);

#ifdef __cplusplus
}
#endif
