/**
 * @file    fake_os.c
 * @brief   电脑测试是单线程的，临界区什么都不用做；代替 core/os/os.c 的 FreeRTOS 实现
 */
#include "core/os/critical.h"

void rm_critical_enter(void)
{
}

void rm_critical_exit(void)
{
}
