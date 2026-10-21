/** @file trace_stm32h7.c
 * @brief 诊断记录器的 DWT / PRIMASK 原语；不调用 RTOS，不格式化或发送。
 */
#include "05_platform/trace/trace.h"
#include <stm32h7xx.h>

/* 与 stm32h7xx_it.c 的全部外设入口同步；再生成后检查覆盖清单。 */
static const IRQn_Type covered_irqs[] = {
    DMA1_Stream0_IRQn, DMA1_Stream1_IRQn, DMA1_Stream2_IRQn,  DMA1_Stream3_IRQn, DMA1_Stream4_IRQn,
    DMA1_Stream5_IRQn, DMA1_Stream6_IRQn, FDCAN1_IT0_IRQn,    FDCAN2_IT0_IRQn,   FDCAN1_IT1_IRQn,
    FDCAN2_IT1_IRQn,   TIM2_IRQn,         SPI2_IRQn,          USART1_IRQn,       USART2_IRQn,
    USART3_IRQn,       DMA1_Stream7_IRQn, UART5_IRQn,         DMA2_Stream0_IRQn, DMA2_Stream1_IRQn,
    DMA2_Stream2_IRQn, DMA2_Stream3_IRQn, DMA2_Stream4_IRQn,  DMA2_Stream5_IRQn, DMA2_Stream6_IRQn,
    OTG_HS_IRQn,       UART7_IRQn,        BDMA_Channel0_IRQn, USART10_IRQn,      FDCAN3_IT0_IRQn,
    FDCAN3_IT1_IRQn
};

void rm_trace_platform_init(void)
{
    rm_trace_reset(SystemCoreClock);
    for (uint32_t i = 0u; i < sizeof(covered_irqs) / sizeof(covered_irqs[0]); ++i)
    {
        const uint32_t irq = (uint32_t)covered_irqs[i];
        rm_trace.covered_irqs[irq / 32u] |= 1u << (irq % 32u);
    }
}

uint32_t rm_trace_hw_lock(void)
{
    const uint32_t saved = __get_PRIMASK();
    __disable_irq();
    return saved;
}

void rm_trace_hw_unlock(uint32_t saved)
{
    __set_PRIMASK(saved);
}

uint32_t rm_trace_hw_cycles(void)
{
    return DWT->CYCCNT;
}

uint32_t rm_trace_hw_context(void)
{
    return __get_IPSR();
}

void rm_trace_hw_barrier(void)
{
    __DMB();
}

void rm_trace_hw_irqs(void)
{
    for (uint32_t i = 0u; i < RM_TRACE_IRQ_WORDS; ++i)
    {
        rm_trace.enabled_irqs[i] |= NVIC->ISER[i];
    }
}
