/**
 * @file    time.h
 * @brief   全工程唯一的时间基准（ADR 0020）：上电以来的微秒数，64 位，不回绕
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/compiler.h"

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

#ifdef __cplusplus
}
#endif
