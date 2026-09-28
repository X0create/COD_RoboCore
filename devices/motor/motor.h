/**
 * @file    motor.h
 * @brief   电机统一接口（《架构设计》“电机：统一接口 + 各品牌实现”）
 * @note    - 单位全部是输出轴上的国际单位：rad、rad/s、N·m（ADR 0031）；
 *          - 品牌分支只出现在 devices/motor/ 内部，子系统只调用 motor_*()；
 *          - 反馈由 comm_rx 任务写、control 任务读，motor_read_feedback() 在临界区里拷贝完整快照；
 *          - 指令只写入槽位，control 任务周期末尾由 motor_group_flush() 统一打包发送（motor_group.h）；
 *          - 设备层不做任何闭环（ADR 0016）。
 *          目前只有 DJI 电机（移植第 5 步）；达妙电机在第 9 步加入。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/watchdog/watchdog.h"
#include "platform/can.h"
#include "platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 反馈多久收不到就算离线（ADR 0031：DJI 电调每 1 ms 一帧，20 ms 即连续丢 20 帧） */
#define MOTOR_OFFLINE_TIMEOUT_MS 20u

/* 常用减速比（转子 : 输出轴），给 MotorConfig.gear_ratio 用；拆掉减速箱时填 1 */
#define DJI_M3508_GEAR_RATIO (3591.0f / 187.0f)
#define DJI_M2006_GEAR_RATIO 36.0f

typedef enum
{
    MOTOR_M3508,  /* C620 电调 */
    MOTOR_M2006,  /* C610 电调 */
    MOTOR_GM6020, /* 本步只有反馈，没有指令（ADR 0031） */
} MotorType;

/** 停机动作（运行时契约第 5 节）。DJI 电机只支持零力矩和失能，配置成阻尼时 motor_init() 拒绝 */
typedef enum
{
    SAFE_ACTION_ZERO_TORQUE, /* 不出力：DJI 发 0 电流 */
    SAFE_ACTION_DAMP,        /* 只阻碍运动：达妙 Kd（第 9 步） */
    SAFE_ACTION_DISABLE,     /* 驱动器不输出：DJI 发 0 后停止发送 */
} SafeAction;

/** 本车固定参数：写成 robot.c 里的 const 对象，运行中不变 */
typedef struct
{
    const char *name; /* 日志和设备清单里的名字 */
    MotorType type;
    CanBusId can_bus;
    uint8_t id;             /* 电调 ID：M3508 / M2006 为 1–8，GM6020 为 1–7 */
    int8_t direction;       /* +1 / -1：使输出轴正方向符合坐标系约定 */
    float gear_ratio;       /* 转子 : 输出轴，如 DJI_M3508_GEAR_RATIO；直驱填 1 */
    SafeAction stop_action; /* 全车停时发送出口改写成的动作 */
} MotorConfig;

/** 一份完整快照：所有字段来自同一帧 */
typedef struct
{
    float angle_rad; /* 输出轴多圈角度。带减速箱的型号以上电位置为 0；GM6020 以编码器 0 为 0 */
    float single_angle_rad; /* 转子编码器单圈角度 [-π, π)，不乘方向，标定零点时用 */
    uint16_t raw_encoder;    /* 原始编码器值 0–8191，只用于标定和调试 */
    float speed_rad_s;       /* 输出轴角速度 */
    float torque_nm;         /* 输出轴力矩 */
    bool torque_is_estimate; /* true：由电流 × 力矩常数估算，只可参考 */
    float temperature_c;     /* C610 不报温度，恒为 0 */
    uint8_t error_code;      /* 驱动器上报的错误码，0 = 正常（DJI 没有） */
    bool online;             /* 读取时按 MOTOR_OFFLINE_TIMEOUT_MS 计算 */
    uint64_t stamp_us;       /* 收到这帧的时刻 */
} MotorFeedback;

/** 这种型号支持什么 */
typedef struct
{
    bool torque_command;        /* 能否 motor_set_torque() */
    bool torque_feedback_exact; /* 反馈力矩是否可信 */
    bool needs_enable;          /* 是否需要使能帧（达妙：是） */
} MotorCaps;

/** DJI 电机的私有状态：多圈计数 */
typedef struct
{
    int32_t turns;         /* 转子圈数 */
    uint16_t last_encoder; /* 上一帧编码器值，判断过零 */
    uint16_t zero_encoder; /* 角度零点对应的编码器值 */
    bool have_last;        /* 是否收到过帧 */
} DjiMotorState;

typedef struct MotorGroup MotorGroup;

/** 运行状态：只由 devices/motor/ 内部读写，其他文件不要直接访问 */
typedef struct Motor
{
    const MotorConfig *cfg;
    Watchdog wd;
    MotorFeedback fb; /* comm_rx 任务写，临界区保护 */
    union
    {
        DjiMotorState dji; /* 只在 comm_rx 任务里用；达妙的状态在第 9 步加入 */
    } brand;

    /* 本周期的指令槽位：只在 control 任务里读写，motor_group_flush() 发送后清空 */
    float torque_cmd_nm;
    bool torque_set;
    SafeAction safe_action;
    bool safe_set;

    struct Motor *next; /* 所属电机组的链表 */
} Motor;

/**
 * @brief   初始化并加入电机组：检查配置、查 ID 冲突、订阅反馈帧、登记看门狗
 * @param   conflict  失败原因是 ID 冲突时，指向冲突的那个电机，否则置 NULL（用于日志）
 * @return  false：配置不支持（ID 超范围、方向不是 ±1、减速比 ≤ 0、DJI 配了阻尼）、与组内电机的
 *          反馈 ID 或控制帧槽位冲突，或这路 CAN 的硬件滤波器已用完
 * @pre     初始化阶段（can_start() 之前）调用；cfg 在整个运行期间有效
 */
RM_NODISCARD bool motor_init(Motor *m, const MotorConfig *cfg, MotorGroup *group,
                             const Motor **conflict);

MotorCaps motor_caps(const Motor *m);

/** 子系统在 xxx_init() 里检查一次，运行时不再检查 */
bool motor_supports_torque(const Motor *m);

/**
 * @brief   拷贝一份完整的反馈快照
 * @return  是否在线；离线时 out 仍是最后一帧的内容（从未收到时全为 0）
 */
RM_NODISCARD bool motor_read_feedback(const Motor *m, MotorFeedback *out);

/**
 * @brief   写入本周期的力矩指令（输出轴 N·m），超出电调量程时截到量程
 * @pre     motor_supports_torque(m)；只在 control 任务里调用
 */
void motor_set_torque(Motor *m, float torque_nm);

/** 本周期执行停机动作，覆盖 motor_set_torque()；只在 control 任务里调用 */
void motor_apply_safe_action(Motor *m, SafeAction action);

#ifdef __cplusplus
}
#endif
