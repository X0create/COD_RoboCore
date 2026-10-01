/**
 * @file    app_main.c
 * @brief   上电顺序，见 app_main.h
 * @note    startup_task 必须和 app_main 放在同一个文件里：它在 CubeMX 生成代码里已有弱定义，
 *          单独放进静态库的另一个 .o 时链接器不会去取，弱定义的空函数就会被悄悄用上。
 */
#include "app_main.h"

#include "01_applic/config/objects.h"
#include "01_applic/system/safety_gate.h"
#include "04_core/log/log.h"
#include "05_platform/time/time.h"

/* 阶段 0 还没有 RM_ASSERT 和故障记录（阶段 1），初始化失败时先停在这里，调试器能直接看到位置；
 * 停在这里时没有任何任务运行，不会给电机发指令 */
static void halt_on_init_failure(void)
{
    for (;;)
    {
    }
}

/** @return false：有任务创建失败，失败的任务名已记日志 */
static bool create_tasks(void)
{
    for (uint32_t i = 0u; i < task_table_count; i++)
    {
        const TaskTableEntry *t = &task_table[i];
        if (!rm_task_create(t->task, t->name, t->entry, t->arg, t->priority, t->stack,
                            t->stack_words))
        {
            RM_LOG_E("create task %s failed", t->name);
            return false;
        }
    }
    return true;
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

    /* 读故障记录、board_init、参数在阶段 1 加入 */

    if (!objects_init()) /* 3. 这台车的全部对象（objects.c） */
    {
        RM_LOG_E("robot init failed");
        halt_on_init_failure();
    }
    if (!create_tasks()) /* 4. 按这台车的任务表创建全部任务 */
    {
        halt_on_init_failure();
    }
    RM_LOG_I("starting scheduler");
}

void startup_task(void *argument)
{
    (void)argument;
    /* 设备自检、硬件看门狗在阶段 1 加入 */
    safety_gate_set_system_ready(&safety_gate); /* 5. 允许解锁 */
    RM_LOG_I("startup done");
    rm_task_delete_self();
}
