/**
 * @file    adc_stm32f4.c
 * @brief   ADC 的 STM32F4 实现，见 05_platform/adc/adc.h
 * @note    CubeMX 配置（C 板）：ADC3 12 位、连续转换、DMA 循环；只有一个转换序位：通道 8（PF10，电池分压）。
 *          读数直接读 DMA 缓冲区，不用 DMA 的半满 / 满中断：HAL_ADC_Start_DMA 会打开它们，
 *          连续转换下每秒几万次中断却没人用，白白占 CPU，启动后关掉。
 *          F4 的 ADC 不需要像 H7 那样先校准。
 */
#include "05_platform/adc/adc.h"

#include <adc.h>

#define ADC_FULL_SCALE 4095.0f /* 12 位 */
#define ADC_VREF_V     3.3f

static volatile uint16_t sample; /* ADC3 的 DMA 缓冲区：一直被 DMA 改写成最新结果 */
static bool started;

bool adc_start(void)
{
    started = HAL_ADC_Start_DMA(&hadc3, (uint32_t *)&sample, 1u) == HAL_OK;
    if (started)
    {
        __HAL_DMA_DISABLE_IT(hadc3.DMA_Handle, DMA_IT_TC | DMA_IT_HT);
    }
    return started;
}

float adc_read_volts(AdcChannel ch)
{
    (void)ch; /* 目前只有电池一个通道 */
    if (!started)
    {
        return 0.0f;
    }
    return (float)sample * ADC_VREF_V / ADC_FULL_SCALE;
}
