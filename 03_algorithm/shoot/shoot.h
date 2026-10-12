/**
 * @file    shoot.h
 * @brief   发射机构的算法（纯计算，电脑上可测）：热量限频、本地热量估算、卡弹检测与反转、开火判定、弹速自适应
 * @note    按节排列，都用国际单位（rad、rad/s、N·m、m、s），热量用裁判系统的热量单位。
 *          逻辑在实车哨兵上用过；下面注释里的“参考值”是那台车的数值，换车时按实际情况调整。
 *          发射机构 01_applic/modules/shooter 还没有，先只有算法和测试。
 *          热量：每发一颗弹，枪管热量增加；热量超过上限会扣血，热量每秒按“冷却值”下降。限频就是为了不超热量。
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ================================================================== */
/* 热量限频                                                            */
/*
 * 按剩余热量（热量上限 − 当前热量）算允许的射频（发 / s）：
 *   剩余 ≥ full_heat：最高射频；剩余 ≤ stop_heat：0；
 *   中间：冷却能维持的射频（cooling / heat_per_shot）+ 把多出 stop_heat 的热量在 window_s 内用完的射频，
 *   上限 max_rate_hz。剩余热量接近 stop_heat 时射频趋近冷却能维持的值，热量不再往上涨。
 *   效果：热量充足时全速打，快到上限时自动降到“冷却多少就打多少”，不会超热量。
 */
/* ================================================================== */

typedef struct
{
    float heat_per_shot; /* 每发增加的热量：17 mm 为 10，42 mm 为 100 */
    float max_rate_hz;   /* 最高射频（拨弹机构和规则允许的） */
    float full_heat; /* 剩余热量不少于它就用最高射频（参考值 100，即 10 发 17 mm） */
    float stop_heat; /* 剩余热量不多于它就停（参考值 25，留 2 发多的余量防超热量） */
    float window_s; /* 中间段把多余热量用完的时间窗 */
} ShootRateConfig;

/**
 * @param   heat_remaining  热量上限 − 当前热量
 * @param   cooling_per_s   每秒冷却值（裁判系统 0x0201）
 * @return  允许的射频（发 / s），0 到 max_rate_hz
 * @pre     full_heat > stop_heat，heat_per_shot > 0，window_s > 0
 */
float shoot_rate_limit_hz(const ShootRateConfig *cfg, float heat_remaining, float cooling_per_s);

/* ================================================================== */
/* 本地热量估算                                                        */
/*
 * 裁判系统的热量（0x0202）约 10 Hz 更新、还有链路延迟，连发时会滞后几发。本地按“每发 + heat_per_shot、
 * 每秒 − cooling”自己算一份，收到裁判系统的热量时取两者较大的（保守，宁可少打也不超热量）
 */
/* ================================================================== */

typedef struct
{
    float heat; /* 当前估算热量，≥ 0 */
} ShootHeat;

/** 一个周期：发了 shots 发（通常 0 或 1，由出弹检测或裁判系统 0x0207 给出），并按冷却值递减 */
void shoot_heat_step(ShootHeat *h, unsigned shots, float heat_per_shot, float cooling_per_s,
                     float dt_s);

/** 收到裁判系统的热量：取估算值和裁判值中较大的 */
void shoot_heat_sync(ShootHeat *h, float referee_heat);

/* ================================================================== */
/* 卡弹检测与反转                                                      */
/*
 * 判卡弹（任一）：拨弹力矩 ≥ torque_threshold_nm 持续 jam_time_s；或一直在拨弹却 no_shot_time_s 没出弹
 * （出弹靠裁判系统的发射事件或测速模块判断；no_shot_time_s 为 0 时不用这一条）。
 * 卡弹后反转：至少 reverse_min_s；之后力矩降到 release_torque_nm 以下、检测到出弹，或满 reverse_max_s 就恢复。
 * 参考值（按电调电流原始值）：9800 持续 150 ms 判卡弹，反转 100–150 ms，降到 6000 以下恢复（按所用电机换算成 N·m 再填）
 */
/* ================================================================== */

typedef struct
{
    float torque_threshold_nm;
    float jam_time_s;
    float no_shot_time_s;      /* 0 = 不用“没出弹”这一条 */
    float reverse_speed_rad_s; /* 反转时拨弹盘的转速（取正值，内部取反） */
    float reverse_min_s;
    float reverse_max_s;
    float release_torque_nm;
} ShootJamConfig;

typedef struct
{
    bool reversing;
    float over_torque_s; /* 力矩一直超阈值的时间 */
    float no_shot_s;     /* 一直在拨弹却没出弹的时间 */
    float reverse_s;     /* 已反转的时间 */
    unsigned jam_count;  /* 判卡弹的次数（调试用） */
} ShootJam;

/**
 * @brief   每个周期调用一次，返回拨弹盘实际该用的目标转速
 * @param   target_rad_s  正常拨弹的目标转速（不拨弹时为 0）
 * @param   torque_nm     拨弹电机的实测力矩
 * @param   shot          本周期检测到出弹
 */
float shoot_jam_step(ShootJam *j, const ShootJamConfig *cfg, float target_rad_s, float torque_nm,
                     bool shot, float dt_s);

/* ================================================================== */
/* 开火判定                                                            */
/*
 * 云台指向与目标方向的误差在装甲板张角以内才开火：|误差| < atan(半宽 / 距离)。
 * 参考值：半宽 0.0825 m（小装甲板宽 0.165 m）；俯仰方向可给很大的半高（如 1.5 m），只看航向
 */
/* ================================================================== */

/**
 * @param   yaw_err_rad、pitch_err_rad  目标方向 − 当前指向（视觉给出的、扣掉控制延时的目标）
 * @param   distance_m  目标距离，> 0
 */
bool shoot_on_target(float yaw_err_rad, float pitch_err_rad, float distance_m, float half_width_m,
                     float half_height_m);

/* ================================================================== */
/* 弹速自适应                                                          */
/*
 * 每拿到一次实测弹速（裁判系统 0x0207，每发一次），偏低就把摩擦轮目标转速调高 step_up，偏高就调低 step_down，
 * 并限制在 [min, max]。参考值（17 mm）：目标弹速 22.5–23.0 m/s，低于 8 m/s 的读数当作无效
 * 为什么要自适应：摩擦轮磨损、温度、弹丸批次都会让弹速漂移，固定转速打不准，太高还会超速扣血。
 */
/* ================================================================== */

typedef struct
{
    float speed_low_m_s;   /* 低于它就调高 */
    float speed_high_m_s;  /* 高于它就调低 */
    float speed_valid_m_s; /* 低于它的读数无效（没出弹或测速失败），不调 */
    float step_up_rad_s;
    float step_down_rad_s;
    float target_min_rad_s; /* 摩擦轮目标转速范围 */
    float target_max_rad_s;
} ShootSpeedConfig;

/** @return 新的摩擦轮目标转速 */
float shoot_speed_adapt(const ShootSpeedConfig *cfg, float target_rad_s, float muzzle_speed_m_s);

#ifdef __cplusplus
}
#endif
