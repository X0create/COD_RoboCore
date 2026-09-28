/**
 * @file    app_main.c
 * @brief   固件入口和启动任务，见 app_main.h
 * @note    startup_task 必须和 app_main 放在同一个文件里：它在 CubeMX 生成代码里已有弱定义，
 *          单独放进静态库的另一个 .o 时链接器不会去取，弱定义的空函数就会被悄悄用上。
 */
#include "app_main.h"

#include "robot.h"

#include "platform/time.h"

#include "core/log/log.h"
#include "core/os/os.h"

/* 阶段 0 还没有 RM_ASSERT 和故障记录（阶段 1），初始化失败时先停在这里，调试器能直接看到位置 */
static void halt_on_init_failure(void)
{
    for (;;)
    {
    }
}

void app_main(void)
{
    const bool time_ok = rm_time_init(); /* 1. DWT 计时 */
    rm_log_init();                       /* 2. RTT 日志 */

    RM_LOG_I("COD RoboCore booting");
    if (!time_ok)
    {
        RM_LOG_E("DWT cycle counter not running");
        halt_on_init_failure();
    }

    /* 3–5（读故障记录、board_init、参数）在阶段 1 加入 */

    if (!robot_init()) /* 6. 设备 → 子系统 → 安全门 */
    {
        RM_LOG_E("robot_init failed");
        halt_on_init_failure();
    }
    robot_create_tasks(); /* 7. 其余任务 */
    RM_LOG_I("starting scheduler");
}

void startup_task(void *argument)
{
    (void)argument;

    /* 9–12（打开接收中断、设备自检、硬件看门狗、允许解锁）在阶段 1 加入 */
    RM_LOG_I("startup done");
    rm_task_delete_self();
}
