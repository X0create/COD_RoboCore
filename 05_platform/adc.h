/**
 * @file    adc.h
 * @brief   模拟输入：按用途命名的通道，读引脚电压（ADR 0027、0033）
 * @note    ADC 用 DMA 连续采样，adc_read_volts() 读最新结果，任务里随时可调用、不阻塞。
 */
#pragma once

#include <stdbool.h>

#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    ADC_BATTERY, /* 电池电压分压后的引脚 */
    ADC_COUNT,
} AdcChannel;

/**
 * @brief   校准并开始连续采样
 * @return  false：校准或启动失败
 * @pre     调度器已启动（在 startup 任务里调用）；只调用一次
 */
RM_NODISCARD bool adc_start(void);

/** 引脚电压（V，0–3.3），未开始采样时为 0 */
float adc_read_volts(AdcChannel ch);

#ifdef __cplusplus
}
#endif
