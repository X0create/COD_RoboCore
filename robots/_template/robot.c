/**
 * @file    robot.c
 * @brief   新兵种的样板：阶段 0 只有一个心跳任务，每 500 ms 通过 RTT 打印一次
 */
#include "robot.h"

#include "core/log/log.h"
#include "core/os/os.h"

/* FreeRTOS 优先级，数字越大越高；本兵种全部任务的优先级只写在这里 */
enum
{
    PRIORITY_HEARTBEAT = 1,
};

#define HEARTBEAT_PERIOD_MS   500u
#define HEARTBEAT_STACK_WORDS 256u

static RmTask heartbeat_task;
static StackType_t heartbeat_stack[HEARTBEAT_STACK_WORDS];

static void heartbeat_entry(void *arg)
{
    (void)arg;
    uint32_t beat = 0u;
    RmTaskPeriod last_wake = rm_task_period_start();

    for (;;)
    {
        /* 每次打印都会读一次时间，同时保证 DWT 扩展计数至少每 7.8 s 更新一次（time.h 的 @pre） */
        RM_LOG_I("alive %u", (unsigned)beat);
        beat++;
        rm_task_delay_until(&last_wake, HEARTBEAT_PERIOD_MS);
    }
}

bool robot_init(void)
{
    return true;
}

void robot_create_tasks(void)
{
    if (!rm_task_create(&heartbeat_task, "heartbeat", heartbeat_entry, NULL, PRIORITY_HEARTBEAT,
                        heartbeat_stack, HEARTBEAT_STACK_WORDS))
    {
        RM_LOG_E("create heartbeat task failed");
    }
}
