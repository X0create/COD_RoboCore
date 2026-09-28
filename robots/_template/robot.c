/**
 * @file    robot.c
 * @brief   新兵种的样板：DR16 遥控 + 一台 M3508（只收反馈）+ 心跳任务
 * @note    心跳任务驱动状态灯，每秒通过 RTT 打印一次（上板验证手段，见 docs/VERIFICATION_TODO.md）：
 *          - 遥控：在线时打印 5 个通道、两个拨杆和丢弃的坏帧数；离线时打印 “rc lost”；
 *          - M3508（CAN1，ID 1，接线沿用 COD-H7-Template）：在线时打印角度、速度、力矩估计、温度；
 *          上线 / 离线的变化由 daemon 任务另外打印。
 *          **本样板不发送任何电机指令**：motor_group_flush() 要等第 6 步的 control 任务才调用。
 */
#include "robot.h"

#include "comm_rx.h"
#include "core/log/log.h"
#include "core/os/os.h"
#include "daemon.h"
#include "devices/motor/motor.h"
#include "devices/motor/motor_group.h"
#include "devices/remote/dr16.h"
#include "msgs/rc_state.h"
#include "platform/can.h"
#include "platform/status_led.h"
#include "platform/uart.h"

/* FreeRTOS 优先级，数字越大越高；本兵种全部任务的优先级只写在这里 */
enum
{
    PRIORITY_HEARTBEAT = 1,
    PRIORITY_DAEMON = 2,
    PRIORITY_COMM_RX = 3, /* 高于控制以外的任务，保证反馈帧及时取走（《架构设计》任务划分） */
};

#define HEARTBEAT_STEP_MS     25u
#define HEARTBEAT_STEPS       40u /* 40 × 25 ms = 1 s 一拍 */
#define HEARTBEAT_STACK_WORDS 256u
#define LED_GREEN_LEVEL       0x20u /* WS2812 满亮度很刺眼，1/8 亮度足够看清 */

/* DR16 接收机接 UART5（接线沿用 COD-H7-Template，《架构设计》“阶段 1 清单”） */
#define DBUS_UART UART_5

/* 话题实例 */
static RcStateTopic rc_state; /* 发布：dr16    读取：心跳任务（以后是 command、安全门） */

static Dr16 dr16;

/* 电机：配置是 const，运行状态单独存放（《架构设计》“配置和运行状态分开存放”） */
static const MotorConfig chassis_motor_config = {
    .name = "m3508_1",
    .type = MOTOR_M3508,
    .can_bus = CAN_BUS_1,
    .id = 1u,
    .direction = 1,
    .gear_ratio = DJI_M3508_GEAR_RATIO,
    .stop_action = SAFE_ACTION_ZERO_TORQUE,
};
static Motor chassis_motor;
static MotorGroup motors;
static RmTask heartbeat_task;
static StackType_t heartbeat_stack[HEARTBEAT_STACK_WORDS];

static void on_dbus_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    dr16_on_bytes((Dr16 *)ctx, data, len, now_us);
}

/*
 * 绿色每拍闪两下表示正常：0–50 ms 亮一次，200–225 ms 再亮一次，其余时间灭。
 * 两次亮的时长不同（50 ms、25 ms，用户 2026-09-28 指定），一长一短容易和其他闪烁码区分。
 */
static bool led_on_at(uint32_t step)
{
    return step < 2u || step == 8u;
}

static void log_rc(void)
{
    RcState rc;
    if (!rc_state_read(&rc_state, &rc, RC_LOST_TIMEOUT_MS))
    {
        RM_LOG_I("rc lost (bad frames %u)", (unsigned)dr16.bad_frames);
        return;
    }
    RM_LOG_I("rc ch %d %d %d %d %d, sw %d %d, bad frames %u", rc.ch[0], rc.ch[1], rc.ch[2],
             rc.ch[3], rc.ch[4], (int)rc.sw[0], (int)rc.sw[1], (unsigned)dr16.bad_frames);
}

/* 浮点用整数打印（RTT 的 printf 不支持 %f）：毫弧度、毫弧度/秒、毫牛·米 */
static void log_motor(void)
{
    MotorFeedback fb;
    if (!motor_read_feedback(&chassis_motor, &fb))
    {
        RM_LOG_I("m3508_1 offline");
        return;
    }
    RM_LOG_I("m3508_1 angle %d mrad, speed %d mrad/s, torque %d mNm, %d C",
             (int)(fb.angle_rad * 1000.0f), (int)(fb.speed_rad_s * 1000.0f),
             (int)(fb.torque_nm * 1000.0f), (int)fb.temperature_c);
}

static void heartbeat_entry(void *arg)
{
    (void)arg;
    uint32_t beat = 0u;
    uint32_t step = 0u;

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        rm_status_led_set(0u, led_on_at(step) ? LED_GREEN_LEVEL : 0u, 0u);

        if (step == 0u)
        {
            /* 每次打印都会读一次时间，同时保证 DWT 扩展计数至少每 7.8 s 更新一次（time.h 的 @pre） */
            RM_LOG_I("alive %u", (unsigned)beat);
            log_rc();
            log_motor();
            beat++;
        }

        step = (step + 1u) % HEARTBEAT_STEPS;
        rm_task_delay_until(&last_wake, HEARTBEAT_STEP_MS);
    }
}

bool robot_init(void)
{
    if (!dr16_init(&dr16, &rc_state) || !comm_rx_add_uart(DBUS_UART, on_dbus_bytes, &dr16))
    {
        RM_LOG_E("dr16 init failed");
        return false;
    }
    const Motor *conflict;
    if (!motor_init(&chassis_motor, &chassis_motor_config, &motors, &conflict))
    {
        RM_LOG_E("motor %s init failed%s%s", chassis_motor_config.name,
                 conflict != NULL ? ": id conflict with " : "",
                 conflict != NULL ? conflict->cfg->name : "");
        return false;
    }
    return true;
}

void robot_create_tasks(void)
{
    comm_rx_create_task(PRIORITY_COMM_RX);
    daemon_create_task(PRIORITY_DAEMON);
    if (!rm_task_create(&heartbeat_task, "heartbeat", heartbeat_entry, NULL, PRIORITY_HEARTBEAT,
                        heartbeat_stack, HEARTBEAT_STACK_WORDS))
    {
        RM_LOG_E("create heartbeat task failed");
    }
}
