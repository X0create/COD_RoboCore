/**
 * @file    comm_rx_task.c
 * @brief   步兵的 comm_rx 任务（收到数据就运行）：打开接收，然后把收到的数据交给对应的解析器
 * @note    相当于老模板 bsp_can.c / bsp_uart.c 里的接收回调，但解析放在任务里，中断只收数据并唤醒本任务。
 *          接线一览（接线沿用 COD-H7-Template）：
 *            CAN1      四个轮子电机的反馈（每个电机在 motor_init() 里订阅自己的反馈 ID）
 *            UART5     DR16 遥控接收机 → dr16 → 发布 rc_state
 */
#include "robot.h"

#include "01_app/common/comm_rx.h"
#include "05_platform/time.h"
#include "05_platform/uart.h"

#define DBUS_UART UART_5 /* DR16 接收机 */

void comm_rx_task_entry(void *arg)
{
    (void)arg;
    comm_rx_start_can();
    comm_rx_start_uart(DBUS_UART);

    uint8_t buf[64];
    uint32_t n;
    for (;;)
    {
        comm_rx_wait(); /* 等中断通知“有新数据”，最多 10 ms */

        /* CAN：电机反馈（按 ID 交给对应电机解码、存反馈） */
        comm_rx_can_all();

        /* UART5 → DR16：凑满 18 字节一帧 → 校验 → 发布 rc_state */
        while ((n = uart_read(DBUS_UART, buf, sizeof(buf))) > 0u)
        {
            dr16_on_bytes(&dr16, buf, n, rm_time_now_us());
        }
    }
}
