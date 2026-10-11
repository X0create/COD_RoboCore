/**
 * @file    log.h
 * @brief   SEGGER RTT 日志，E / W / I / D 四级
 * @note    - 格式串走 SEGGER_RTT_printf：支持 %d %u %x %s %c，**不支持 %f**（浮点先放大成整数，并写明倍率）；
 *          - 每行开头是 rm_time_init() 以来的毫秒数（所以 rm_time_init() 成功之前不能用 RM_LOG_*，改用 RM_LOG_E_NO_TIME）；一行不超过 128 字节时整行一次写入，不会和别的任务串行；
 *          - 只能在任务里或调度器启动前调用，中断里不打日志；1 kHz 的路径上不打日志。
 */
#pragma once

#include <stdint.h>

#include "SEGGER_RTT.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define RM_LOG_LEVEL_ERROR 1
#define RM_LOG_LEVEL_WARN  2
#define RM_LOG_LEVEL_INFO  3
#define RM_LOG_LEVEL_DEBUG 4

/* 编译期过滤：高于这一级的日志连同参数求值一起消失。Release 构建由 CMake 定义为 INFO */
#ifndef RM_LOG_LEVEL
#define RM_LOG_LEVEL RM_LOG_LEVEL_DEBUG
#endif

/** 初始化 RTT。调度器启动前调用一次；之后 J-Link / Ozone 才能找到 RTT 控制块 */
void rm_log_init(void);

/** rm_time_init() 以来的毫秒数，供日志前缀使用（约 49.7 天回绕，比赛和调试都够用） */
uint32_t rm_log_ms(void);

/*
 * 前缀和换行拼进同一个格式串，整行只调用一次 SEGGER_RTT_printf。
 * ##__VA_ARGS__ 是 GNU 扩展（本仓库用 gnu11），没有额外参数时去掉前面的逗号。
 */
#define RM_LOG_EMIT_(tag, fmt, ...)                                                                \
    ((void)SEGGER_RTT_printf(0u, "%8u " tag " " fmt "\n", (unsigned)rm_log_ms(), ##__VA_ARGS__))

/**
 * 不带时间戳的一行错误日志，只给时间基准不可用时用（rm_time_init() 失败，不能读时间）。
 * msg 必须是字符串字面量，不支持格式参数
 */
#define RM_LOG_E_NO_TIME(msg) ((void)SEGGER_RTT_WriteString(0u, "       - E " msg "\n"))

#if RM_LOG_LEVEL >= RM_LOG_LEVEL_ERROR
#define RM_LOG_E(fmt, ...) RM_LOG_EMIT_("E", fmt, ##__VA_ARGS__)
#else
#define RM_LOG_E(fmt, ...) ((void)0)
#endif

#if RM_LOG_LEVEL >= RM_LOG_LEVEL_WARN
#define RM_LOG_W(fmt, ...) RM_LOG_EMIT_("W", fmt, ##__VA_ARGS__)
#else
#define RM_LOG_W(fmt, ...) ((void)0)
#endif

#if RM_LOG_LEVEL >= RM_LOG_LEVEL_INFO
#define RM_LOG_I(fmt, ...) RM_LOG_EMIT_("I", fmt, ##__VA_ARGS__)
#else
#define RM_LOG_I(fmt, ...) ((void)0)
#endif

#if RM_LOG_LEVEL >= RM_LOG_LEVEL_DEBUG
#define RM_LOG_D(fmt, ...) RM_LOG_EMIT_("D", fmt, ##__VA_ARGS__)
#else
#define RM_LOG_D(fmt, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif
