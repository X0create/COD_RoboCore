/**
 * @file    comm_rx_task.c
 * @brief   comm_rx 任务，见 comm_rx_task.h
 */
#include "comm_rx_task.h"

#include "04_core/log/log.h"
#include "05_platform/can.h"
#include "05_platform/time.h"
#include "05_platform/usb_cdc.h"

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

RmTask comm_rx_task;
static UartSlot uarts[COMM_RX_MAX_UARTS];
static uint32_t uart_count;
static CommRxUartHandler usb_handler; /* 与串口共用同一种回调形式 */
static void *usb_ctx;
static bool usb_started;

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

void comm_rx_set_usb(CommRxUartHandler handler, void *ctx)
{
    usb_handler = handler;
    usb_ctx = ctx;
}

/* can_start() / uart_rx_start() / usb_cdc_start() 的通知回调：在中断里唤醒本任务。ctx 未使用 */
static void notify_from_isr(void *ctx)
{
    (void)ctx;
    rm_task_notify_from_isr(&comm_rx_task);
}

void comm_rx_start(void)
{
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        if (!can_start((CanBusId)bus, notify_from_isr, NULL))
        {
            RM_LOG_E("can%d start failed", bus + 1);
        }
    }
    for (uint32_t i = 0u; i < uart_count; i++)
    {
        uarts[i].started = uart_rx_start(uarts[i].port, notify_from_isr, NULL);
        if (!uarts[i].started)
        {
            RM_LOG_E("uart%d start failed", (int)uarts[i].port + 1);
        }
    }
    if (usb_handler != NULL)
    {
        usb_started = usb_cdc_start(notify_from_isr, NULL);
        if (!usb_started)
        {
            RM_LOG_E("usb cdc start failed");
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

static void drain_usb(void)
{
    uint8_t chunk[64];
    uint32_t n;
    while ((n = usb_cdc_read(chunk, sizeof(chunk))) > 0u)
    {
        usb_handler(chunk, n, rm_time_now_us(), usb_ctx);
    }
}

void comm_rx_task_entry(void *arg)
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
        if (usb_started)
        {
            drain_usb();
        }
    }
}
