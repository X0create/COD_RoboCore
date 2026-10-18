/** DM4340 裸电机专用台架；仅 h723-handguide 预设选用。 */
#pragma once
#include "01_applic/modules/hand_guide/hand_guide.h"
#include "03_algorithm/math/math_const.h"

/* 必须用达妙上位机核对实物；量程不匹配会让力矩解码整体错误。 */
static const MotorConfig hand_motor_config = {
    .name = "dm4340_hand",
    .type = MOTOR_DM,
    .can_bus = CAN_BUS_3,
    .id = 1u,
    .direction = 1,
    .gear_ratio = 1.0f,
    .stop_action = STOP_ACTION_DISABLE,
    .dm = { .master_id = 0x11u, .p_max = RM_PI, .v_max = 8.0f, .t_max = 40.0f }
};
/* Fc 来源见 docs/DM4340_HAND_GUIDE.md；其余为尚待台架验证的首测设置。
 * 温度范围限制复用已测工况；速度范围仅用于低速手拖，不能拿去追踪轨迹。
 */
static const HandGuideConfig hand_guide_config = { .coulomb_nm = 0.3714f,
                                                   .fraction = 0.25f,
                                                   .deadband_rad_s = 0.05f,
                                                   .full_assist_rad_s = 0.2f,
                                                   .arm_speed_rad_s = 0.08f,
                                                   .speed_limit_rad_s = 1.5f,
                                                   .min_temperature_c = 24.0f,
                                                   .max_temperature_c = 42.0f,
                                                   .max_period_s = 0.005f,
                                                   .enable_timeout_s = 0.2f };
#define HAND_GUIDE_SWITCH         1u
#define HAND_GUIDE_RECORD_DIVIDER 5u
extern Motor hand_motor;
extern MotorGroup hand_motors;
extern Dr16 hand_rc;
extern HandGuide hand_guide;
