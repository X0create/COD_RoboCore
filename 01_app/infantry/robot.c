/**
 * @file    robot.c
 * @brief   步兵（第一版：只有底盘）：全部对象、上电顺序、任务表都在这一个文件里，从上往下读
 * @note    1. 对象定义（相当于老模板的全局变量），声明在 robot.h
 *          2. init_objects()：设备 → 底盘 → IMU → 安全门
 *          3. 任务表：5 个任务的优先级、栈、入口（相当于老模板 Core/Src/freertos.c 里的任务列表）
 *          4. app_main()：调度器启动前，由 CubeMX 的 freertos.c 调用
 *          5. startup_task()：调度器启动后第一个运行，打开 ADC、蜂鸣器，允许解锁
 *          任务本身一个任务一个文件：control_task.c、comm_rx_task.c、ins_task.c、heartbeat_task.c 在本目录；
 *          接线（哪个串口交给哪个解析器）在本目录的 comm_rx_task.c；detect_task.c 各兵种相同，在 01_app/common/。调用关系总图见 docs/CALL_FLOW.md。
 *          startup_task 必须和 app_main 放在同一个文件里：它在 CubeMX 生成代码里已有弱定义，
 *          单独放进静态库的另一个 .o 时链接器不会去取，弱定义的空函数就会被悄悄用上。
 */
#include "robot.h"

#include "01_app/common/comm_rx.h"
#include "01_app/common/detect_task.h"
#include "04_core/log/log.h"
#include "04_core/os/os.h"
#include "05_platform/adc.h"
#include "05_platform/time.h"
#include "config.h"

/* ================================================================== */
/* 1. 对象                                                             */
/* ================================================================== */

/* 话题：谁发布、谁读取 */
RcStateTopic rc_state;   /* 发布：dr16  读取：control、heartbeat */
ImuStateTopic imu_state; /* 发布：ins   读取：control、heartbeat */

/* 设备 */
Dr16 dr16;

/* 电机、底盘、电池、IMU 的参数都在 config.h 的配置表里（wheel_config、chassis_config …） */
Motor wheel_motor[CHASSIS_WHEELS];
MotorGroup motors;

Battery battery;
Buzzer buzzer;

/* 子系统与安全门 */
SafetyGate gate;
Chassis chassis;
Ins ins;

/* ================================================================== */
/* 2. 初始化对象（调度器启动前）                                         */
/* ================================================================== */

/** @return false：必需的设备或子系统初始化失败，原因已记日志 */
static bool init_objects(void)
{
    if (!dr16_init(&dr16, &rc_state))
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
    battery_init(&battery, &battery_config);
    safety_gate_init(&gate, arm_switch);
    return true;
}

/* ================================================================== */
/* 3. 任务表                                                           */
/* ================================================================== */

/*
 * 优先级数字越大越高：ins 最高，IMU 采样时刻要准；comm_rx 高于 control，控制周期读到最新反馈。
 * 改优先级：改下面表里的数字；改栈大小：改紧挨着的栈数组。startup 任务由 CubeMX 创建，不在表里。
 */
static StackType_t ins_stack[1024]; /* 4 KB：EKF 的矩阵运算在栈上有临时变量（预算表） */
static StackType_t comm_rx_stack[512];   /* 2 KB */
static StackType_t control_stack[1024];  /* 4 KB */
static StackType_t detect_stack[256];    /* 1 KB */
static StackType_t heartbeat_stack[256]; /* 1 KB */

static RmTask ins_task, control_task, detect_task,
    heartbeat_task; /* comm_rx_task 在 01_app/common/comm_rx.c（中断要用它唤醒任务） */

#define STACK_WORDS(stack) ((uint32_t)(sizeof(stack) / sizeof((stack)[0])))

typedef struct
{
    RmTask *task;
    const char *name;
    RmTaskEntry entry;
    uint32_t priority;
    StackType_t *stack;
    uint32_t stack_words;
} TaskDef;

/* clang-format off */
static const TaskDef task_table[] = {
    /* 任务            名字         入口                  优先级  栈                                          周期 */
    { &ins_task,       "ins",       ins_task_entry,       5u,  ins_stack,       STACK_WORDS(ins_stack)       }, /* 1 ms（ins_task.c） */
    { &comm_rx_task,   "comm_rx",   comm_rx_task_entry,   4u,  comm_rx_stack,   STACK_WORDS(comm_rx_stack)   }, /* 收到 CAN / 串口就运行（comm_rx_task.c） */
    { &control_task,   "control",   control_task_entry,   3u,  control_stack,   STACK_WORDS(control_stack)   }, /* 1 ms（control_task.c） */
    { &detect_task,    "detect",    detect_task_entry,    2u,  detect_stack,    STACK_WORDS(detect_stack)    }, /* 10 ms（01_app/common/detect_task.c） */
    { &heartbeat_task, "heartbeat", heartbeat_task_entry, 1u,  heartbeat_stack, STACK_WORDS(heartbeat_stack) }, /* 25 ms（heartbeat_task.c） */
};
/* clang-format on */

/** @return false：有任务创建失败，失败的任务名已记日志 */
static bool create_tasks(void)
{
    for (unsigned i = 0u; i < sizeof(task_table) / sizeof(task_table[0]); i++)
    {
        const TaskDef *t = &task_table[i];
        if (!rm_task_create(t->task, t->name, t->entry, NULL, t->priority, t->stack,
                            t->stack_words))
        {
            RM_LOG_E("create task %s failed", t->name);
            return false;
        }
    }
    return true;
}

/* ================================================================== */
/* 4. 上电（调度器启动前）                                               */
/* ================================================================== */

/* 阶段 0 还没有 RM_ASSERT 和故障记录（阶段 1），初始化失败时先停在这里，调试器能直接看到位置；
 * 停在这里时没有任何任务运行，不会给电机发指令 */
static void halt_on_init_failure(void)
{
    for (;;)
    {
    }
}

void app_main(void)
{
    const bool time_ok = rm_time_init(); /* DWT 计时 */
    rm_log_init();                       /* RTT 日志 */
    RM_LOG_I("COD RoboCore booting");
    if (!time_ok)
    {
        RM_LOG_E("DWT cycle counter not running");
        halt_on_init_failure();
    }

    /* 读故障记录、board_init、参数在阶段 1 加入 */

    if (!init_objects())
    {
        RM_LOG_E("init failed");
        halt_on_init_failure();
    }
    if (!create_tasks())
    {
        halt_on_init_failure();
    }
    RM_LOG_I("starting scheduler");
}

/* ================================================================== */
/* 5. startup 任务（调度器启动后第一个运行，完成后删除自己）               */
/* ================================================================== */

void startup_task(void *argument)
{
    (void)argument;

    if (!adc_start())
    {
        RM_LOG_E("adc start failed"); /* 只影响低电量提示，不阻止解锁 */
    }
    if (buzzer_init(&buzzer))
    {
        buzzer_play(&buzzer, BUZZER_STARTUP);
    }
    else
    {
        RM_LOG_E("buzzer pwm start failed");
    }

    /* 设备自检、硬件看门狗在阶段 1 加入 */
    safety_gate_set_system_ready(&gate); /* 允许解锁 */
    RM_LOG_I("startup done");
    rm_task_delete_self();
}
