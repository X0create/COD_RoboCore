/**
 * @file    daemon_task.h
 * @brief   daemon 任务（100 Hz）：报告设备上线 / 离线；CAN 总线 bus-off 时重新启动控制器（《架构设计》核心机制第 3 节、任务划分）
 * @note    各兵种相同。只做报告，不参与安全判定：遥控丢失、电机离线由读取方按时间戳当场判断。
 *          任务开始时打印一次设备清单（本固件认为车上有哪些设备）。
 *          硬件看门狗 IWDG（收齐关键任务心跳才喂狗）在阶段 1 加入本任务。
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/** 任务入口，由兵种 robot.c 的任务表创建（栈 256 字够用） */
void daemon_task_entry(void *arg);

#ifdef __cplusplus
}
#endif
