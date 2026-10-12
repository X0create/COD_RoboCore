/**
 * @file    control_task.c
 * @brief   control_task（1 kHz）：整车控制的主循环——读实测 → 算目标 → 算输出并发给电机
 * @note    怎么读这个文件：
 *            control_task_entry() 的循环里只有三步，每步一个函数，按执行顺序排在本文件下方：
 *              1. control_measure_update()  读遥控、读底盘各电机的反馈
 *              2. control_target_update()   安全门判断能不能动，再把摇杆换算成底盘目标速度
 *              3. control_output_update()   速度环算出电机力矩，打包成 CAN 帧发出去
 *            这一拍读到了什么、决定了什么都存在 control_info 里，调试时在 Ozone 里看它即可；
 *            底盘自己的实测和目标在 chassis.measure / chassis.target。
 *          换轮组（全向轮 / 麦轮 / 舵轮）只改 robot.h 的 chassis_config.type，本文件不用动。
 *          目前底盘直接读遥控；加了云台以后，改成由云台给出底盘速度。
 *          1 kHz 任务里不打日志（RTT 打印太慢，会拖慢控制周期），要看数据用 Ozone 或 log_task。
 *          **这个任务会给电机发指令**：第一次上电调试时把车架空、轮子离地。
 *          解锁：右拨杆先拨到下，再拨到中或上；急停：右拨杆拨到下。
 */
#include "01_applic/tasks/tasks.h"

#include "01_applic/config/robot.h"
#include "04_core/os/critical.h"
#include "04_core/os/os.h"
#include "05_platform/time/time.h"

#define CONTROL_PERIOD_MS                                                                          \
    1u /* 控制周期；必须和底盘里的 CHASSIS_PERIOD_S 一致，PID 参数按 1 kHz 整定 */

/*
 * 遥控 → 底盘速度：摇杆推满时对应的底盘速度。
 *   左摇杆上下 ch[3] → 前后 vx，左摇杆左右 ch[2] → 左右 vy，右摇杆左右 ch[0] → 旋转 wz。
 * 通道和摇杆的对应关系要上车核对（推一下摇杆，在 Ozone 里看 control_info.measure.rc.ch 哪一个在变）。
 * 第一次上车先用小值，确认方向对了再加大。
 */
static const float max_vx_m_s = 1.0f;
static const float max_vy_m_s = 1.0f;
static const float max_wz_rad_s = 2.0f;

/**
 * 这一拍的输入和决定，每个周期整体刷新一次。
 * 放在一个结构体里是为了调试方便：Ozone 里只要看 control_info 一个变量，就知道这一拍发生了什么。
 */
typedef struct
{
    struct
    {
        RcState rc;     /* 遥控器最新一帧：摇杆、拨杆、鼠标键盘 */
        bool rc_online; /* 200 ms 内收到过新帧；为 false 时 rc 里是旧数据，不能用 */
    } measure;
    struct
    {
        SafetyDecision gate; /* 安全门的结论：stop_all = true 表示全车停 */
        /* 输出限幅比例：刚解锁时为 0，在 300 ms 内逐渐放开到 1，防止车猛地窜出去 */
        float output_scale;
        ChassisVel chassis_vel; /* 摇杆换算出的底盘目标速度（全车停、遥控丢失时为 0） */
    } target;
} ControlInfo;

static ControlInfo control_info;

/* 本任务每跑完一个周期加 1。detect_task 看到它在涨才喂硬件看门狗：本任务卡死时整板复位 */
volatile uint32_t control_heartbeat;

/* 实时性统计：周期是否准时、每拍执行多久。log_task 每秒取一次打印出来 */
static ControlTiming timing;

/* 三步的实现在本文件下方，按执行顺序排列 */
static void control_measure_update(ControlInfo *info);
static void control_target_update(ControlInfo *info);
static void control_output_update(const ControlInfo *info);

void control_task_entry(void *arg)
{
    (void)arg;
    RmTaskPeriod last_wake = rm_task_period_start(); /* 周期延时的起点 */
    uint64_t last_start_us = rm_time_now_us();
    for (;;)
    {
        const uint64_t start_us = rm_time_now_us();

        control_measure_update(&control_info); /* 1. 读实测 */
        control_target_update(&control_info);  /* 2. 算目标 */
        control_output_update(&control_info);  /* 3. 算输出并发送 */

        /*
         * 实时性统计与心跳：只记录、不影响控制。
         * period_us = 这一拍开始到上一拍开始的间隔（应为 1000 us），exec_us = 这一拍三步花了多久。
         * timing 会被 log_task 读，所以改它时关中断（临界区），避免读到改了一半的数据。
         */
        const uint32_t period_us = (uint32_t)(start_us - last_start_us);
        const uint32_t exec_us = (uint32_t)(rm_time_now_us() - start_us);
        last_start_us = start_us;
        rm_critical_enter();
        timing.period_max_us = period_us > timing.period_max_us ? period_us : timing.period_max_us;
        timing.exec_max_us = exec_us > timing.exec_max_us ? exec_us : timing.exec_max_us;
        timing.late_count += period_us > CONTROL_LATE_US ? 1u : 0u;
        rm_critical_exit();
        control_heartbeat++;

        /* 睡到“上一拍起点 + 1 ms”：节拍固定，不会因为本拍执行时间长短而漂移 */
        rm_task_delay_until(&last_wake, CONTROL_PERIOD_MS);
    }
}

