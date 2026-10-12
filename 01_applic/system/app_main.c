/**
 * @file    app_main.c
 * @brief   上电顺序，见 app_main.h
 * @note    startup_task 必须和 app_main 放在同一个文件里：它在 CubeMX 生成代码里已有弱定义，
 *          单独放进静态库的另一个 .o 时链接器不会去取，弱定义的空函数就会被悄悄用上。
 */
#include "01_applic/system/app_main.h"

#include "01_applic/config/robot.h"
#include "01_applic/system/safety_gate.h"
#include "04_core/log/log.h"
#include "05_platform/iwdg/iwdg.h"
#include "05_platform/time/time.h"

/* 初始化失败时停在这里：调试器暂停后能直接看到停在哪一步（看 RTT 日志里最后一条错误）；
 * 停在这里时调度器还没启动、没有任何任务运行，不会给电机发指令 */
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
    const bool fault_reset = iwdg_caused_last_reset(); /* 0. 先读复位原因（读后清除） */
    const bool time_ok = rm_time_init();               /* 1. DWT 计时 */
    rm_log_init();                                     /* 2. RTT 日志 */
    if (!time_ok)
    {
        /* 时间基准不可用：RM_LOG_* 的时间戳要读 rm_time_now_us()，这里只能用不带时间戳的输出 */
        RM_LOG_E_NO_TIME("DWT cycle counter not running");
        halt_on_init_failure();
    }
    RM_LOG_I("COD RoboCore booting");

    /* 以后要加的上电步骤（读故障记录、读掉电保存的参数等）放在这里，robot_init() 之前 */

    if (!robot_init()) /* 3. 这台车的全部对象（robot.c） */
    {
        RM_LOG_E("robot init failed");
        halt_on_init_failure();
    }
    /* 上次是看门狗复位（程序卡死或致命故障后自动重启）：比赛中不能让车因为一次重启就要操作手重新解锁，
     * 所以遥控在线、拨杆不在“下”就直接恢复。普通上电仍要按顺序解锁 */
    if (fault_reset)
    {
        RM_LOG_W("restarted by watchdog: resume without re-arm");
        safety_gate_resume_after_fault(&safety_gate);
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
    /* 以后要加的启动后检查（设备自检等）放在这里，允许解锁之前 */
    safety_gate_set_system_ready(&safety_gate); /* 5. 允许解锁 */
    RM_LOG_I("startup done");
    rm_task_delete_self();
}
