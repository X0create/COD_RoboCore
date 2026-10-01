/**
 * @file    comm_rx_task.c
 * @brief   台架验证固件的 comm_rx 任务（收到数据就运行）：打开接收，然后把收到的数据交给对应的解析器
 * @note    相当于老模板 bsp_can.c / bsp_uart.c 里的接收回调，但解析放在任务里，中断只收数据并唤醒本任务。
 *          接线一览：
 *            CAN1、CAN2  M3508（CAN1）、达妙 DM8009（CAN2）的反馈（每个电机在 motor_init() 里订阅自己的反馈 ID）
 *            UART5       DR16 遥控接收机 → dr16 → 发布 rc_state（接线沿用 COD-H7-Template）
 *            USART10     图传链路 → vt_link → 发布 vt_rc_state、kbm_state（921600，ADR 0036）
 *            USB         上位机 → vision_link 找 0x5A 帧并计数；收到的字节原样回发，用电脑串口助手验证通道
 *                        （视觉协议确定后去掉回发，ADR 0037）
 */
#include "robot.h"

#include "01_app/common/comm_rx.h"
#include "05_platform/time.h"
#include "05_platform/uart.h"
#include "05_platform/usb_cdc.h"

#define DBUS_UART    UART_5  /* DR16 接收机 */
#define VT_LINK_UART UART_10 /* 图传链路 */

void comm_rx_task_entry(void *arg)
{
    (void)arg;
    comm_rx_start_can();
    comm_rx_start_uart(DBUS_UART);
    comm_rx_start_uart(VT_LINK_UART);
    comm_rx_start_usb();

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

        /* USART10 → 图传链路：找帧 → 校验 → 发布 vt_rc_state、kbm_state */
        while ((n = uart_read(VT_LINK_UART, buf, sizeof(buf))) > 0u)
        {
            vt_link_on_bytes(&vt_link, buf, n);
        }

        /* USB → 视觉链路找帧，并原样回发（验证通道用；上一包还没发完时这段不回发） */
        while ((n = usb_cdc_read(buf, sizeof(buf))) > 0u)
        {
            vision_link_on_bytes(&vision_link, buf, n);
            usb_rx_bytes += n;
            if (!usb_cdc_write(buf, n))
            {
                usb_echo_dropped++;
            }
        }
    }
}
