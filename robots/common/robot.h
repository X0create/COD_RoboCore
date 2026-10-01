/**
 * @file    robot.h
 * @brief   每个兵种（每块板）必须实现的函数，由框架按固定顺序调用
 */
#pragma once

#include <stdbool.h>

#include "platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   组装本兵种：设备 → 子系统 → 安全门
 * @return  false：必需的设备或子系统初始化失败，原因已记日志；系统保持不可解锁
 */
RM_NODISCARD bool robot_init(void);

/** 静态创建本兵种的全部任务（startup 任务除外，它由 CubeMX 创建）。兵种的 robot.c 里有一张任务表 */
void robot_create_tasks(void);

/** startup 任务最后调用（调度器已运行、接收已打开）：设备自检之后允许解锁 */
void robot_start(void);

#ifdef __cplusplus
}
#endif
