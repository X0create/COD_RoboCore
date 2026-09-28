/**
 * @file    os.c
 * @brief   FreeRTOS 薄封装，见 os.h
 */
#include "os.h"

bool rm_task_create(RmTask *task, const char *name, RmTaskEntry entry, void *arg, uint32_t priority,
                    StackType_t *stack, uint32_t stack_words)
{
    if (priority >= (uint32_t)configMAX_PRIORITIES)
    {
        return false;
    }
    task->handle =
        xTaskCreateStatic(entry, name, stack_words, arg, (UBaseType_t)priority, stack, &task->tcb);
    return task->handle != NULL;
}

RmTaskPeriod rm_task_period_start(void)
{
    return xTaskGetTickCount();
}

void rm_task_delay_until(RmTaskPeriod *last_wake, uint32_t period_ms)
{
    (void)xTaskDelayUntil(last_wake, pdMS_TO_TICKS(period_ms));
}

void rm_task_delete_self(void)
{
    vTaskDelete(NULL);
}

bool rm_task_wait_notify(uint32_t timeout_ms)
{
    return ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) > 0u;
}

void rm_task_notify_from_isr(RmTask *task)
{
    BaseType_t higher_priority_woken = pdFALSE;
    vTaskNotifyGiveFromISR(task->handle, &higher_priority_woken);
    /* 被唤醒的任务优先级更高时，中断返回后立刻切换过去，不用等下一个 tick */
    portYIELD_FROM_ISR(higher_priority_woken);
}
