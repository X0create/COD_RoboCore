/**
 * @file    robot.c
 * @brief   步兵（第一版：只有底盘）：有哪些对象、怎么初始化、有哪些任务
 * @note    本目录一个任务一个文件（相当于老模板的 Application/Task/）：
 *            control_task.c    每 1 ms：读输入 → 安全门 → 底盘 → 发送
 *            ins_task.c        每 1 ms：IMU 姿态解算
 *            heartbeat_task.c  状态灯、蜂鸣器、RTT 打印
 *          comm_rx（收 CAN / 串口）和 daemon（上线 / 离线报告）两个任务各兵种相同，在 robots/common/。
 *          调用关系总图见 docs/CALL_FLOW.md。
 */
#include "robot.h"

#include "comm_rx.h"
#include "config.h"
#include "core/log/log.h"
#include "core/os/os.h"
#include "daemon.h"
#include "objects.h"
#include "platform/adc.h"
#include "tasks.h"

#define DBUS_UART UART_5 /* DR16 接收机（接线沿用 COD-H7-Template） */

/* ---------------- 话题：谁发布、谁读取 ---------------- */
RcStateTopic rc_state;   /* 发布：dr16  读取：control、heartbeat */
ImuStateTopic imu_state; /* 发布：ins   读取：control、heartbeat */

/* ---------------- 设备 ---------------- */
Dr16 dr16;

/* 四个驱动轮：顺序 = 轮 0–3（左前、左后、右后、右前），ID 按实车接线改 */
#define WHEEL(wheel_name, esc_id)                                                                  \
    {                                                                                              \
        .name = (wheel_name), .type = MOTOR_M3508, .can_bus = INFANTRY_WHEEL_CAN_BUS,              \
        .id = (esc_id), .direction = INFANTRY_WHEEL_DIRECTION, .gear_ratio = DJI_M3508_GEAR_RATIO, \
        .stop_action = SAFE_ACTION_ZERO_TORQUE                                                     \
    }
static const MotorConfig wheel_config[CHASSIS_WHEELS] = {
    WHEEL("wheel_lf", 1u),
    WHEEL("wheel_lb", 2u),
    WHEEL("wheel_rb", 3u),
    WHEEL("wheel_rf", 4u),
};
Motor wheel_motor[CHASSIS_WHEELS];
MotorGroup motors;

/* ---------------- 子系统与安全门 ---------------- */
SafetyGate gate;
static const ChassisConfig chassis_config = INFANTRY_CHASSIS_CONFIG;
Chassis chassis;
static const InsConfig ins_config = { .install_rotation = INFANTRY_IMU_INSTALL_ROTATION };
Ins ins;

/* ================================================================== */
/* 初始化                                                              */
/* ================================================================== */

/* comm_rx 任务把 UART5 收到的字节交给这里 */
static void on_dbus_bytes(const uint8_t *data, uint32_t len, uint64_t now_us, void *ctx)
{
    (void)ctx;
    dr16_on_bytes(&dr16, data, len, now_us);
}

bool robot_init(void)
{
    if (!dr16_init(&dr16, &rc_state) || !comm_rx_add_uart(DBUS_UART, on_dbus_bytes, NULL))
    {
        RM_LOG_E("dr16 init failed");
        return false;
    }

    Motor *drive[CHASSIS_WHEELS];
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        const Motor *conflict;
        if (!motor_init(&wheel_motor[i], &wheel_config[i], &motors, &conflict))
        {
            RM_LOG_E("motor %s init failed%s%s", wheel_config[i].name,
                     conflict != NULL ? ": id conflict with " : "",
                     conflict != NULL ? conflict->cfg->name : "");
            return false;
        }
        drive[i] = &wheel_motor[i];
    }
    if (!chassis_init(&chassis, &chassis_config, drive, NULL))
    {
        RM_LOG_E("chassis init failed: motor without torque command");
        return false;
    }

    if (!ins_init(&ins, &ins_config, &imu_state))
    {
        RM_LOG_E("ins init failed");
        return false;
    }
    safety_gate_init(&gate, INFANTRY_ARM_SWITCH);
    return true;
}

void robot_start(void)
{
    if (!adc_start())
    {
        RM_LOG_E("adc start failed"); /* 只影响低电量提示，不阻止解锁 */
    }
    safety_gate_set_system_ready(&gate);
}

/* ================================================================== */
/* 任务表（相当于老模板 Core/Src/freertos.c 里的任务列表）               */
/* ================================================================== */

/*
 * | 任务      | 文件                   | 优先级 | 栈    | 周期              |
 * | --------- | ---------------------- | ------ | ----- | ----------------- |
 * | ins       | ins_task.c             | 5 最高 | 4 KB  | 1 ms              |
 * | comm_rx   | common/comm_rx.c       | 4      | 2 KB  | 收到 CAN / 串口就运行 |
 * | control   | control_task.c         | 3      | 4 KB  | 1 ms              |
 * | daemon    | common/daemon.c        | 2      | 1 KB  | 10 ms             |
 * | heartbeat | heartbeat_task.c       | 1      | 1 KB  | 25 ms             |
 *
 * 优先级数字越大越高：ins 最高，IMU 采样时刻要准；comm_rx 高于 control，控制周期读到最新反馈。
 * ins 的栈：EKF 的矩阵运算在栈上有临时变量，按预算表给 4 KB。comm_rx、daemon 的栈在各自文件里。
 */
enum
{
    PRIORITY_HEARTBEAT = 1,
    PRIORITY_DAEMON = 2,
    PRIORITY_CONTROL = 3,
    PRIORITY_COMM_RX = 4,
    PRIORITY_INS = 5,
};

static RmTask ins_task, control_task, heartbeat_task;
static StackType_t ins_stack[1024], control_stack[1024], heartbeat_stack[256];

#define STACK_WORDS(stack) ((uint32_t)(sizeof(stack) / sizeof((stack)[0])))

static void create_task(RmTask *task, const char *name, RmTaskEntry entry, uint32_t priority,
                        StackType_t *stack, uint32_t stack_words)
{
    if (!rm_task_create(task, name, entry, NULL, priority, stack, stack_words))
    {
        RM_LOG_E("create %s task failed", name);
    }
}

void robot_create_tasks(void)
{
    create_task(&ins_task, "ins", ins_task_entry, PRIORITY_INS, ins_stack, STACK_WORDS(ins_stack));
    comm_rx_create_task(PRIORITY_COMM_RX);
    create_task(&control_task, "control", control_task_entry, PRIORITY_CONTROL, control_stack,
                STACK_WORDS(control_stack));
    daemon_create_task(PRIORITY_DAEMON);
    create_task(&heartbeat_task, "heartbeat", heartbeat_task_entry, PRIORITY_HEARTBEAT,
                heartbeat_stack, STACK_WORDS(heartbeat_stack));
}
