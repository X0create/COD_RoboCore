/**
 * @file    robot_comm_rx_task.c
 * @brief   这台车的 comm_rx_task（收到数据就运行）：打开接收，然后把收到的数据交给对应的解析器
 * @note    相当于老模板 bsp_can.c / bsp_uart.c 里的接收回调，但解析放在任务里，中断只收数据并唤醒本任务。
 *          接线一览（接线沿用 COD-H7-Template）：
 *            CAN       四个轮子电机的反馈（总线和 ID 在 robot_config.h 的 wheel_config）
 *            UART5     DR16 遥控接收机 → dr16 → 发布 rc_state
 */
#include "robot.h"

#include "01_applic/system/comm_rx_common.h"
#include "05_platform/can/can.h"
#include "05_platform/time/time.h"
#include "05_platform/uart/uart.h"

#define DBUS_UART UART_5 /* DR16 接收机 */

void comm_rx_task_entry(void *arg)
{
    (void)arg;
    comm_rx_start_can();
    comm_rx_start_uart(DBUS_UART);

    CanFrame frame;
    uint8_t buf[64];
    uint32_t n;
    for (;;)
    {
        comm_rx_wait(); /* 等中断通知“有新数据”，最多 10 ms */

        /* CAN → 四个轮子电机：每帧依次交给每个电机，总线和反馈 ID 都对上才收（motor_receive）；
         * 电机在哪路总线只写在 robot_config.h 的 wheel_config 里 */
        for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
        {
            while (can_read((CanBusId)bus, &frame))
            {
                for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
                {
                    if (motor_receive(&wheel_motor[i], (CanBusId)bus, &frame))
                    {
                        break;
                    }
                }
            }
        }
        comm_rx_recover_bus_off(); /* bus-off 的总线每 100 ms 重启一次 */

        /* UART5 → DR16：凑满 18 字节一帧 → 校验 → 发布 rc_state */
        while ((n = uart_read(DBUS_UART, buf, sizeof(buf))) > 0u)
        {
            dr16_on_bytes(&dr16, buf, n, rm_time_now_us());
        }
    }
}
