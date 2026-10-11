/**
 * @file    robot.h
 * @brief   这台车（目前：四轮全向轮底盘，遥控直接给底盘速度）：全部可调参数 + 全部对象的声明
 * @note    改车只看两个文件：本文件（参数表、对象声明）和 robot.c（对象定义、robot_init()、任务表）。
 *          通用的参数（电池阈值、EKF、IMU 加热、各种超时）留在各自模块里，位置见 01_applic/README.md“参数在哪里”。
 *          标 “待量” 的尺寸按实车量好再改；标 “待核对” 的方向在台架上按 docs/VERIFICATION_TODO.md 核对。
 *          换轮组：改 chassis_config 的 .type 并填对应的尺寸（全向轮 .omni、麦轮 .mecanum、
 *          舵轮 .steer，半舵半全向 .half_steer + .steer 的零点和 PID，含义见 03_algorithm/kinematics/kinematics.h 对应的一节）；
 *          舵轮还要在 robot.c 里加 4 个转向电机，半舵半全向加 2 个（全向轮位置传 NULL）。
 *          PID 不带 dt（ADR 0029），参数和 1 kHz 调用频率绑定。
 *          参数表是 static const：只给 robot.c 和 01_applic/tasks/ 里的任务用，没用到的不占空间。
 *          对象相当于老模板里各任务直接读的全局变量（remote_ctrl、Chassis_Motor[] 等），其他层不 include 本文件
 *          （CODING_STANDARD 第 8 节的例外）。谁写谁读：
 *            dr16（遥控）        comm_rx_task 写；control、log 用 dr16_read() 读
 *            ins（姿态）         ins_task 写；control、log 用 ins_read() 读
 *            电机反馈            comm_rx_task 经 motor_group_receive(&motors, …) 写；chassis、log 用 motor_read_feedback() 读
 *            chassis、电机指令   只在 control_task 里写；log 只读来打印
 */
#pragma once

#include <stdint.h>

#include "01_applic/modules/chassis/chassis.h"
#include "01_applic/modules/ins/ins.h"
#include "01_applic/system/safety_gate.h"
#include "02_devices/motor/motor.h"
#include "02_devices/remote/dr16.h"
#include "03_algorithm/math/rm_math.h"
#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ------------------------------------------------------------------ */
/* 驱动轮电机：一行一个轮子，按实车接线改                                */
/* ------------------------------------------------------------------ */

/*
 * 顺序 = 底盘的轮 0–3（左前、左后、右后、右前，kinematics.h 全向轮一节），全部在 FDCAN1（经典 CAN）。
 * direction：使电机正转 = 这个轮子推动底盘逆时针转（kinematics.h 全向轮一节）；四个电机轴都朝外对称安装时四个值相同，正负待核对（V46）。
 * 停机动作：零力矩（DJI 电调只支持零力矩 / 失能，ADR 0031）。
 */
static const MotorConfig wheel_config[CHASSIS_WHEELS] = {
    { .name = "wheel_lf",
      .type = MOTOR_M3508,
      .can_bus = CAN_BUS_1,
      .id = 1u,
      .direction = 1,
      .gear_ratio = DJI_M3508_GEAR_RATIO,
      .stop_action = STOP_ACTION_ZERO_TORQUE },
    { .name = "wheel_lb",
      .type = MOTOR_M3508,
      .can_bus = CAN_BUS_1,
      .id = 2u,
      .direction = 1,
      .gear_ratio = DJI_M3508_GEAR_RATIO,
      .stop_action = STOP_ACTION_ZERO_TORQUE },
    { .name = "wheel_rb",
      .type = MOTOR_M3508,
      .can_bus = CAN_BUS_1,
      .id = 3u,
      .direction = 1,
      .gear_ratio = DJI_M3508_GEAR_RATIO,
      .stop_action = STOP_ACTION_ZERO_TORQUE },
    { .name = "wheel_rf",
      .type = MOTOR_M3508,
      .can_bus = CAN_BUS_1,
      .id = 4u,
      .direction = 1,
      .gear_ratio = DJI_M3508_GEAR_RATIO,
      .stop_action = STOP_ACTION_ZERO_TORQUE },
};

/* ------------------------------------------------------------------ */
/* 底盘：尺寸、加速度、每轮速度环                                        */
/* ------------------------------------------------------------------ */

/*
 * X 形四轮全向轮。轮半径、中心距待量；加速度先取保守值，台架整定。
 * 速度环沿用旧工程（COD-H7-Template Control_Task.c）的底盘参数，已换算成国际单位（输出轴 rad/s → N·m）：
 *   旧值 kp 13、ki 0.1（rpm → 电流原始值）、积分限幅 5000 rpm、输出限幅 12000 电流原始值；
 *   换算：输出轴 1 rad/s = 转子 183.38 rpm，1 N·m = 2730.7 电流原始值（M3508 + C620，02_devices/motor/motor.h）
 */
static const ChassisConfig chassis_config = {
    .type = CHASSIS_OMNI,
    .omni = { .wheel_radius_m = 0.076f, .center_dist_m = 0.25f, .first_wheel_rad = RM_PI / 4.0f },
    .max_accel_m_s2 = 2.0f,
    .max_alpha_rad_s2 = 4.0f,
    .drive_speed_pid = { .kp = 0.8730f,             /* N·m 每 rad/s */
                         .ki = 0.006715f,           /* N·m 每 rad/s，每 1 ms 累加一次 */
                         .integral_limit = 27.27f,  /* rad/s */
                         .output_limit = 4.3945f }, /* N·m */
};

/* ------------------------------------------------------------------ */
/* 遥控                                                                */
/* ------------------------------------------------------------------ */

/* 遥控满杆对应的底盘速度在 01_applic/tasks/control_task.c 顶部（只有那里用） */

/* 解锁 / 急停拨杆：右拨杆 sw[1]（ADR 0032；左右以 V11 上板核对为准） */
static const uint8_t arm_switch = 1u;

/* ------------------------------------------------------------------ */
/* IMU                                                           */
/* ------------------------------------------------------------------ */

/*
 * IMU 安装旋转：机体系向量 = R × 芯片系向量（按行存储，ADR 0006）。
 * 默认芯片轴与机体轴同向（X 前、Y 左、Z 上）；实际朝向以 V7 上板结果为准
 */
static const InsConfig ins_config = { .install_rotation = {
                                          1.0f, 0.0f, 0.0f, /* 机体 x */
                                          0.0f, 1.0f, 0.0f, /* 机体 y */
                                          0.0f, 0.0f, 1.0f, /* 机体 z */
                                      } };

/* ================================================================== */
/* 对象（定义在 robot.c）                                               */
/* ================================================================== */

extern Dr16 dr16;
extern Motor wheel_motor[CHASSIS_WHEELS]; /* 轮 0–3：左前、左后、右后、右前 */
extern MotorGroup motors; /* 全部电机：comm_rx_task 收反馈、control_task 最后统一发送 */

extern Chassis chassis;
extern Ins ins;

/**
 * @brief   初始化这台车的全部对象（设备 → 机构 → 安全门），由 app_main() 在调度器启动前调用
 * @return  false：必需的设备或机构初始化失败，原因已记日志
 */
RM_NODISCARD bool robot_init(void);

#ifdef __cplusplus
}
#endif
