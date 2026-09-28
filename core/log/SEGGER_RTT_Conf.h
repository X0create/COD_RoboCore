/**
 * @file    SEGGER_RTT_Conf.h
 * @brief   SEGGER RTT 的项目配置（未列出的项用 SEGGER_RTT_ConfDefaults.h 的默认值）
 */
#pragma once

/*
 * RTT 写缓冲时用 BASEPRI 屏蔽到这一档，必须等于 FreeRTOS 的 configMAX_SYSCALL_INTERRUPT_PRIORITY
 * （CubeMX：LIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5，左移 4 位 = 0x50）。log.c 里用 _Static_assert 检查。
 */
#define SEGGER_RTT_MAX_INTERRUPT_PRIORITY (0x50)

/*
 * SEGGER_RTT_printf 先格式化到这么大的栈上缓冲区，满了才写一次 RTT。
 * 一行日志不超过这个长度时，整行在一次加锁的写入里完成，多个任务同时打日志也不会串行。
 */
#define SEGGER_RTT_PRINTF_BUFFER_SIZE (128u)
