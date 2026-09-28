/**
 * @file    app_main.h
 * @brief   固件入口：启动顺序写死在这里，不按兵种改（《架构设计》运行时契约第 1 节）
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   调度器启动前的初始化，并创建全部任务
 * @note    由 CubeMX 生成的 freertos.c 在 MX_FREERTOS_Init() 的 USER CODE 区里调用；
 *          返回后由生成的 main() 调用 osKernelStart()
 */
void app_main(void);

/**
 * @brief   启动任务：调度器启动后第一个运行，完成后删除自己
 * @note    CubeMX 以最高优先级静态创建它（ADR 0025 修订），生成的是弱定义，这里是真正的实现
 */
void startup_task(void *argument);

#ifdef __cplusplus
}
#endif
