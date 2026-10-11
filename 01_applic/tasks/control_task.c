/**
 * @file    control_task.c
 * @brief   这台车的 control_task（1 kHz）：读实测 → 算目标 → 算输出
 * @note    照老模板 Control_Task.c 的写法：循环里只有三步，每步一个函数，按顺序排在下面；
 *          这一拍读到了什么、决定了什么都存在 control_info 里，在 Ozone 里看它即可。
 *          第 3 步含老模板 CAN_Task 的工作（把电机指令打包成 CAN 帧发出去）。
 *          底盘的轮组（全向轮 / 麦轮 / 舵轮）由 robot.h 的 chassis_config.type 选，这里不用改。
 *          第一版底盘直接读遥控（ADR 0043）。1 kHz 任务里不打日志。
 *          **这个任务会给电机发指令**：上板时车架空、轮子离地（docs/VERIFICATION_TODO.md V45 起）。
 *          解锁：右拨杆拨到下再拨到中或上；急停：右拨杆拨到下（ADR 0032）。
 */
#include "tasks.h"

#include "01_applic/config/robot.h"
#include "04_core/os/os.h"
#include "05_platform/time/time.h"

#define CONTROL_PERIOD_MS 1u /* 和 CHASSIS_PERIOD_S 一致 */

/*
 * 遥控 → 底盘速度（满杆时的速度）。第一版底盘直接读遥控（ADR 0043）：
 * 左摇杆上下 ch[3] → 前后，左摇杆左右 ch[2] → 左右，右摇杆左右 ch[0] → 旋转；
 * 通道对应沿用旧工程编号，待 V11 核对。首次上台架先用小值
 */
static const float max_vx_m_s = 1.0f;
static const float max_vy_m_s = 1.0f;
static const float max_wz_rad_s = 2.0f;

/** 这一拍的输入和决定（相当于老模板的 Control_Info；底盘自己的实测和目标在 chassis.measure / chassis.target） */
typedef struct
{
    struct
    {
        RcState rc;
        bool rc_online; /* 200 ms 内有新帧；不在线时 rc 是旧数据 */
        ImuState imu;
        bool imu_ready; /* 标定完成且 20 ms 内有更新 */
    } measure;
    struct
    {
        SafetyDecision gate;    /* 安全门的结论：stop_all = 全车停 */
        float output_scale;     /* 输出限幅比例：解锁后从 0 逐渐放开到 1 */
        ChassisVel chassis_vel; /* 摇杆换算出的目标底盘速度（全车停时为 0） */
    } target;
} ControlInfo;

static ControlInfo control_info;

static void control_measure_update(ControlInfo *info);
static void control_target_update(ControlInfo *info);
static void control_output_update(const ControlInfo *info);

void control_task_entry(void *arg)
{
    (void)arg;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        control_measure_update(&control_info);
        control_target_update(&control_info);
        control_output_update(&control_info);

        rm_task_delay_until(&last_wake, CONTROL_PERIOD_MS);
    }
}

/* 第 1 步 读实测：遥控、IMU、底盘各电机 */
static void control_measure_update(ControlInfo *info)
{
    info->measure.rc_online = dr16_read(&dr16, &info->measure.rc);
    info->measure.imu_ready = ins_read(&ins, &info->measure.imu);
    chassis_measure_update(&chassis);
}

/*
 * 第 2 步 算目标：
 *   安全门：急停、遥控丢失、未解锁、IMU 未就绪 → 全车停（stop_all）；
 *   能动时摇杆换算成目标底盘速度（摇杆向右为正，底盘向左、逆时针为正，所以左右和旋转取反），
 *   能动时遥控一定在线（安全门保证），rc 有效；
 *   底盘：斜坡 → 逆解出各轮目标（全车停时目标对齐实测速度）
 */
static void control_target_update(ControlInfo *info)
{
    const uint64_t now_us = rm_time_now_us();
    info->target.gate =
        safety_gate_update(&safety_gate, info->measure.rc_online ? &info->measure.rc : NULL,
                           info->measure.imu_ready, now_us);
    info->target.output_scale = safety_gate_output_scale(&safety_gate, now_us);

    if (info->target.gate.stop_all)
    {
        info->target.chassis_vel = (ChassisVel){ 0 };
    }
    else
    {
        const RcState *rc = &info->measure.rc;
        const float k = 1.0f / (float)RC_CH_MAX;
        info->target.chassis_vel = (ChassisVel){ .vx_m_s = (float)rc->ch[3] * k * max_vx_m_s,
                                                 .vy_m_s = -(float)rc->ch[2] * k * max_vy_m_s,
                                                 .wz_rad_s = -(float)rc->ch[0] * k * max_wz_rad_s };
    }
    chassis_target_update(&chassis, &info->target.chassis_vel, info->target.gate.stop_all);
}

/*
 * 第 3 步 算输出并发送：
 *   底盘每轮速度环 → 电机力矩（全车停时不写指令、只清积分）；
 *   全车停时把每个电机改写成它的停机动作，即使上面漏判也不会发出运动指令；
 *   然后把所有电机的指令打包成 CAN 帧发出去（老模板的 CAN_Task），见 motor.c 第 5 节
 */
static void control_output_update(const ControlInfo *info)
{
    chassis_output_update(&chassis, info->target.output_scale);
    if (info->target.gate.stop_all)
    {
        motor_group_apply_stop_all(&motors);
    }
    motor_group_send(&motors);
}
