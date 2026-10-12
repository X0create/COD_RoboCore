/**
 * @file    robot.h
 * @brief   这台车（目前：四轮全向轮底盘，遥控直接给底盘速度）：全部可调参数 + 全部对象的声明
 * @note    改车只看两个文件：本文件（参数表、对象声明）和 robot.c（对象定义、robot_init()、任务表）。
 *          通用的参数（电池阈值、EKF、IMU 加热、各种超时）留在各自模块里，位置见 01_applic/README.md“参数在哪里”。
 *          标 “待量” 的尺寸按实车量好再改；标 “待核对” 的方向在台架上按 docs/VERIFICATION_TODO.md 核对。
 *          换轮组：改 chassis_config 的 .type 并填对应的尺寸（全向轮 .omni、麦轮 .mecanum、
 *          舵轮 .steer，半舵半全向 .half_steer + .steer 的零点和 PID，含义见 03_algorithm/kinematics/ 下的头文件）；
 *          舵轮还要在 robot.c 里加 4 个转向电机，半舵半全向加 2 个（全向轮位置传 NULL）；
 *          转向电机用 M2006、M3508 这类没有绝对编码器、上电位置记为 0 的型号时，
 *          要在 .steer.zero_switch 里配零点开关，上电先转一圈找到零点。
 *          PID 不带 dt：参数是按 1 kHz 调用整定的，改了控制频率要重新整定。
 *          参数表是 static const：只给 robot.c 和 01_applic/tasks/ 里的任务用，没用到的不占空间。
 *          下方的“对象”就是整车共用的全局变量（遥控、电机、底盘、IMU）。只有 01_applic/ 里的代码 include 本文件，
 *          底层模块不知道“这台车”长什么样，所以换车只改这里。谁写谁读：
 *            dr16（遥控）        comm_rx_task 写；control、log 用 dr16_read() 读
 *            ins（姿态）         ins_task 写；control、log 用 ins_read() 读
 *            电机反馈            comm_rx_task 经 motor_group_receive(&motors, …) 写；chassis、log 用 motor_read_feedback() 读
 *            chassis、电机指令   只在 control_task 里写；log 只读来打印
 *            supercap（电容）    comm_rx_task 经 supercap_receive() 写反馈；control_task 发指令；log 用 supercap_read() 读
 */
#pragma once

#include <stdint.h>

#include "01_applic/modules/chassis/chassis.h"
#include "01_applic/modules/ins/ins.h"
#include "01_applic/system/safety_gate.h"
#include "02_devices/motor/motor.h"
#include "02_devices/remote/dr16.h"
#include "02_devices/supercap/supercap.h"
#include "03_algorithm/math/math_const.h"
#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ------------------------------------------------------------------ */
/* 驱动轮电机：一行一个轮子，按实车接线改                                */
/* ------------------------------------------------------------------ */

/*
 * 顺序 = 底盘的轮 0–3（左前、左后、右后、右前，omni.h），全部在 FDCAN1（经典 CAN）。
 * direction：使电机正转 = 这个轮子推动底盘逆时针转（omni.h）；四个电机轴都朝外对称安装时四个值相同。
 *            第一次上车要核对：架空车，给一个小的逆时针旋转指令，看四个轮子是否都朝同一个旋转方向转，转反的改成 -1。
 * 停机动作：零力矩，即发 0 电流，轮子靠摩擦自己停下（DJI 电调只支持零力矩或失能两种停法）。
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
 * 四轮全向轮：每个轮子斜 45° 装在一个角上，俯视四个轮子围成一圈（O 形）。
 * 轮半径、中心距（轮子接地点到底盘中心）要按实车量了再填；加速度先取保守值，上车再调。
 * 速度环 PID 单位是国际单位：输入轮子输出轴转速误差（rad/s），输出电机力矩（N·m）。
 * 下面的数字是从一组常用的“rpm 进、电流原始值出”的 M3508 底盘参数换算来的：
 *   原值 kp 13、ki 0.1、积分限幅 5000 rpm、输出限幅 12000（C620 电流原始值，±16384 对应 ±20 A）；
 *   换算：输出轴 1 rad/s = 转子 183.38 rpm，1 N·m = 2730.7 电流原始值（M3508 + C620，见 02_devices/motor/motor.h），
 *   例如 kp = 13 × 183.38 ÷ 2730.7 ≈ 0.8730。
 * 手里有别的参数时照这个方法换算，就能直接填进来。
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
/* 超级电容                                                            */
/* ------------------------------------------------------------------ */

/*
 * 电容控制板：经典 CAN，只能接 FDCAN1 或 FDCAN2（FDCAN3 是 CAN FD）。现在轮子在 FDCAN1，电容放 FDCAN2。
 * 两个 ID 不能和同一路上的电机重复（robot_init 会检查）。协议见 02_devices/supercap/README.md。
 * 没接电容板时也没关系：只是日志里一直显示 supercap offline，不影响车动。
 */
static const SupercapConfig supercap_config = {
    .name = "supercap", .can_bus = CAN_BUS_2, .cmd_id = 0x061u, .feedback_id = 0x062u
};

/*
 * 发给电容的底盘功率上限（W）。裁判系统解析还没做，先用固定值；
 * 做好之后改成裁判系统给的底盘功率上限（0x0201），这个常数就删掉。按本车当前等级的规则值填
 */
static const float supercap_power_limit_w = 60.0f;

/* ------------------------------------------------------------------ */
/* 遥控                                                                */
/* ------------------------------------------------------------------ */

/* 遥控满杆对应的底盘速度在 01_applic/tasks/control_task.c 顶部（只有那里用） */

/* 解锁 / 急停拨杆：sw[1]，一般是右拨杆（上车拨一下，在 Ozone 里看 dr16 的 sw 哪个在变来确认） */
static const uint8_t arm_switch = 1u;

/* ------------------------------------------------------------------ */
/* IMU                                                           */
/* ------------------------------------------------------------------ */

/*
 * IMU 安装旋转：机体系向量 = R × 芯片系向量（3×3 矩阵，按行存储）。
 * 机体系：X 朝车头、Y 朝车左、Z 朝上。默认认为板子上的芯片轴与机体轴同向（单位矩阵）。
 * 板子装歪了、倒装了就改这里。核对方法：车头抬起时 pitch 应变化、向左转时 yaw 应增大，
 * 静止水平放置时加速度计 Z 轴应约为 +9.8 m/s²。
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

extern Supercap supercap; /* comm_rx_task 收反馈、control_task 发指令、log_task 打印 */

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
