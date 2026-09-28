/**
 * @file    ramp.h
 * @brief   斜坡：让一个量每次最多变化固定步长，逐步靠近目标（纯计算，电脑上可测）
 * @note    移植自 COD-H7-Template `Components/Algorithm/Src/Ramp.c` 的 `f_Ramp_Calc`，行为不变。
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   current 向 target 移动至多 step，不越过 target
 * @pre     step >= 0；每次调用的间隔固定时，step / 间隔 就是变化速率
 * @return  新的值
 */
float ramp_step(float current, float target, float step);

#ifdef __cplusplus
}
#endif