ControlTiming control_timing_take(void)
{
    rm_critical_enter();
    const ControlTiming t = timing;
    timing.exec_max_us = 0u; /* 两个最大值清零，下一次取到的就是“最近这段时间”的最大值 */
    timing.period_max_us = 0u;
    rm_critical_exit();
    return t;
}

/*
 * 第 1 步 读实测：遥控器、底盘各电机。
 * 读取函数都会拷出一份完整的数据，并告诉你它是否在线（多久没更新算掉线由各模块自己定）。
 * 底盘不用 IMU：IMU 出问题时只影响用到姿态的机构（如云台），底盘照常能开。
 */
static void control_measure_update(ControlInfo *info)
{
    info->measure.rc_online = dr16_read(&dr16, &info->measure.rc);
    chassis_measure_update(&chassis);
}

/*
 * 第 2 步 算目标：
 *   ① 安全门：急停、未解锁 → 全车停（stop_all）；
 *   ② 遥控丢失：不全车停，目标速度给 0，底盘按加速度限制平稳减速停下；遥控恢复后直接按摇杆继续，
 *      不用重新解锁（比赛时遥控偶尔断一下，不能让车彻底趴窝）；
 *   ③ 正常：摇杆换算成底盘目标速度。摇杆向右为正，而底盘约定向左、逆时针为正，所以左右和旋转取反；
 *   ④ 底盘：按加速度限制让速度逐渐变化，再分解成每个轮子的目标转速（全车停时目标对齐实测，见 chassis.c）。
 */
static void control_target_update(ControlInfo *info)
{
    const uint64_t now_us = rm_time_now_us();
    /* 遥控不在线时传 NULL，安全门据此判断“没有有效输入” */
    info->target.gate = safety_gate_update(
        &safety_gate, info->measure.rc_online ? &info->measure.rc : NULL, now_us);
    info->target.output_scale = safety_gate_output_scale(&safety_gate, now_us);

    if (info->target.gate.stop_all || !info->measure.rc_online)
    {
        info->target.chassis_vel = (ChassisVel){ 0 };
    }
    else
    {
        const RcState *rc = &info->measure.rc;
        const float k = 1.0f / (float)RC_CH_MAX; /* 摇杆原始值归一化到 -1 … 1 */
        info->target.chassis_vel = (ChassisVel){ .vx_m_s = (float)rc->ch[3] * k * max_vx_m_s,
                                                 .vy_m_s = -(float)rc->ch[2] * k * max_vy_m_s,
                                                 .wz_rad_s = -(float)rc->ch[0] * k * max_wz_rad_s };
    }
    chassis_target_update(&chassis, &info->target.chassis_vel, info->target.gate.stop_all);
}

/*
 * 第 3 步 算输出并发送：
 *   ① 底盘每个轮子的速度环算出电机力矩（全车停时不写指令、只清积分，防止解锁瞬间积分冲出去）；
 *   ② 全车停时，把每个电机的指令改写成它自己的停机动作（零力矩或失能）——
 *      这是最后一道关口：就算上面哪里漏判了，也不会发出运动指令；
 *   ③ 把所有电机的指令打包成 CAN 帧发出去（每个电机最终发什么，见 motor.c 的 motor_group_send()）；
 *   ④ 给超级电容发指令（内部限频，10 ms 才真的发一帧）。电容一直允许输出：全车停时电机本来就不出力，
 *      电容输不输出都一样；功率上限先用 robot.h 的固定值，裁判系统做好后改成裁判系统给的值。
 */
static void control_output_update(const ControlInfo *info)
{
    chassis_output_update(&chassis, info->target.output_scale);
    if (info->target.gate.stop_all)
    {
        motor_group_apply_stop_all(&motors);
    }
    motor_group_send(&motors);

    const SupercapCmd cap_cmd = { .enable_output = true,
                                  .power_limit_w = supercap_power_limit_w,
                                  .buffer_energy_j = 0.0f };
    supercap_send(&supercap, &cap_cmd);
}
