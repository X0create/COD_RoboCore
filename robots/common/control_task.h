/**
 * @file    control_task.h
 * @brief   control 任务：1 kHz 按绝对时刻周期调用兵种的 robot_control_step()（《架构设计》任务划分）
 * @note    每个周期的内容由兵种决定（读输入快照 → 安全门 → 各子系统 → 全车停改写 → 电机组发送），
 *          本任务只负责准时调用。1 kHz 任务里不打日志。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CONTROL_PERIOD_MS 1u

/** 静态创建 control 任务 @pre 调度器启动前调用 */
void control_task_create(uint32_t priority);

#ifdef __cplusplus
}
#endif
