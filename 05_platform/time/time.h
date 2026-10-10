/**
 * @file    time.h
 * @brief   全工程唯一的时间基准（ADR 0020）：上电以来的微秒数，64 位，不回绕
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   启动计时，并自检计数器确实在走
 * @return  false：计数器没有增长（例如调试组件被锁住），时间戳全部不可用
 * @pre     系统时钟已配置好；在调度器启动前、单线程下调用一次
 */
RM_NODISCARD bool rm_time_init(void);

/**
 * @brief   上电以来的微秒数
 * @pre     rm_time_init() 已返回 true；两次调用间隔不超过硬件计数器一圈
 *          （H723 @ 550 MHz 约 7.8 s；1 kHz 任务每周期都会调用，第一阶段由心跳任务保证）
 * @note    任务和中断里都可以调用
 */
uint64_t rm_time_now_us(void);

/* ---------------- 芯片实现共用的辅助代码（实现在 time.c） ---------------- */
/* 与芯片无关，电脑上测试；只给本目录的 *_<芯片>.c 用，上层不要调用 */

/*
 * 把会回绕的 32 位周期计数（DWT CYCCNT）扩展成不回绕的 64 位计数。
 * 纯计算，不访问硬件：stm32h7 / stm32f4 的芯片实现共用。H723 @ 550 MHz 时 32 位计数约 7.8 s 回绕一次（ADR 0020）。
 */

typedef struct
{
    uint64_t total;    /* 扩展后的 64 位计数 */
    uint32_t last_raw; /* 上一次读到的 32 位原始值 */
} CycleExtend;

/** 以当前原始值为起点：64 位计数从 raw 开始，与上电后的 CYCCNT 一致。 */
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
