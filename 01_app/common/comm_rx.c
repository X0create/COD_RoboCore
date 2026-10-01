/**
 * @file    comm_rx.c
 * @brief   接收的公共部分，见 comm_rx.h
 */
#include "comm_rx.h"

#include "04_core/log/log.h"
#include "05_platform/can.h"
#include "05_platform/usb_cdc.h"

/* 没有通知时也每隔这么久检查一次，防止某次通知丢失后数据积压 */
#define COMM_RX_IDLE_MS 10u

RmTask comm_rx_task;

/* can_start() / uart_rx_start() / usb_cdc_start() 的通知回调：在中断里唤醒 comm_rx 任务。ctx 未使用 */
static void notify_from_isr(void *ctx)
{
    (void)ctx;
    rm_task_notify_from_isr(&comm_rx_task);
}

void comm_rx_start_can(void)
{
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        if (!can_start((CanBusId)bus, notify_from_isr, NULL))
        {
            RM_LOG_E("can%d start failed", bus + 1);
        }
    }
}

void comm_rx_start_uart(UartPort port)
{
    if (!uart_rx_start(port, notify_from_isr, NULL))
    {
        RM_LOG_E("uart%d start failed", (int)port + 1);
    }
}

void comm_rx_start_usb(void)
{
    if (!usb_cdc_start(notify_from_isr, NULL))
    {
        RM_LOG_E("usb cdc start failed");
    }
}

void comm_rx_wait(void)
{
    (void)rm_task_wait_notify(COMM_RX_IDLE_MS);
}
