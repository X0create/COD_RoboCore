/**
 * @file    log.c
 * @brief   SEGGER RTT 日志，见 log.h
 */
#include "log.h"

#include "05_platform/time/time.h"

#include "FreeRTOS.h"

/*
 * RTT 写缓冲时屏蔽的中断档位必须和 FreeRTOS 一致：
 * 比它高，RTT 会挡住 FreeRTOS 以上的紧急中断；比它低，调用 FreeRTOS 接口的中断可能打断 RTT 写到一半。
 */
_Static_assert(SEGGER_RTT_MAX_INTERRUPT_PRIORITY == configMAX_SYSCALL_INTERRUPT_PRIORITY,
               "SEGGER_RTT_MAX_INTERRUPT_PRIORITY must equal configMAX_SYSCALL_INTERRUPT_PRIORITY");

void rm_log_init(void)
{
    SEGGER_RTT_Init();
}

uint32_t rm_log_ms(void)
{
    return (uint32_t)(rm_time_now_us() / 1000u);
}
