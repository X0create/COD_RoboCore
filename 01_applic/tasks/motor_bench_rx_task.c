/** 单电机 CAN1、DR16、USB 接收与记录发送；不打开其他总线。 */
#include "01_applic/config/motor_bench_robot.h"
#include "01_applic/modules/motor_bench/telemetry/sysid/sysid.h"
#include "01_applic/tasks/tasks.h"
#include "04_core/log/log.h"
#include "05_platform/time/time.h"
#include "05_platform/uart/uart.h"
#include "05_platform/usb_cdc/usb_cdc.h"
#include <board.h>

RmTask comm_rx_task;
static void notify(void *context)
{
    (void)context;
    rm_task_notify_from_isr(&comm_rx_task);
}

void motor_bench_rx_task_entry(void *argument)
{
    (void)argument;
    if (!can_start(bench_motor_config.can_bus, notify, NULL)
        || !uart_rx_start(BOARD_DBUS_UART, notify, NULL) || !usb_cdc_start(notify, NULL))
    {
        RM_LOG_E("motor bench CAN/DBUS/USB start failed; receiver stopped");
        rm_task_delete_self();
        return;
    }
    uint64_t last_recover_us = 0u;
    uint8_t reply[MOTOR_BENCH_MESSAGE_SIZE];
    bool reply_pending = false;
    for (;;)
    {
        (void)rm_task_wait_notify(1u);
        CanFrame frame;
        while (can_read(bench_motor_config.can_bus, &frame))
        {
            (void)motor_group_receive(&bench_motors, bench_motor_config.can_bus, &frame);
        }
        uint8_t bytes[64];
        uint64_t stamp_us;
        uint32_t n;
        while ((n = uart_read(BOARD_DBUS_UART, bytes, sizeof(bytes), &stamp_us)) != 0u)
        {
            dr16_on_bytes(&bench_rc, bytes, n, stamp_us);
        }
        while ((n = usb_cdc_read(bytes, sizeof(bytes))) != 0u)
        {
            sysid_receive(bytes, n);
        }
        if (!reply_pending)
        {
            reply_pending = motor_bench_link_reply(&bench_link, reply);
        }
        if (reply_pending && sysid_reply(MOTOR_BENCH_REPLY, reply, sizeof(reply)))
        {
            reply_pending = false;
        }
        sysid_poll(); /* 唯一 USB 发送方；运动仍须单独的 START 或 DR16 拨杆。 */
        const uint64_t now_us = rm_time_now_us();
        if (can_is_bus_off(bench_motor_config.can_bus) && now_us - last_recover_us >= 100000u)
        {
            last_recover_us = now_us;
            can_recover(bench_motor_config.can_bus);
        }
    }
}
