/**
 * @file    time_stm32f4.c
 * @brief   STM32F4 的时间基准：DWT 周期计数器扩展成 64 位，再换算成微秒
 * @note    和 H7 版相同，只少了解锁一步：Cortex-M4 的 DWT 没有软件锁。
 *          168 MHz 时 32 位计数约 25.6 s 回绕一次（比 H7 的 7.8 s 宽松）。
 */
#include "05_platform/time/time.h"
#include "05_platform/time/time_internal.h"

#include <stm32f4xx.h>

/* 自检时至少要走过这么多周期，才认为计数器在工作（远小于下面空循环实际消耗的周期数） */
#define SELF_TEST_MIN_CYCLES 100u

static CycleExtend cycles;
static uint32_t cycles_per_us;

bool rm_time_init(void)
{
    /* 打开跟踪模块总开关，DWT 才会工作 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    cycles_per_us = SystemCoreClock / 1000000u;

    /* 自检：空转一小段，计数器必须增长 */
    const uint32_t start = DWT->CYCCNT;
    for (volatile uint32_t i = 0u; i < 1000u; i++)
    {
    }
    if ((uint32_t)(DWT->CYCCNT - start) < SELF_TEST_MIN_CYCLES)
    {
        return false;
    }

    cycle_extend_init(&cycles, DWT->CYCCNT);
    return true;
}

uint64_t rm_time_now_us(void)
{
    /* 扩展计数是“读—改—写”共享状态：任务和中断交错会多算或漏算一圈，所以关中断执行 */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint64_t total = cycle_extend_update(&cycles, DWT->CYCCNT);
    __set_PRIMASK(primask);

    return total / cycles_per_us;
}
