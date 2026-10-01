/**
 * @file    time.c
 * @brief   STM32H7 的时间基准：DWT 周期计数器扩展成 64 位，再换算成微秒
 * @note    做法参考 COD_UniCFramework `impl_stm32_dwt.c`（Cortex-M7 的 DWT 解锁）
 */
#include "05_platform/time/time.h"

#include "cycle_extend.h"

#include <stm32h7xx.h>

/* CoreSight 组件的软件锁解锁值，由 Arm 架构规定；CMSIS 没有为 DWT 定义这个常数 */
#define DWT_LAR_UNLOCK_KEY 0xC5ACCE55UL

/* 自检时至少要走过这么多周期，才认为计数器在工作（远小于下面空循环实际消耗的周期数） */
#define SELF_TEST_MIN_CYCLES 100u

static CycleExtender cycles;
static uint32_t cycles_per_us;

bool rm_time_init(void)
{
    /* 打开跟踪模块总开关，DWT 才会工作 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /*
     * Cortex-M7 的 DWT 带软件锁（Cortex-M4 没有）：锁着时写 CTRL、CYCCNT 会被悄悄丢弃，
     * 不报错，计数器就是不走。所以先解锁。
     */
    DWT->LAR = DWT_LAR_UNLOCK_KEY;

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
