/** 单电机 CAN FD + DR16 接收，USB 日志发送；不打开其他 CAN 总线。 */
#include "01_applic/config/hand_guide_robot.h"
#include "01_applic/modules/hand_guide/telemetry/sysid/sysid.h"
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

void hand_guide_rx_task_entry(void *argument)
{
    (void)argument;
    if (!can_start(hand_motor_config.can_bus, notify, NULL)
        || !uart_rx_start(BOARD_DBUS_UART, notify, NULL) || !usb_cdc_start(notify, NULL))
    {
        RM_LOG_E("hand guide CAN/DBUS/USB start failed; receiver stopped");
        rm_task_delete_self(); /* 没有遥控/电机反馈，控制模块保持失能。 */
        return;
    }
    uint64_t last_recover_us = 0u;
    for (;;)
    {
        (void)rm_task_wait_notify(1u);
        CanFrame frame;
        while (can_read(hand_motor_config.can_bus, &frame))
        {
            (void)motor_group_receive(&hand_motors, hand_motor_config.can_bus, &frame);
        }
        uint8_t bytes[64];
        uint64_t stamp_us;
        uint32_t n;
        while ((n = uart_read(BOARD_DBUS_UART, bytes, sizeof(bytes), &stamp_us)) != 0u)
        {
            dr16_on_bytes(&hand_rc, bytes, n, stamp_us);
        }
        while ((n = usb_cdc_read(bytes, sizeof(bytes))) != 0u)
        {
            sysid_receive(bytes, n);
        }
        sysid_poll(); /* 唯一 USB 发送方；轴表为空，电脑不能发运动指令。 */
        const uint64_t now_us = rm_time_now_us();
        if (can_is_bus_off(hand_motor_config.can_bus) && now_us - last_recover_us >= 100000u)
        {
            last_recover_us = now_us;
            can_recover(hand_motor_config.can_bus); /* 模块仍锁定，不自动重新使能。 */
        }
    }
}
