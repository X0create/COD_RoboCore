/**
 * @file    shoot.c
 * @brief   发射机构的算法（说明见 shoot.h）
 */
#include "03_algorithm/shoot/shoot.h"

#include <math.h>

/* ================================================================== */
/* 热量限频（说明见 shoot.h 同名一节）                                  */
/* ================================================================== */

float shoot_rate_limit_hz(const ShootRateConfig *cfg, float heat_remaining, float cooling_per_s)
{
    if (heat_remaining >= cfg->full_heat)
    {
        return cfg->max_rate_hz;
    }
    if (heat_remaining <= cfg->stop_heat)
    {
        return 0.0f;
    }
    /* 冷却撑得住的射频 + 把多出 stop_heat 的热量在时间窗内用完 */
    const float rate = cooling_per_s / cfg->heat_per_shot
                       + (heat_remaining - cfg->stop_heat) / (cfg->heat_per_shot * cfg->window_s);
    return fminf(fmaxf(rate, 0.0f), cfg->max_rate_hz);
}

/* ================================================================== */
/* 本地热量估算（说明见 shoot.h 同名一节）                              */
/* ================================================================== */

void shoot_heat_step(ShootHeat *h, unsigned shots, float heat_per_shot, float cooling_per_s,
                     float dt_s)
{
    h->heat += (float)shots * heat_per_shot - cooling_per_s * dt_s;
    h->heat = fmaxf(h->heat, 0.0f);
}

void shoot_heat_sync(ShootHeat *h, float referee_heat)
{
    h->heat = fmaxf(h->heat, referee_heat);
}

/* ================================================================== */
/* 卡弹检测与反转（说明见 shoot.h 同名一节）                            */
/* ================================================================== */

float shoot_jam_step(ShootJam *j, const ShootJamConfig *cfg, float target_rad_s, float torque_nm,
                     bool shot, float dt_s)
{
    if (j->reversing)
    {
        j->reverse_s += dt_s;
        const bool released = fabsf(torque_nm) < cfg->release_torque_nm || shot;
        if (j->reverse_s >= cfg->reverse_max_s || (j->reverse_s >= cfg->reverse_min_s && released))
        {
            j->reversing = false; /* 恢复正常拨弹，计时从头开始 */
            j->over_torque_s = 0.0f;
            j->no_shot_s = 0.0f;
        }
        else
        {
            return -cfg->reverse_speed_rad_s;
        }
    }

    /* 力矩一直超阈值：拨弹盘被卡住 */
    j->over_torque_s =
        (fabsf(torque_nm) >= cfg->torque_threshold_nm) ? j->over_torque_s + dt_s : 0.0f;
    /* 一直在拨弹却没出弹：弹丸卡在拨弹盘和枪管之间（力矩不一定大） */
    j->no_shot_s = (target_rad_s != 0.0f && !shot) ? j->no_shot_s + dt_s : 0.0f;

    const bool stalled = j->over_torque_s >= cfg->jam_time_s;
    const bool starved = cfg->no_shot_time_s > 0.0f && j->no_shot_s >= cfg->no_shot_time_s;
    if (target_rad_s != 0.0f && (stalled || starved))
    {
        j->reversing = true;
        j->reverse_s = 0.0f;
        j->jam_count++;
        return -cfg->reverse_speed_rad_s;
    }
    return target_rad_s;
}

/* ================================================================== */
/* 开火判定（说明见 shoot.h 同名一节）                                  */
/* ================================================================== */

bool shoot_on_target(float yaw_err_rad, float pitch_err_rad, float distance_m, float half_width_m,
                     float half_height_m)
{
    return fabsf(yaw_err_rad) < atanf(half_width_m / distance_m)
           && fabsf(pitch_err_rad) < atanf(half_height_m / distance_m);
}

/* ================================================================== */
/* 弹速自适应（说明见 shoot.h 同名一节）                                */
/* ================================================================== */

float shoot_speed_adapt(const ShootSpeedConfig *cfg, float target_rad_s, float muzzle_speed_m_s)
{
    if (muzzle_speed_m_s >= cfg->speed_valid_m_s && muzzle_speed_m_s < cfg->speed_low_m_s)
    {
        target_rad_s += cfg->step_up_rad_s;
    }
    else if (muzzle_speed_m_s > cfg->speed_high_m_s)
    {
        target_rad_s -= cfg->step_down_rad_s;
    }
    return fminf(fmaxf(target_rad_s, cfg->target_min_rad_s), cfg->target_max_rad_s);
}
