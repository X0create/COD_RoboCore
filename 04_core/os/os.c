/**
 * @file    os.c
 * @brief   FreeRTOS 薄封装，见 os.h
 */
#include "os.h"

#include "05_platform/time/time.h"
#include "critical.h"
#include "delay.h"

bool rm_task_create(RmTask *task, const char *name, RmTaskEntry entry, void *arg, uint32_t priority,
                    StackType_t *stack, uint32_t stack_words)
{
    if (priority >= (uint32_t)
            configMAX_PRIORITIES) /* FreeRTOS 会悄悄把超范围的优先级截成最高，这里直接报错 */
    {
        return false;
    }
    /* 静态创建：任务控制块和栈都由调用方提供，不用堆 */
    task->handle =
        xTaskCreateStatic(entry, name, stack_words, arg, (UBaseType_t)priority, stack, &task->tcb);
    return task->handle != NULL;
}

/* 周期任务的起点：之后每次 rm_task_delay_until() 都从这个时刻往后数整周期，不会累积漂移 */
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

/* 等通知：收到过就立刻返回 true 并清零计数，超时返回 false */
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

void rm_critical_enter(void)
{
    taskENTER_CRITICAL();
}

void rm_critical_exit(void)
{
    taskEXIT_CRITICAL();
}

void rm_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* 忙等（不让出 CPU），只用于初始化时芯片要求的几微秒等待 */
void rm_delay_us(uint32_t us)
{
    const uint64_t end_us = rm_time_now_us() + us;
    while (rm_time_now_us() < end_us)
    {
    }
}
