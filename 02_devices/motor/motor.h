/**
 * @file    motor.h
 * @brief   电机统一接口（《架构设计》“电机：统一接口 + 各品牌实现”）
 * @note    - 单位全部是输出轴上的国际单位：rad、rad/s、N·m（ADR 0031）；
 *          - 品牌分支只出现在 02_devices/motor/ 内部，子系统只调用 motor_*()；
 *          - 设备层不做任何闭环（ADR 0016）。
 *
 *          一个电机怎么用（和老模板 SendValue → CAN_Task 的顺序相同）：
 *            收：comm_rx_task  can_read() 取一帧 → motor_receive(&电机, 总线, &帧)，是它的反馈就解码、存下、喂狗
 *            读：control_task  motor_read_feedback()，在临界区里拷贝完整快照（带在线判断）
 *            写：control_task  motor_set_torque() / motor_apply_safe_action()  —— 只记下本周期的指令，还没有发送
 *            发：control_task  周期末尾 motor_group_send()：确定最终指令（停机动作 > 没写指令 > 离线 > 力矩）
 *                              → 编码 → 发送 → 清空本周期指令（motor_group.c）
 *          品牌：DJI（M3508 / M2006 / GM6020）、达妙（MIT 模式，ADR 0035）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "03_algorithm/math/math_const.h"

#include "04_core/watchdog/watchdog.h"
#include "05_platform/can/can.h"
#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 反馈多久收不到就算离线（ADR 0031：每 1 ms 一帧，20 ms 即连续丢 20 帧；达妙每收到一帧指令回一帧反馈） */
#define MOTOR_OFFLINE_TIMEOUT_MS 20u

/* 常用减速比（转子 : 输出轴），给 MotorConfig.gear_ratio 用；拆掉减速箱时填 1 */
#define DJI_M3508_GEAR_RATIO (3591.0f / 187.0f)
#define DJI_M2006_GEAR_RATIO 36.0f

/*
 * M3508 + C620 的换算常数（附录 A.2），全仓库只在这里定义：dji_motor.c 编解码、robot_config.h 把旧工程的 PID
 * 换算到国际单位都用这里。C620 电流原始值 ±16384 对应 ±20 A；原装减速箱输出轴 0.3 N·m/A
 */
#define DJI_C620_RAW_MAX   16384
#define DJI_C620_MAX_A     20.0f
#define DJI_M3508_NM_PER_A 0.3f
#define DJI_M3508_RAW_PER_NM                                                                       \
    ((float)DJI_C620_RAW_MAX                                                                       \
     / (DJI_C620_MAX_A * DJI_M3508_NM_PER_A)) /* 输出轴 1 N·m 的电流原始值 */
#define DJI_M3508_RPM_PER_RAD_S                                                                    \
    (DJI_M3508_GEAR_RATIO * 60.0f / RM_TWO_PI) /* 输出轴 1 rad/s 的转子 rpm */

typedef enum
{
    MOTOR_M3508,  /* C620 电调 */
    MOTOR_M2006,  /* C610 电调 */
    MOTOR_GM6020, /* 只有反馈，没有指令（ADR 0031） */
    MOTOR_DM, /* 达妙各型号（DM4310、DM8009…），差异在 DmConfig 里（ADR 0027） */
} MotorType;

/** 停机动作（运行时契约第 5 节）。DJI 电机只支持零力矩和失能，配置成阻尼时 motor_init() 拒绝 */
typedef enum
{
    SAFE_ACTION_ZERO_TORQUE, /* 不出力：DJI 发 0 电流；达妙 MIT 的 Kp、Kd、力矩全为 0 */
    SAFE_ACTION_DAMP, /* 只阻碍运动：达妙 MIT 只给 Kd（DmConfig.damp_kd），驱动器自己闭环 */
    SAFE_ACTION_DISABLE, /* 驱动器不输出：DJI 发 0 后停止发送；达妙发失能帧 */
} SafeAction;

/**
 * 达妙电机的参数。P_MAX / V_MAX / T_MAX 必须与驱动器里用上位机配置的完全一致，
 * 否则 MIT 帧的换算整体错位且不报错（《架构设计》达妙三条硬性要求）
 */
typedef struct
{
    uint16_t master_id; /* 反馈帧 ID（驱动器里配置的 Master ID），如 0x11 */
    float p_max;        /* 位置范围 ±p_max，rad */
    float v_max;        /* 速度范围 ±v_max，rad/s */
    float t_max;        /* 力矩范围 ±t_max，N·m */
    float damp_kd;      /* 阻尼停机时的 Kd，N·m·s/rad（0–5） */
} DmConfig;

