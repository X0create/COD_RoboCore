/**
 * @file    comm_rx.c
 * @brief   comm_rx 任务，见 comm_rx.h
 */
#include "comm_rx.h"

#include "core/log/log.h"
#include "core/os/os.h"
#include "platform/can.h"
#include "platform/time.h"

#define COMM_RX_STACK_WORDS 512u
/* 没有通知时也每隔这么久检查一次，防止某次通知丢失后数据积压 */
#define COMM_RX_IDLE_MS 10u
/* 最多几个串口由本任务解析（DR16、裁判系统、图传……） */
#define COMM_RX_MAX_UARTS 4u

typedef struct
{
    UartPort port;
    CommRxUartHandler handler;
    void *ctx;
    bool started;
} UartSlot;

static RmTask comm_rx_task;
static StackType_t comm_rx_stack[COMM_RX_STACK_WORDS];
static volatile bool task_created;
static UartSlot uarts[COMM_RX_MAX_UARTS];
static uint32_t uart_count;

bool comm_rx_add_uart(UartPort port, CommRxUartHandler handler, void *ctx)
{
    if (uart_count >= COMM_RX_MAX_UARTS)
    {
        return false;
    }
    for (uint32_t i = 0u; i < uart_count; i++)
    {
        if (uarts[i].port == port)
        {
            return false;
        }
    }
    uarts[uart_count++] = (UartSlot){ .port = port, .handler = handler, .ctx = ctx };
    return true;
}

void comm_rx_start_uarts(void)
{
    for (uint32_t i = 0u; i < uart_count; i++)
    {
        uarts[i].started = uart_rx_start(uarts[i].port, comm_rx_notify_from_isr, NULL);
        if (!uarts[i].started)
        {
            RM_LOG_E("uart%d start failed", (int)uarts[i].port + 1);
        }
    }
}

/* 把一个串口上次以来收到的字节全部交给它的解析者 */
static void drain_uart(const UartSlot *slot)
{
    uint8_t chunk[64];
    uint32_t n;
    while ((n = uart_read(slot->port, chunk, sizeof(chunk))) > 0u)
    {
        slot->handler(chunk, n, rm_time_now_us(), slot->ctx);
    }
}

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
        for (uint32_t i = 0u; i < uart_count; i++)
        {
            if (uarts[i].started)
            {
                drain_uart(&uarts[i]);
            }
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
