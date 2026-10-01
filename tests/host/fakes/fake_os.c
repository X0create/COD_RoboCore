/**
 * @file    fake_os.c
 * @brief   电脑测试是单线程的，临界区什么都不用做；延时让假时钟前进。代替 04_core/os/os.c 的 FreeRTOS 实现
 */
#include "04_core/os/critical.h"
#include "04_core/os/delay.h"
#include "05_platform/time/time.h"
#include "fake_time.h"

void rm_critical_enter(void)
{
}

void rm_critical_exit(void)
{
}

/* 延时让假时钟前进，被测代码里的等待不会卡住测试 */
void rm_delay_ms(uint32_t ms)
{
    fake_time_advance_ms(ms);
}

void rm_delay_us(uint32_t us)
{
    fake_time_set_us(rm_time_now_us() + us);
}
