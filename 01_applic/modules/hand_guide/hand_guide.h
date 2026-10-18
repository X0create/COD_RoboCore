/** @file hand_guide.h
 * 单电机手拖：只抵消一部分运动摩擦，不产生位置或速度目标。
 * 纯逻辑；任务负责提供设备快照、执行使能/失能和发送。
 */
#pragma once
#include "02_devices/motor/motor.h"
#include "02_devices/remote/dr16.h"

typedef struct
{
    float coulomb_nm;
    float fraction;
    float deadband_rad_s;
    float full_assist_rad_s;
    float arm_speed_rad_s;
    float speed_limit_rad_s;
    float min_temperature_c;
    float max_temperature_c;
    float max_period_s;
    float enable_timeout_s;
} HandGuideConfig;

typedef enum
{
    HAND_GUIDE_OFF,
    HAND_GUIDE_BASELINE,
    HAND_GUIDE_ASSIST
} HandGuideMode;
typedef enum
{
    HAND_GUIDE_OK,
    HAND_GUIDE_RC_LOST,
    HAND_GUIDE_MOTOR_LOST,
    HAND_GUIDE_DRIVER_ERROR,
    HAND_GUIDE_OVERSPEED,
    HAND_GUIDE_TEMPERATURE,
    HAND_GUIDE_LATE,
    HAND_GUIDE_TX_FAILED,
    HAND_GUIDE_ARM_MOVING,
    HAND_GUIDE_ENABLE_FAILED,
    HAND_GUIDE_DISABLED
} HandGuideFault;

typedef struct
{
    const HandGuideConfig *cfg;
    HandGuideMode mode;
    HandGuideFault fault;
    bool armed;
    bool seen_enabled;
    bool request_enable; /* 只在从停机进入基线的这一拍为 true */
    float waiting_s;
    float torque_nm;
} HandGuide;

/** 初始化检查一次配置；此后 cfg 始终有效。上电始终锁定。 */
bool hand_guide_init(HandGuide *self, const HandGuideConfig *cfg);
/** @pre 输入是设备层已校验的快照；dt_s 为本拍真实间隔，tx_failed 为上一拍发送失败。
 * 右拨杆：下=停机并重新准备，中=零力矩基线，上=补偿。
 * 必须先下→中，等驱动已使能且轴静止，再中→上；故障后回下才可重新准备。
 */
void hand_guide_update(HandGuide *self, const MotorFeedback *fb, bool rc_online, RcSwitch sw,
                       float dt_s, bool tx_failed);

/** 执行唯一输出出口；OFF 覆盖为失能，运行时 MIT Kp/Kd 均由 motor 层编码为 0。 */
void hand_guide_apply(const HandGuide *self, Motor *motor);
