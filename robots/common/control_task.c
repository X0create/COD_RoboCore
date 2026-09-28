/**
 * @file    control_task.c
 * @brief   control 任务，见 control_task.h
 */
#include "control_task.h"

#include "core/log/log.h"
#include "core/os/os.h"
#include "robot.h"

#define CONTROL_STACK_WORDS 1024u /* 4 KB（运行时契约第 4 节的预算） */

static RmTask control_task;
static StackType_t control_stack[CONTROL_STACK_WORDS];

static void control_entry(void *arg)
{
    (void)arg;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        robot_control_step();
        rm_task_delay_until(&last_wake, CONTROL_PERIOD_MS);
    }
}

void control_task_create(uint32_t priority)
{
    if (!rm_task_create(&control_task, "control", control_entry, NULL, priority, control_stack,
                        CONTROL_STACK_WORDS))
    {
        RM_LOG_E("create control task failed");
    }
}
