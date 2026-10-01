/**
 * @file    comm_rx_common.c
 * @brief   接收的公共部分，见 comm_rx_common.h
 */
#include "comm_rx_common.h"

#include "04_core/log/log.h"
#include "05_platform/can/can.h"
#include "05_platform/time/time.h"
#include "05_platform/usb_cdc/usb_cdc.h"

/* 没有通知时也每隔这么久检查一次，防止某次通知丢失后数据积压 */
#define COMM_RX_IDLE_MS 10u
/* 同一路 bus-off 恢复至少间隔 100 ms（《架构设计》“发送队列满了怎么办”） */
#define CAN_RECOVER_PERIOD_US 100000u

RmTask comm_rx_task;

/* can_start() / uart_rx_start() / usb_cdc_start() 的通知回调：在中断里唤醒 comm_rx_task。ctx 未使用 */
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

/* bus-off 后 FDCAN 不会自己回到总线，这路上的电机全部离线（机构停）。这里负责把它拉回来 */
void comm_rx_recover_bus_off(void)
{
    const uint64_t now_us = rm_time_now_us();
    static uint64_t last_try_us[CAN_BUS_COUNT];
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        if (!can_is_bus_off((CanBusId)bus) || now_us - last_try_us[bus] < CAN_RECOVER_PERIOD_US)
        {
            continue;
        }
        last_try_us[bus] = now_us;
        RM_LOG_W("can%d bus-off, restarting", bus + 1);
        can_recover((CanBusId)bus);
    }
}
