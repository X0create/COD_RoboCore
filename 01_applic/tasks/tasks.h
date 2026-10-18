/**
 * @file    tasks.h
 * @brief   全部任务的入口声明，每个任务的实现在同目录的 xxx_task.c
 * @note    任务的名字、优先级、栈都在 01_applic/config/robot.c 的任务表 task_table[]，由 app_main() 逐个创建。
 *          优先级从高到低：ins 6 → comm_rx 5 → control 4 → detect 3 → indicator 2 → log 1。
 *          新增一个任务的步骤见 docs/FREERTOS_GUIDE.md 第 4 节。
 *          任务之间共享的少量数据（心跳、实时性统计、电池电压）也声明在这里，由产生它的任务提供读取函数。
 */
#pragma once

#include <stdbool.h>

#include "04_core/os/os.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief   ins_task（1 kHz）：BMI088 → 加热 → 零偏标定 → EKF → 保存最新姿态
 * @param   arg  这台车的 Ins 对象（Ins *），已经 ins_init()
 */
void ins_task_entry(void *arg);

/** comm_rx_task（收到数据就运行）：打开接收，把 CAN、串口的数据交给对应解析器 */
void comm_rx_task_entry(void *arg);

/** comm_rx_task 的任务控制块：中断要用它唤醒任务，所以定义在 comm_rx_task.c */
extern RmTask comm_rx_task;

/** control_task（1 kHz）：读实测 → 算目标 → 算输出并发送 */
void control_task_entry(void *arg);

/** control_task 每个周期加 1：detect_task 看到它在涨才喂硬件看门狗 IWDG，control 卡死时整板复位 */
extern volatile uint32_t control_heartbeat;

/** control_task 的实时性统计（只记录、不影响控制；log_task 每秒打印一次） */
typedef struct
{
    uint32_t exec_max_us;   /* 一个周期三步的执行时间最大值 */
    uint32_t period_max_us; /* 相邻两个周期开始时刻的间隔最大值（应为 1000） */
    uint32_t late_count;    /* 间隔超过 CONTROL_LATE_US 的次数（自上电累计） */
} ControlTiming;

/** 间隔超过这么久算一次超期 */
#define CONTROL_LATE_US 1500u

/** 取出统计并把两个最大值清零（log_task 每秒调用一次，所以最大值是“最近 1 s 内”的） */
ControlTiming control_timing_take(void);

/** detect_task（100 Hz）：报告设备上线 / 离线（不参与安全判定），按 control_task 心跳喂 IWDG */
void detect_task_entry(void *arg);

/** indicator_task（25 ms）：状态灯、蜂鸣器、低电量检查 */
void indicator_task_entry(void *arg);

/** 最近一次测得的电池电压（V），供 log_task 打印；indicator_task 启动前为 0 */
float indicator_battery_v(void);

/** 当前是否低电量（判定规则见 indicator_task.c 的 battery_config） */
bool indicator_battery_low(void);

/** log_task（每 1 s）：通过 RTT 打印这台车的状态，只读不写 */
void log_task_entry(void *arg);

#ifdef __cplusplus
}
#endif

/** 独立 h723-handguide 台架固件的任务与记录初始化。 */
bool hand_guide_log_init(void);
void hand_guide_task_entry(void *argument);
void hand_guide_rx_task_entry(void *argument);
