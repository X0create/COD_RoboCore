/**
 * @file    robot.c
 * @brief   新兵种的样板：阶段 0 只有一个心跳任务
 * @note    心跳任务驱动状态灯，每秒通过 RTT 打印一次，同时统计（阶段 0 的上板验证手段，见 docs/VERIFICATION_TODO.md）：
 *          - DBUS 串口收到的字节数：接上 DR16 并打开遥控器后约 1300 字节/秒（18 字节 × 约 71 帧）；
 *          - CAN1 上 DJI 电机反馈帧（0x201–0x204）的帧数：一台电调上电时约 1000 帧/秒。
 */
#include "robot.h"
#include "comm_rx.h"

#include "platform/can.h"
#include "platform/status_led.h"
#include "platform/uart.h"

#include "core/log/log.h"
#include "core/os/os.h"

/* FreeRTOS 优先级，数字越大越高；本兵种全部任务的优先级只写在这里 */
enum
{
    PRIORITY_HEARTBEAT = 1,
    PRIORITY_COMM_RX = 3, /* 高于控制以外的任务，保证反馈帧及时取走（《架构设计》任务划分） */
};

#define HEARTBEAT_STEP_MS     25u
#define HEARTBEAT_STEPS       40u /* 40 × 25 ms = 1 s 一拍 */
#define HEARTBEAT_STACK_WORDS 256u
#define LED_GREEN_LEVEL       0x20u /* WS2812 满亮度很刺眼，1/8 亮度足够看清 */

/* DR16 接收机接 UART5（接线沿用 COD-H7-Template，《架构设计》“阶段 1 清单”） */
#define DBUS_UART UART_5

static RmTask heartbeat_task;
static StackType_t heartbeat_stack[HEARTBEAT_STACK_WORDS];

/* CAN1 上 DJI 电机反馈帧计数：comm_rx 任务里累加，心跳任务每秒读一次（32 位读写是原子的，只用于调试统计） */
static volatile uint32_t dji_feedback_frames;

static void count_dji_feedback(const CanFrame *frame, void *ctx)
{
    (void)frame;
    (void)ctx;
    dji_feedback_frames++;
}

/*
 * 绿色每拍闪两下表示正常：0–50 ms 亮一次，200–225 ms 再亮一次，其余时间灭。
 * 两次亮的时长不同（50 ms、25 ms，用户 2026-09-28 指定），一长一短容易和其他闪烁码区分。
 */
static bool led_on_at(uint32_t step)
{
    return step < 2u || step == 8u;
}

/* 取走本步收到的全部字节，返回字节数（阶段 0 只统计，不解析） */
static uint32_t drain_dbus(void)
{
    uint8_t chunk[64];
    uint32_t total = 0u;
    uint32_t n;
    while ((n = uart_read(DBUS_UART, chunk, sizeof(chunk))) > 0u)
    {
        total += n;
    }
    return total;
}

static void heartbeat_entry(void *arg)
{
    (void)arg;
    uint32_t beat = 0u;
    uint32_t step = 0u;
    uint32_t dbus_bytes = 0u;
    uint32_t last_can_frames = 0u;

    /* 接收在调度器启动后打开（《架构设计》运行时契约第 1 节）；这里不需要通知，每 25 ms 轮询一次 */
    const bool dbus_ok = uart_rx_start(DBUS_UART, NULL, NULL);
    if (!dbus_ok)
    {
        RM_LOG_E("dbus uart start failed");
    }

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        rm_status_led_set(0u, led_on_at(step) ? LED_GREEN_LEVEL : 0u, 0u);
        if (dbus_ok)
        {
            dbus_bytes += drain_dbus();
        }

        if (step == 0u)
        {
            /* 每次打印都会读一次时间，同时保证 DWT 扩展计数至少每 7.8 s 更新一次（time.h 的 @pre） */
            const uint32_t can_frames = dji_feedback_frames;
            RM_LOG_I("alive %u, dbus %u B/s, can1 %u frames/s", (unsigned)beat,
                     (unsigned)dbus_bytes, (unsigned)(can_frames - last_can_frames));
            beat++;
            dbus_bytes = 0u;
            last_can_frames = can_frames;
        }
        step = (step + 1u) % HEARTBEAT_STEPS;
        rm_task_delay_until(&last_wake, HEARTBEAT_STEP_MS);
    }
}

bool robot_init(void)
{
    /* DJI 电机 1–4 号的反馈 ID（附录 A.2）；阶段 0 只计数，电机驱动在移植第 5 步加入 */
    if (!can_subscribe_range(CAN_BUS_1, 0x201u, 0x204u, count_dji_feedback, NULL))
    {
        RM_LOG_E("can1 subscribe failed");
        return false;
    }
    return true;
}

void robot_create_tasks(void)
{
    comm_rx_create_task(PRIORITY_COMM_RX);
    if (!rm_task_create(&heartbeat_task, "heartbeat", heartbeat_entry, NULL, PRIORITY_HEARTBEAT,
                        heartbeat_stack, HEARTBEAT_STACK_WORDS))
    {
        RM_LOG_E("create heartbeat task failed");
    }
}
