/**
 * @file    control_task.c
 * @brief   这台车的 control_task（1 kHz）：读输入 → 安全门 → 底盘 → 发送
 * @note    相当于老模板的 Control_Task.c + CAN_Task.c：一个控制周期的四步都写在下面的循环里，从上往下读即可。
 *          第一版底盘直接读遥控（ADR 0043）。1 kHz 任务里不打日志。
 *          **这个任务会给电机发指令**：上板时车架空、轮子离地（docs/VERIFICATION_TODO.md V45 起）。
 *          解锁：右拨杆拨到下再拨到中或上；急停：右拨杆拨到下（ADR 0032）。
 */
#include "tasks.h"

#include "01_applic/config/robot.h"
#include "04_core/os/os.h"
#include "05_platform/time/time.h"

#define CONTROL_PERIOD_MS 1u /* 和 CHASSIS_PERIOD_S 一致 */

void control_task_entry(void *arg)
{
    (void)arg;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        const uint64_t now_us = rm_time_now_us();

        /* 1. 读输入：遥控 200 ms 没更新算丢失；IMU 标定完成且 20 ms 内有更新才算就绪 */
        RcState rc;
        const bool rc_online = dr16_read(&dr16, &rc);
        ImuState imu;
        const bool imu_ready = ins_read(&ins, &imu);

        /* 2. 安全门：急停、遥控丢失、未解锁、IMU 未就绪 → 全车停（stop_all） */
        const SafetyDecision gate_out =
            safety_gate_update(&safety_gate, rc_online ? &rc : NULL, imu_ready, now_us);

        /* 3. 底盘（chassis.c）：
         *    全车停 → chassis_stop：只读实测、清积分，不写电机指令；
         *    能动   → 摇杆换算成目标速度（满杆速度见 robot.h；摇杆向右为正，底盘向左、逆时针为正，所以左右和旋转取反），
         *             chassis_step 里依次 读电机实测 → 算目标（斜坡、逆解）→ 算输出（每轮速度环）。
         *             能动时遥控一定在线（安全门保证），rc 有效 */
        if (gate_out.stop_all)
        {
            chassis_stop(&chassis);
        }
        else
        {
            const float k = 1.0f / (float)RC_CH_MAX;
            const ChassisVel cmd = { .vx_m_s = (float)rc.ch[3] * k * max_vx_m_s,
                                     .vy_m_s = -(float)rc.ch[2] * k * max_vy_m_s,
                                     .wz_rad_s = -(float)rc.ch[0] * k * max_wz_rad_s };
            chassis_step(&chassis, &cmd, safety_gate_output_scale(&safety_gate, now_us));
        }

        /* 4. 发送（老模板的 CAN_Task）：全车停时把每个电机改写成它的停机动作，即使上面漏判也不会发出运动指令；
         *    然后把所有电机的指令打包成 CAN 帧发出去，见 motor.c 第 5 节 */
        if (gate_out.stop_all)
        {
            motor_group_apply_stop_all(&motors);
        }
        motor_group_send(&motors);

        rm_task_delay_until(&last_wake, CONTROL_PERIOD_MS);
    }
}
