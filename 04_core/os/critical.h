/**
 * @file    critical.h
 * @brief   任务里用的临界区（《架构设计》运行时契约第 2 节）
 * @note    单独成一个头文件、不包含 FreeRTOS 头文件，这样话题、看门狗这些与芯片无关的模块在电脑测试里也能编译；
 *          固件里由 os.c 实现（taskENTER_CRITICAL），电脑测试里由 tests/host/fakes 提供空实现。
 *          只能在任务里调用，可以嵌套；中断里不能用。临界区里只做几十字节的拷贝，不调用会阻塞的函数。
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

void rm_critical_enter(void);
void rm_critical_exit(void);

#ifdef __cplusplus
}
#endif
