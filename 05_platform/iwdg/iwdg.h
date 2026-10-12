/**
 * @file    iwdg.h
 * @brief   硬件看门狗 IWDG：程序卡死超过 IWDG_TIMEOUT_MS 没喂狗，芯片自己复位
 * @note    不是 04_core/watchdog（设备在线检测）。IWDG 有自己的时钟（LSI），CPU 卡死它也照样倒计时。
 *          - 谁喂：detect_task 每 10 ms 检查一次 control_task 的心跳，在涨才喂（只有 control_task 发电机指令；
 *            其他任务卡死不影响电机安全，没必要为它们整板复位）；
 *          - 调试器暂停（Halt）时 IWDG 一起暂停，打断点不会被复位；
 *          - 致命故障（栈溢出、断言、HardFault）也经 IWDG 立即复位，所以“上次是不是 IWDG 复位”
 *            就等于“上次是不是程序出了故障”，复位后据此自动恢复、不用操作手重新解锁（见 safety_gate.h）。
 *          直接写寄存器，不用 CubeMX 生成 IWDG 初始化。
 */
#pragma once

#include <stdbool.h>

#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 超时：卡死后最多这么久复位。按 LSI 标称 32 kHz 设置，LSI 有个体偏差，实际值略有出入 */
#define IWDG_TIMEOUT_MS 50u

/**
 * @brief   上一次复位是不是 IWDG 引起的（程序卡死或致命故障）。读完清除芯片的复位标志
 * @pre     上电后、任何地方清除复位标志之前调用一次（app_main 开头）
 */
bool iwdg_caused_last_reset(void);

/** 启动 IWDG（启动后不能停止，直到下次复位）；调试器暂停时它也暂停 */
void iwdg_start(void);

/** 喂狗：倒计时重新从 IWDG_TIMEOUT_MS 开始 */
void iwdg_feed(void);

/**
 * @brief   致命故障（栈溢出、configASSERT、HardFault 等）：关中断；连着调试器时停在断点，方便看现场；
 *          否则（赛场上）立即经 IWDG 复位。不在这里发 CAN：栈可能已经坏了
 * @note    中断、任务里都可以调用
 */
RM_NORETURN void iwdg_fault(void);

#ifdef __cplusplus
}
#endif
