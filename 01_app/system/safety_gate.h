/**
 * @file    safety_gate.h
 * @brief   安全门：决定整车能不能动（全车停），以及 Init → Safe → Manual 模式（运行时契约第 5 节、ADR 0026）
 * @note    纯逻辑，control 任务每个周期调用一次 safety_gate_update()，电脑上可测。
 *
 *          全车停的条件：尚未启动完成、遥控丢失、急停、尚未解锁、IMU 未就绪。
 *          - 急停：解锁拨杆在“下”（电平信号，每个周期重新判断）；
 *          - 解锁：在 Safe 模式、遥控在线时先看到拨杆在“下”，再拨到“中”或“上”；
 *          - 回到 Safe 后必须重新做一次解锁动作；遥控恢复、上电时拨杆已在上方，都不会自己动起来。
 *          - IMU 未就绪（imu_state 读不到或过期）：全车停并回到 Safe，恢复后同样要重新解锁。
 *          机构停（本机构设备离线）不在这里，由子系统自己处理。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "04_core/msg/rc_state.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 进入 Manual 后，输出限幅在这么长时间内从 0 线性升到正常值（恢复时不猛冲） */
#define SAFETY_RAMP_MS 300u

typedef enum
{
    ROBOT_MODE_INIT,   /* 启动未完成：全车停，不能解锁 */
    ROBOT_MODE_SAFE,   /* 全车停，等待解锁 */
    ROBOT_MODE_MANUAL, /* 允许动作 */
} RobotMode;

typedef struct
{
    uint8_t arm_switch; /* 解锁 / 急停用哪个拨杆：RcState.sw 的下标（兵种配置，ADR 0032） */
    volatile bool system_ready; /* startup 任务写、control 任务读；单字节读写是原子的 */
    RobotMode mode;
    bool saw_stop_position;   /* Safe 模式下、遥控在线时看到过拨杆在“下” */
    uint64_t manual_since_us; /* 进入 Manual 的时刻，用于输出斜坡 */
} SafetyGate;

typedef struct
{
    bool stop_all; /* 本周期全车停：发送前把每个电机改写成它的 stop_action */
    bool entered_manual; /* 本周期刚进入 Manual：子系统清积分、目标对齐当前状态 */
} SafetyDecision;

/**
 * 全车唯一的安全门（定义在 safety_gate.c）：control 任务每周期更新，indicator、log 任务只读 mode 来提示和打印。
 * 函数仍以指针为参数，电脑测试可以另建实例
 */
extern SafetyGate safety_gate;

/** @param arm_switch  RcState.sw 的下标，0 或 1 */
void safety_gate_init(SafetyGate *gate, uint8_t arm_switch);

/** 启动完成（设备自检之后）才允许解锁；由 startup 任务调用一次 */
void safety_gate_set_system_ready(SafetyGate *gate);

/**
 * @brief   每个控制周期调用一次
 * @param   rc         本周期读到的遥控快照；遥控丢失（话题过期）时传 NULL
 * @param   imu_ready  本周期 imu_state 是否读得到且不旧（imu_state_read(…, IMU_STALE_MS)）
 */
SafetyDecision safety_gate_update(SafetyGate *gate, const RcState *rc, bool imu_ready,
                                  uint64_t now_us);

/** 模式的名字（"init" / "safe" / "manual"），用于日志 */
const char *safety_gate_mode_name(RobotMode mode);

/** 输出限幅的比例：进入 Manual 后 SAFETY_RAMP_MS 内从 0 升到 1，其余时间为 1 */
float safety_gate_output_scale(const SafetyGate *gate, uint64_t now_us);

#ifdef __cplusplus
}
#endif
