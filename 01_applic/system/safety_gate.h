/**
 * @file    safety_gate.h
 * @brief   安全门：决定整车能不能动（全车停），以及 Init → Safe → Manual 三种模式
 * @note    纯逻辑，control_task 每个周期调用一次 safety_gate_update()，不碰硬件，电脑上可以单元测试。
 *          三种模式：Init = 刚上电、还在启动；Safe = 已启动、等操作手解锁；Manual = 已解锁、可以动。
 *          “全车停”的具体做法：发送前把每个电机的指令改成它的停机动作（零力矩或失能），见 motor.h 的 StopAction。
 *
 *          赛场上优先保证车能继续跑：全车停只有三种情况——尚未启动完成、尚未解锁、急停。
 *          - 急停：解锁拨杆在“下”（电平信号，每个周期重新判断），回到 Safe，之后要重新解锁；
 *          - 解锁：在 Safe 模式、遥控在线时先看到拨杆在“下”，再拨到“中”或“上”；上电时拨杆已在上方不会自己动起来；
 *          - 遥控丢失：Manual 下**不**全车停、不退出 Manual，由 control_task 把目标速度改为 0、底盘按斜坡减速；
 *            遥控恢复、拨杆不在“下”就直接继续（不用重新解锁）。Safe 下丢失期间的拨杆位置不作数。
 *          - 程序故障复位后（看门狗 IWDG 复位）：遥控第一次在线时拨杆不在“下”就直接解锁，不用重新拨杆；
 *            普通上电、调试器复位仍要按上面的顺序解锁。
 *          IMU 状态不在这里：只影响用到 IMU 的机构。机构降级（本机构设备离线）由子系统自己处理。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "02_devices/remote/dr16.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 进入 Manual 后，输出限幅在这么长时间内从 0 线性升到正常值（恢复时不猛冲） */
#define SAFETY_RAMP_MS 300u

typedef enum
{
    SAFETY_MODE_INIT,   /* 启动未完成：全车停，不能解锁 */
    SAFETY_MODE_SAFE,   /* 全车停，等待解锁 */
    SAFETY_MODE_MANUAL, /* 允许动作 */
} SafetyMode;

typedef struct
{
    uint8_t arm_switch; /* 解锁 / 急停用哪个拨杆：RcState.sw 的下标（在 robot.h 配置） */
    volatile bool system_ready; /* startup_task 写、control_task 读；单字节读写是原子的 */
    SafetyMode mode;
    bool saw_stop_position; /* Safe 模式下、遥控在线时看到过拨杆在“下” */
    bool resume_pending; /* 故障复位后还没恢复：遥控第一次在线、拨杆不在“下”时直接解锁 */
    uint64_t manual_since_us; /* 进入 Manual 的时刻，用于输出斜坡 */
} SafetyGate;

typedef struct
{
    bool stop_all; /* 本周期全车停：发送前把每个电机改写成它的 stop_action */
    bool entered_manual; /* 本周期刚进入 Manual：子系统清积分、目标对齐当前状态 */
} SafetyDecision;

/**
 * 全车唯一的安全门（定义在 safety_gate.c）：control_task 每周期更新，indicator、log_task 只读 mode 来提示和打印。
 * 函数仍以指针为参数，电脑测试可以另建实例
 */
extern SafetyGate safety_gate;

/** @param arm_switch  RcState.sw 的下标，0 或 1 */
void safety_gate_init(SafetyGate *gate, uint8_t arm_switch);

/** 上次复位是程序故障（IWDG）引起的：允许不重新拨杆就恢复。由 app_main 在 safety_gate_init() 之后调用 */
void safety_gate_resume_after_fault(SafetyGate *gate);

/** 启动完成（设备自检之后）才允许解锁；由 startup_task 调用一次 */
void safety_gate_set_system_ready(SafetyGate *gate);

/**
 * @brief   每个控制周期调用一次
 * @param   rc  本周期读到的遥控数据；遥控丢失（dr16_read 返回 false）时传 NULL
 */
SafetyDecision safety_gate_update(SafetyGate *gate, const RcState *rc, uint64_t now_us);

/** 模式的名字（"init" / "safe" / "manual"），用于日志 */
const char *safety_gate_mode_name(SafetyMode mode);

/** 输出限幅的比例：进入 Manual 后 SAFETY_RAMP_MS 内从 0 升到 1，其余时间为 1 */
float safety_gate_output_scale(const SafetyGate *gate, uint64_t now_us);

#ifdef __cplusplus
}
#endif
