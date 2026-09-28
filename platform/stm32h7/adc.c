/**
 * @file    adc.c
 * @brief   ADC 的 STM32H7 实现，见 platform/adc.h
 * @note    CubeMX 配置：ADC1 16 位、连续转换、DMA 循环；两个转换序位都是通道 4（PC4，电池分压），
 *          读数取两者平均（同 COD-H7-Template bsp_adc.c 的配置，旧代码只用第一个）。
 */
#include "platform/adc.h"

#include "adc.h"
#include "dma_buf.h"

#define ADC_FULL_SCALE 65535.0f /* 16 位 */
#define ADC_VREF_V     3.3f

/* ADC1 的 DMA 缓冲区（DMA 访问不到 DTCM） */
RM_DMA_BUF static volatile uint16_t samples[2];
static bool started;

bool adc_start(void)
{
    started = HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) == HAL_OK
              && HAL_ADC_Start_DMA(&hadc1, (uint32_t *)samples, 2u) == HAL_OK;
    return started;
}

float adc_read_volts(AdcChannel ch)
{
    (void)ch; /* 目前只有电池一个通道 */
    if (!started)
    {
        return 0.0f;
    }
    const float raw = 0.5f * ((float)samples[0] + (float)samples[1]);
    return raw * ADC_VREF_V / ADC_FULL_SCALE;
}
