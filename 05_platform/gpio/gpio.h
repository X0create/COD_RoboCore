/**
 * @file    gpio.h
 * @brief   GPIO 输入：按用途命名的引脚，任务里轮询读电平
 * @note    引脚模式（输入、上下拉）由 CubeMX 配置（06_boards/<板子>/REGEN_CHECKLIST.md），这里不重复初始化。
 *          没在 CubeMX 里配成输入的引脚（上电默认模拟模式）读出来恒为低电平。
 *          只轮询、不用外部中断：现在唯一的使用者是舵轮零点开关，1 kHz 轮询的边沿误差在找零时正反两次平均抵消。
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** 按用途命名的输入引脚；具体接在哪个引脚见芯片实现里的表。GPIO_IN_NONE = 没有接 */
typedef enum
{
    GPIO_IN_NONE = 0,      /* 配置结构体清零时就是“没有接” */
    GPIO_IN_ZERO_SWITCH_1, /* 零点开关（接近开关）1–4，舵轮找零用 */
    GPIO_IN_ZERO_SWITCH_2,
    GPIO_IN_ZERO_SWITCH_3,
    GPIO_IN_ZERO_SWITCH_4,
    GPIO_IN_COUNT,
} GpioInput;

/**
 * @brief   读输入引脚的电平
 * @return  true = 高电平
 * @pre     in 不是 GPIO_IN_NONE
 * @note    任务和中断里都可以调用
 */
bool gpio_read(GpioInput in);

#ifdef __cplusplus
}
#endif
