/**
 * @file    comm_rx_task.c
 * @brief   comm_rx_task（收到数据就运行）：打开接收 → 等中断唤醒 → 把收到的数据交给对应的解析器
 * @note    为什么不在中断里直接解析：中断要尽量短，否则会耽误别的中断和任务。所以中断只做两件事——
 *          把收到的字节 / 帧存进缓冲区、唤醒本任务；解析、校验、保存都在本任务里做。
 *          完整的接收流程都在这一个文件：中断回调、打开接收、读取与分派、CAN bus-off 恢复。
 *          加一个新的接收设备（比如裁判系统串口）：在 comm_rx_task_entry() 里打开它的接收，
 *          再在循环里照 DR16 的写法读出字节、交给它的解析函数。
 *          接线一览：
 *            CAN       全部电机的反馈 → 电机组 motors（总线和 ID 在 robot.h 的电机表，如 wheel_config）
 *                      超级电容的反馈 → supercap（总线和 ID 在 robot.h 的 supercap_config）
 *            UART5     DR16 遥控接收机 → dr16（保存最新一帧，dr16_read() 读）
 */
#include "01_applic/tasks/tasks.h"

#include "01_applic/config/robot.h"
#include "04_core/log/log.h"
#include "05_platform/can/can.h"
#include "05_platform/time/time.h"
#include "05_platform/uart/uart.h"

#define DBUS_UART UART_5 /* DR16 接收机 */

/* 没有通知时也每隔这么久检查一次，防止某次通知丢失后数据积压 */
#define COMM_RX_IDLE_MS 10u
/* 同一路 bus-off 恢复至少间隔 100 ms：总线真有故障（短路、没接终端电阻）时，避免不停重启刷屏 */
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

/*
 * bus-off：CAN 控制器连续发送出错太多次后，自己把自己从总线上断开（常见原因：线松了、接错、没终端电阻）。
 * 断开后 FDCAN 不会自己回到总线，这路上的电机全部离线（用到它们的机构停）。这里负责把它拉回来。
 */
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
    if (!uart_rx_start(DBUS_UART, notify_from_isr,
                       NULL)) /* DMA 接收；失败时读这个串口始终得到 0 字节 */
    {
        RM_LOG_E("uart5 (dr16) start failed");
    }

    CanFrame frame;
    uint8_t buf[64];
    uint32_t n;
    uint64_t rx_us;
    for (;;)
    {
        (void)rm_task_wait_notify(COMM_RX_IDLE_MS); /* 等中断通知“有新数据”，最多 10 ms */

        /* CAN → 电机、电容：每帧先交给电机组，总线和反馈 ID 都对上的电机才收（motor_group_receive）；
         * 不是电机的再交给电容。加电机不用改这里：robot.c 里 motor_init(..., &motors, ...) 就进了组；
         * 加别的 CAN 设备时照电容的写法，在后面再接一个 */
        for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
        {
            while (can_read((CanBusId)bus, &frame))
            {
                if (!motor_group_receive(&motors, (CanBusId)bus, &frame))
                {
                    (void)supercap_receive(&supercap, (CanBusId)bus, &frame);
                }
            }
        }
        recover_bus_off(); /* bus-off 的总线每 100 ms 重启一次 */

        /* UART5 → DR16：凑满 18 字节一帧 → 校验 → 保存为最新一帧。
         * rx_us 是串口中断收到这批字节的时刻，不是现在：本任务被耽误时，积压的旧帧不会被当成刚收到的，
         * 遥控掉线判断才准确 */
        while ((n = uart_read(DBUS_UART, buf, sizeof(buf), &rx_us)) > 0u)
        {
            dr16_on_bytes(&dr16, buf, n, rx_us);
        }
    }
}
