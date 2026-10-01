/**
 * @file    control_task.c
 * @brief   步兵的 control 任务（1 kHz）：读输入 → 安全门 → 底盘 → 发送
 * @note    相当于老模板的 Control_Task.c + CAN_Task.c：一个控制周期的四步都写在下面的循环里，从上往下读即可。
 *          第一版底盘直接读遥控（ADR 0043）。1 kHz 任务里不打日志。
 *          **这个任务会给电机发指令**：上板时车架空、轮子离地（docs/VERIFICATION_TODO.md V45 起）。
 *          解锁：右拨杆拨到下再拨到中或上；急停：右拨杆拨到下（ADR 0032）。
 */
#include "robot.h"

#include "04_core/os/os.h"
#include "05_platform/time.h"
#include "config.h"

#define CONTROL_PERIOD_MS 1u
#define CONTROL_DT_S      ((float)CONTROL_PERIOD_MS * 0.001f)

/* 摇杆 → 目标底盘速度（通道对应见 config.h）。摇杆向右为正，底盘向左、逆时针为正，所以左右和旋转取反 */
static ChassisVel chassis_cmd_from_rc(const RcState *rc)
{
    const float k = 1.0f / (float)RC_CH_MAX;
    return (ChassisVel){ .vx_m_s = (float)rc->ch[3] * k * max_vx_m_s,
                         .vy_m_s = -(float)rc->ch[2] * k * max_vy_m_s,
                         .wz_rad_s = -(float)rc->ch[0] * k * max_wz_rad_s };
}

void control_task_entry(void *arg)
{
    (void)arg;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        const uint64_t now_us = rm_time_now_us();

        /* 1. 读输入：遥控 200 ms 没更新算丢失；IMU 标定完成且 20 ms 内有更新才算就绪 */
        RcState rc;
        const bool rc_online = rc_state_read(&rc_state, &rc, RC_LOST_TIMEOUT_MS);
        ImuState imu;
        const bool imu_ready = imu_state_read(&imu_state, &imu, IMU_STALE_MS);

        /* 2. 安全门：急停、遥控丢失、未解锁、IMU 未就绪 → 全车停（stop_all） */
        const SafetyDecision gate_out =
            safety_gate_update(&gate, rc_online ? &rc : NULL, imu_ready, now_us);

        /* 3. 底盘：chassis_step 里依次 读电机实测 → 算目标（斜坡、逆解）→ 算输出（每轮速度环），见 chassis.c。
         *    能动时遥控一定在线（安全门保证），rc 有效；全车停时底盘不读目标、只清积分 */
        const ChassisVel cmd = gate_out.stop_all ? (ChassisVel){ 0 } : chassis_cmd_from_rc(&rc);
        chassis_step(&chassis, &cmd, gate_out.stop_all, safety_gate_output_scale(&gate, now_us),
                     CONTROL_DT_S);

        /* 4. 发送（老模板的 CAN_Task）：全车停时把每个电机改写成它的停机动作，即使上面漏判也不会发出运动指令；
         *    然后把所有电机的指令打包成 CAN 帧发出去，见 motor_group.c */
        if (gate_out.stop_all)
        {
            motor_group_apply_stop_all(&motors);
        }
        motor_group_send(&motors);

        rm_task_delay_until(&last_wake, CONTROL_PERIOD_MS);
    }
}
