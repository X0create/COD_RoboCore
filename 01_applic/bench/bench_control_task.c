/**
 * @file    bench_control_task.c
 * @brief   台架验证固件的 control_task（1 kHz）：读输入 → 安全门 → 速度环 → 发送
 * @note    相当于老模板的 Control_Task.c + CAN_Task.c：遥控通道 3 控制一台 M3508 的转速，
 *          一个控制周期的四步都写在下面的循环里，从上往下读即可。1 kHz 任务里不打日志。
 *          另接一台达妙 DM8009（FDCAN2，FD；同旧工程配置）：解锁时请求使能、之后零力矩；全车停时阻尼；
 *          每个周期都发一帧（驱动器只在收到帧时回反馈）。
 *          **这个任务会给电机发指令**：未解锁时持续发 0 电流；解锁后按遥控通道 3 转动。上板按台架条件
 *          （docs/VERIFICATION_TODO.md“接电机”）。
 *          解锁：右拨杆拨到下再拨到中或上；急停：右拨杆拨到下（bench_config.h，ADR 0032）。
 */
#include "bench_robot.h"

#include "03_algorithm/control/pid.h"
#include "04_core/os/os.h"
#include "05_platform/time/time.h"
#include "bench_config.h"

#define CONTROL_PERIOD_MS 1u

static Pid speed_pid; /* 老模板的 Chassis_PID */

void control_task_entry(void *arg)
{
    (void)arg;
    pid_init(&speed_pid, PID_POSITION, &speed_pid_param);

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        const uint64_t now_us = rm_time_now_us();

        /* 1. 读输入：遥控 200 ms 没更新算丢失；IMU 标定完成且 20 ms 内有更新才算就绪；电机 20 ms 没反馈算离线 */
        RcState rc;
        const bool rc_online = rc_state_read(&rc_state, &rc, RC_LOST_TIMEOUT_MS);
        ImuState imu;
        const bool imu_ready = imu_state_read(&imu_state, &imu, IMU_STALE_MS);
        MotorFeedback fb;
        const bool motor_online = motor_read_feedback(&chassis_motor, &fb);

        /* 2. 安全门：急停、遥控丢失、未解锁、IMU 未就绪 → 全车停（stop_all） */
        const SafetyDecision gate_out =
            safety_gate_update(&safety_gate, rc_online ? &rc : NULL, imu_ready, now_us);

        /* 3. 速度环（老模板 Control_Task 的 Target → PID）。全车停或电机离线（机构停）时清积分，不写指令：
         *    没写指令的电机发零力矩，恢复时从零开始，不会因积分猛冲 */
        if (gate_out.stop_all || !motor_online)
        {
            pid_reset(&speed_pid);
        }
        else /* 不全车停时本周期遥控一定在线（安全门保证），rc 有效 */
        {
            const float target_rad_s = (float)rc.ch[3] * BENCH_SPEED_PER_CH;
            const float limit =
                speed_pid.param.output_limit * safety_gate_output_scale(&safety_gate, now_us);
            float torque_nm = pid_calc(&speed_pid, target_rad_s, fb.speed_rad_s);
            torque_nm = (torque_nm > limit) ? limit : ((torque_nm < -limit) ? -limit : torque_nm);
            motor_set_torque(&chassis_motor, torque_nm);
        }

        /* 达妙：解锁时请求使能（离线后请求会被清除，需要重新解锁）；之后不写指令即为零力矩 */
        if (gate_out.entered_manual)
        {
            motor_request_enable(&joint_motor);
        }

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
