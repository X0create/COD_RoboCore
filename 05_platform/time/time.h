/**
 * @file    time.h
 * @brief   全工程唯一的时间基准：rm_time_init() 以来的微秒数，64 位
 * @note    为什么只要一个：所有模块的“多久没收到数据”“两次更新隔了多久”都用同一个时钟比较，结果才对得上。
 *          用的是 CPU 内核自带的 DWT 周期计数器（每个 CPU 时钟加 1），精度远高于 FreeRTOS 的 1 ms tick。
 *          起点是 rm_time_init()：它把 DWT->CYCCNT 清零，不含此前的上电启动时间。
 *          64 位扩展靠持续读取：两次读取间隔必须小于硬件计数器一圈（H723 @ 550 MHz 约 7.8 s），
 *          否则会漏算整圈、时间变小。目前由 1 kHz 的 ins_task、control_task 每周期调用保证。
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
 * @brief   把计数器清零并启动计时，再自检计数器确实在走
 * @return  false：计数器没有增长（例如调试组件被锁住），时间戳全部不可用
 * @pre     系统时钟已配置好；在调度器启动前、单线程下调用一次
 */
RM_NODISCARD bool rm_time_init(void);

/**
 * @brief   rm_time_init() 以来的微秒数
 * @pre     rm_time_init() 已返回 true（之前不能调用，包括经由 RM_LOG_*() 间接调用）；
 *          与上一次调用间隔小于计数器一圈（约 7.8 s，见文件说明）
 * @note    任务和中断里都可以调用
 */
uint64_t rm_time_now_us(void);

#ifdef __cplusplus
}
#endif