/** 本车固定参数：写成robot_config.h 里的 const 配置表，运行中不变 */
typedef struct
{
    const char *name; /* 日志和设备清单里的名字 */
    MotorType type;
    CanBusId can_bus;
    uint8_t id; /* DJI 电调 ID：M3508 / M2006 为 1–8，GM6020 为 1–7；达妙为 CAN ID 1–15 */
    int8_t direction; /* +1 / -1：使输出轴正方向符合坐标系约定 */
    float
        gear_ratio; /* 转子 : 输出轴，如 DJI_M3508_GEAR_RATIO；直驱填 1。达妙反馈已是输出轴，必须填 1 */
    SafeAction stop_action; /* 全车停时发送出口改写成的动作 */
    DmConfig dm;            /* 只有 MOTOR_DM 使用 */
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
    uint8_t error_code; /* 驱动器上报的错误码，0 = 正常（DJI 没有；达妙为状态码 0x8–0xE） */
    bool enabled;      /* 驱动器已使能（DJI 电调没有使能概念，恒为 true） */
    bool online;       /* 读取时按 MOTOR_OFFLINE_TIMEOUT_MS 计算 */
    uint64_t stamp_us; /* 收到这帧的时刻 */
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

/** 达妙电机的私有状态 */
typedef struct
{
    bool
        want_enabled; /* 期望使能：motor_request_enable / disable 设置，离线时清除（control_task） */
    bool clear_requested; /* 这次使能请求还没发过清错（control_task） */
    uint64_t last_cmd_us; /* 上次发使能 / 失能 / 清错命令的时刻（control_task） */
    bool cmd_sent;        /* 发过命令，last_cmd_us 有效 */
} DmMotorState;

typedef struct MotorGroup MotorGroup;

/** 本周期一个电机最终发什么：motor_group_send() 第 1 步由 final_output() 算出，第 2 步按它编码 */
typedef enum
{
    MOTOR_OUT_TORQUE,      /* 发 MotorOutput.torque_nm */
    MOTOR_OUT_ZERO_TORQUE, /* 不出力 */
    MOTOR_OUT_DAMP,        /* 阻尼（只有达妙支持；DJI 按零力矩发） */
    MOTOR_OUT_DISABLE,     /* 失能：DJI 发一次 0 后停发；达妙发失能命令 */
} MotorOutputKind;

typedef struct
{
    MotorOutputKind kind;
    float torque_nm; /* kind 为 MOTOR_OUT_TORQUE 时有效 */
} MotorOutput;

/** 运行状态：只由 02_devices/motor/ 内部读写，其他文件不要直接访问 */
typedef struct Motor
{
    const MotorConfig *cfg;
    Watchdog wd;
    MotorFeedback fb; /* comm_rx_task 写，临界区保护 */
    union
    {
        DjiMotorState dji; /* 只在 comm_rx_task 里用 */
        DmMotorState dm;   /* 只在 control_task 里用 */
    } brand;

    /* 本周期的指令槽位：只在 control_task 里读写，motor_group_send() 发送后清空 */
    float torque_cmd_nm;
    bool torque_set;
    SafeAction safe_action;
    bool safe_set;
    MotorOutput out; /* motor_group_send() 第 1 步写、第 2 步读 */

    struct Motor *next; /* 所属电机组的链表 */
} Motor;

/**
 * @brief   初始化并加入电机组：检查配置、查 ID 冲突、登记看门狗
 * @param   conflict  失败原因是 ID 冲突时，指向冲突的那个电机，否则置 NULL（用于日志）
 * @return  false：配置不支持（ID 超范围、方向不是 ±1、减速比 ≤ 0、DJI 配了阻尼）、与组内电机的
 *          反馈 ID 或控制帧槽位冲突
 * @pre     初始化阶段调用；cfg 在整个运行期间有效
 */
RM_NODISCARD bool motor_init(Motor *m, const MotorConfig *cfg, MotorGroup *group,
                             const Motor **conflict);

MotorCaps motor_caps(const Motor *m);

/** 子系统在 xxx_init() 里检查一次，运行时不再检查 */
bool motor_supports_torque(const Motor *m);

/**
 * @brief   把一帧 CAN 交给这个电机：是它的反馈（总线和反馈 ID 都对上）就解码、存下、喂看门狗
 * @return  true：这帧是它的（长度不对的也算，丢弃不喂狗），调用方不用再交给别的电机；false：不是它的
 * @pre     只在 comm_rx_task 里调用（comm_rx_task.c）
 */
bool motor_receive(Motor *m, CanBusId bus, const CanFrame *frame);

/**
 * @brief   拷贝一份完整的反馈快照
 * @return  是否在线；离线时 out 仍是最后一帧的内容（从未收到时全为 0）
 */
RM_NODISCARD bool motor_read_feedback(const Motor *m, MotorFeedback *out);

/**
 * @brief   记下本周期的力矩指令（输出轴 N·m）——只记下，周期末尾 motor_group_send() 才编码发送；超出电调量程时截到量程
 * @note    本周期没调用的电机发零力矩；电机离线或有停机动作时，这个指令不会发出（motor_group.c 的 final_output()）
 * @pre     motor_supports_torque(m)；只在 control_task 里调用
 */
void motor_set_torque(Motor *m, float torque_nm);

/** 记下本周期的停机动作，优先于 motor_set_torque()；motor_group_send() 时才发出。只在 control_task 里调用 */
void motor_apply_safe_action(Motor *m, SafeAction action);

/**
 * @brief   请求使能（达妙；DJI 无操作）。电机报错时先清错一次再使能
 * @note    只由安全门允许动作后、所属子系统调用（进入 Manual 时）；离线后请求被清除，重新上线不会自动使能
 */
void motor_request_enable(Motor *m);

/** 请求失能（达妙；DJI 无操作） */
void motor_request_disable(Motor *m);

#ifdef __cplusplus
}
#endif
