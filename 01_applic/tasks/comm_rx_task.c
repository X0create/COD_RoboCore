/**
 * @file    comm_rx_task.c
 * @brief   comm_rx_task（收到数据就运行）：打开接收 → 等中断唤醒 → 把收到的数据交给对应的解析器
 * @note    相当于老模板 bsp_can.c / bsp_uart.c 里的接收回调，但解析放在任务里，中断只收数据并唤醒本任务。
 *          完整的接收流程都在这一个文件：中断回调、打开接收、读取与分派、CAN bus-off 恢复。
 *          接线一览（接线沿用 COD-H7-Template）：
 *            CAN       四个轮子电机的反馈（总线和 ID 在 robot_config.h 的 wheel_config）
 *            UART5     DR16 遥控接收机 → dr16（保存最新一帧，dr16_read() 读）
 */
#include "comm_rx_task.h"

#include "01_applic/robot/robot.h"
#include "04_core/log/log.h"
#include "05_platform/can/can.h"
#include "05_platform/time/time.h"
#include "05_platform/uart/uart.h"

#define DBUS_UART UART_5 /* DR16 接收机 */

/* 没有通知时也每隔这么久检查一次，防止某次通知丢失后数据积压 */
#define COMM_RX_IDLE_MS 10u
/* 同一路 bus-off 恢复至少间隔 100 ms（《架构设计》“发送队列满了怎么办”） */
#define CAN_RECOVER_PERIOD_US 100000u

RmTask comm_rx_task;

/* can_start() / uart_rx_start() 的通知回调：在中断里唤醒本任务。ctx 未使用 */
static void notify_from_isr(void *ctx)
{
    (void)ctx;
    rm_task_notify_from_isr(&comm_rx_task);
}

/* 打开每一路 CAN 的接收（全部标准帧）；失败记日志 */
static void start_can(void)
{
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        if (!can_start((CanBusId)bus, notify_from_isr, NULL))
        {
            RM_LOG_E("can%d start failed", bus + 1);
        }
    }
}

/* 打开一个串口的 DMA 接收；失败记日志，之后读这个串口始终得到 0 字节 */
static void start_uart(UartPort port)
{
    if (!uart_rx_start(port, notify_from_isr, NULL))
    {
        RM_LOG_E("uart%d start failed", (int)port + 1);
    }
}

/* bus-off 后 FDCAN 不会自己回到总线，这路上的电机全部离线（机构停）。这里负责把它拉回来 */
static void recover_bus_off(void)
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

void comm_rx_task_entry(void *arg)
{
    (void)arg;
    start_can();
    start_uart(DBUS_UART);

    CanFrame frame;
    uint8_t buf[64];
    uint32_t n;
    for (;;)
    {
        (void)rm_task_wait_notify(COMM_RX_IDLE_MS); /* 等中断通知“有新数据”，最多 10 ms */

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
        recover_bus_off(); /* bus-off 的总线每 100 ms 重启一次 */

        /* UART5 → DR16：凑满 18 字节一帧 → 校验 → 保存为最新一帧 */
        while ((n = uart_read(DBUS_UART, buf, sizeof(buf))) > 0u)
        {
            dr16_on_bytes(&dr16, buf, n, rm_time_now_us());
        }
    }
}
