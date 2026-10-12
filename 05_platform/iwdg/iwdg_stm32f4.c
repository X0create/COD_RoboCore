/**
 * @file    iwdg_stm32f4.c
 * @brief   STM32F4 的 IWDG（见 05_platform/iwdg/iwdg.h）
 * @note    寄存器写法照参考手册 RM0090“IWDG”一章：写 KR 的几个钥匙值启动、解锁、重装；
 *          和 H7 版的差别只有寄存器名字：复位标志在 RCC->CSR，调试冻结位在 DBGMCU->APB1FZ。
 *          F4 的 LSI 精度差（17–47 kHz），按 32 kHz 算的 50 ms 实际在 34–94 ms 之间；detect_task 每 10 ms 喂一次，都够用；
 *          LSI 由硬件在启动 IWDG 时自动打开。
 */
#include "05_platform/iwdg/iwdg.h"

#include <stm32f4xx.h>

/* KR 钥匙值（参考手册规定） */
#define IWDG_KEY_START  0xCCCCu /* 启动 */
#define IWDG_KEY_UNLOCK 0x5555u /* 允许改 PR、RLR */
#define IWDG_KEY_RELOAD 0xAAAAu /* 喂狗：计数器重装为 RLR */

#define LSI_HZ         32000u
#define IWDG_PRESCALER 4u /* PR = 0 → 4 分频：每计一次 0.125 ms */
#define IWDG_RELOAD    ((LSI_HZ / IWDG_PRESCALER) * IWDG_TIMEOUT_MS / 1000u) /* 400 */

_Static_assert(IWDG_RELOAD >= 1u && IWDG_RELOAD <= 0xFFFu, "RLR 只有 12 位");

/* 改 PR、RLR：解锁 → 写 → 等芯片同步完（SR 清零）→ 重装，新值才生效 */
static void configure(uint32_t reload)
{
    IWDG->KR = IWDG_KEY_UNLOCK;
    IWDG->PR = 0u;
    IWDG->RLR = reload;
    while (IWDG->SR != 0u)
    {
    }
    IWDG->KR = IWDG_KEY_RELOAD;
}

bool iwdg_caused_last_reset(void)
{
    const bool by_iwdg = (RCC->CSR & RCC_CSR_IWDGRSTF) != 0u;
    RCC->CSR |= RCC_CSR_RMVF; /* 清除全部复位标志，下次复位重新记录 */
    return by_iwdg;
}

void iwdg_start(void)
{
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP; /* 调试器暂停内核时 IWDG 也暂停 */
    IWDG->KR = IWDG_KEY_START;
    configure(IWDG_RELOAD);
}

void iwdg_feed(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

void iwdg_fault(void)
{
    __disable_irq();
    /* 经 IWDG 复位（不用 NVIC_SystemReset）：复位原因统一记为 IWDG，复位后据此自动恢复。
     * 没启动过也没关系：启动钥匙可以重复写。先设好最短超时再判断调试器：
     * 调试器断开后 C_DEBUGEN 可能残留，这时 BKPT 会让内核锁死，也要靠已经在倒计时的 IWDG 复位 */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
    IWDG->KR = IWDG_KEY_START;
    configure(1u); /* 最短超时：约 0.125 ms 后复位 */
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0u)
    {
        __BKPT(0); /* 连着调试器：停在这里（IWDG 跟着暂停），看调用栈找故障位置；继续运行就复位 */
    }
    for (;;)
    {
    }
}
