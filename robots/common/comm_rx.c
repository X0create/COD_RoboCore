/**
 * @file    comm_rx.c
 * @brief   comm_rx 任务，见 comm_rx.h
 */
#include "comm_rx.h"

#include "platform/can.h"

#include "core/log/log.h"
#include "core/os/os.h"

#define COMM_RX_STACK_WORDS 512u
/* 没有通知时也每隔这么久检查一次，防止某次通知丢失后帧积压在环形缓冲里 */
#define COMM_RX_IDLE_MS 10u

static RmTask comm_rx_task;
static StackType_t comm_rx_stack[COMM_RX_STACK_WORDS];
static volatile bool task_created;

static void comm_rx_entry(void *arg)
{
    (void)arg;
    for (;;)
    {
        (void)rm_task_wait_notify(COMM_RX_IDLE_MS);
        for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
        {
            (void)can_dispatch((CanBusId)bus);
        }
    }
}

void comm_rx_create_task(uint32_t priority)
{
    task_created = rm_task_create(&comm_rx_task, "comm_rx", comm_rx_entry, NULL, priority,
                                  comm_rx_stack, COMM_RX_STACK_WORDS);
    if (!task_created)
    {
        RM_LOG_E("create comm_rx task failed");
    }
}

void comm_rx_notify_from_isr(void *ctx)
{
    (void)ctx;
    if (task_created)
    {
        rm_task_notify_from_isr(&comm_rx_task);
    }
}
