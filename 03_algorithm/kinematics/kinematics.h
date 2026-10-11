/**
 * @file    kinematics.h
 * @brief   底盘运动学：底盘速度 ↔ 各轮转速 / 朝向（纯计算，电脑上可测）
 * @note    按节排列：底盘速度（各种轮组共用的输入输出类型）、全向轮 omni、麦轮 mecanum、舵轮 steer、半舵半全向 half_steer。
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ================================================================== */
/* 底盘速度（各种轮组共用的输入输出类型）
 *
 * 底盘速度：全向轮、麦轮、舵轮运动学共用的输入输出类型
 * 底盘系 C（《架构设计》约定表）：原点在底盘几何中心，X 车头、Y 左、Z 上，俯视逆时针为正。
 */

typedef struct
{
    float vx_m_s;   /* 向前 */
    float vy_m_s;   /* 向左 */
    float wz_rad_s; /* 俯视逆时针 */
} ChassisVel;

/* ================================================================== */
/* 全向轮 omni
 *
 * 四轮全向轮底盘运动学：底盘速度 ↔ 各轮转速（纯计算，电脑上可测）
 * 几何约定（俯视，底盘系 C）：
 * - 四个轮子到底盘中心的距离都是 center_dist_m，相邻两轮相差 90°；
 * - 轮 0 在 first_wheel_rad 方向上（从 X 轴逆时针量），轮 1、2、3 依次逆时针排列。
 *   常见的 X 形布置：first_wheel_rad = π/4，轮 0–3 = 左前、左后、右后、右前；
 *   十字形布置：first_wheel_rad = 0，轮 0–3 = 前、左、后、右；
 * - 轮子正转 = 这个轮子推动底盘绕中心逆时针转（沿切向）。电机输出轴正方向要用
 *   MotorConfig.direction 调成与此一致。
 * 轮 i 的转速：ω_i = (−sin θ_i · vx + cos θ_i · vy + R · wz) / r。
 */

#define OMNI_WHEELS 4u

typedef struct
{
    float wheel_radius_m;  /* 全向轮半径 */
    float center_dist_m;   /* 轮子接地点到底盘中心的距离 */
    float first_wheel_rad; /* 轮 0 所在方向，从 X 轴逆时针量 */
} OmniConfig;

typedef struct
{
    OmniConfig cfg;
    float sin_theta[OMNI_WHEELS]; /* 各轮方向角的正弦、余弦，初始化时算好 */
    float cos_theta[OMNI_WHEELS];
} Omni;

/** @pre wheel_radius_m > 0、center_dist_m > 0 */
void omni_init(Omni *omni, const OmniConfig *cfg);

/** 逆解：底盘速度 → 各轮转速（rad/s，正方向见文件说明） */
void omni_inverse(const Omni *omni, const ChassisVel *vel, float wheel_rad_s[OMNI_WHEELS]);

/**
 * @brief   正解：各轮转速 → 底盘速度
 * @note    四个轮子只有三个自由度，按最小二乘求解：轮子打滑、四个转速互相矛盾时得到“最接近”的底盘速度
 */
void omni_forward(const Omni *omni, const float wheel_rad_s[OMNI_WHEELS], ChassisVel *vel);

/* ================================================================== */
/* 麦轮 mecanum
 *
 * 四轮麦克纳姆轮底盘运动学：底盘速度 ↔ 各轮转速（纯计算，电脑上可测）
 * 几何约定（俯视，底盘系 C）：
 * - 轮 0–3 = 左前、左后、右后、右前（逆时针，与全向轮一节的 X 形布置同序），接地点在
 *   (±half_wheelbase_m, ±half_track_m)；
 * - 轮子正转 = 这个轮子把底盘往前推。电机输出轴正方向要用 MotorConfig.direction 调成与此一致；
 * - 辊子按常见的 O 形安装：从上往下看，四个轮子上方的辊子围成 O 形（接地辊子的轴线都指向
 *   底盘中心）。此时向左平移，左前、右后轮反转，左后、右前轮正转。装成 X 形时不能正常横移、
 *   旋转，上板第一次横移就能看出来。
 * 设 k = half_wheelbase_m + half_track_m：
 *   ω_0 = (vx − vy − k·wz) / r，ω_1 = (vx + vy − k·wz) / r，
 *   ω_2 = (vx − vy + k·wz) / r，ω_3 = (vx + vy + k·wz) / r。
 */

#define MECANUM_WHEELS 4u

typedef struct
{
    float wheel_radius_m;   /* 麦轮半径 */
    float half_wheelbase_m; /* 前后轮轴距的一半 */
    float half_track_m;     /* 左右轮距的一半 */
} MecanumConfig;

/** 逆解：底盘速度 → 各轮转速（rad/s） @pre 各尺寸 > 0 */
void mecanum_inverse(const MecanumConfig *cfg, const ChassisVel *vel,
                     float wheel_rad_s[MECANUM_WHEELS]);

/** 正解：各轮转速 → 底盘速度；四个轮速互相矛盾（打滑）时取最小二乘解 */
void mecanum_forward(const MecanumConfig *cfg, const float wheel_rad_s[MECANUM_WHEELS],
                     ChassisVel *vel);

/* ================================================================== */
/* 舵轮 steer
 *
 * 四轮舵轮底盘运动学：底盘速度 ↔ 各轮朝向与转速（纯计算，电脑上可测）
 * 几何约定（俯视，底盘系 C）：
 * - 轮 0–3 = 左前、左后、右后、右前（逆时针，与麦轮一节同序），转向轴在
 *   (±half_wheelbase_m, ±half_track_m)；
 * - 轮子朝向 heading：轮子向前滚动的方向与底盘 X 轴的夹角，俯视逆时针为正；
 * - 轮子正转 = 沿 heading 方向滚动。
 * 轮 i 转向轴处的速度：v_i = (vx − wz·y_i, vy + wz·x_i)。
 * 逆解给出的目标朝向离当前朝向最多 90°：需要转过 90° 以上时，改为反向转动轮子、转向另一侧，
 * 转向电机转得少、换向快；目标朝向是连续值（当前朝向 + 差值），不会在 ±π 处跳变。
 */

#define STEER_WHEELS 4u

/** 转向轴速度低于这个值（m/s）时，这个轮子保持当前朝向、转速为 0（速度为 0 时朝向没有意义） */
#define STEER_MIN_SPEED_M_S 1e-3f

typedef struct
{
    float wheel_radius_m;   /* 轮子半径 */
    float half_wheelbase_m; /* 前后转向轴距离的一半 */
    float half_track_m;     /* 左右转向轴距离的一半 */
} SteerConfig;

typedef struct
{
    float heading_rad; /* 轮子朝向 */
    float speed_rad_s; /* 轮子转速 */
} SteerWheel;

/**
 * @brief   单个舵轮的逆解：转向轴在底盘系 (x_m, y_m) 处的轮子 → 目标朝向和转速
 * @param   heading_rad  这个轮子的当前朝向（可以是多圈的连续值）
 * @note    steer_inverse() 和半舵半全向（下一节）都用它
 */
SteerWheel steer_wheel_inverse(float x_m, float y_m, float wheel_radius_m, const ChassisVel *vel,
                               float heading_rad);

/**
 * @brief   逆解：底盘速度 → 各轮目标朝向和转速
 * @param   heading_rad  各轮当前朝向（可以是多圈的连续值）
 * @pre     各尺寸 > 0
 */
void steer_inverse(const SteerConfig *cfg, const ChassisVel *vel,
                   const float heading_rad[STEER_WHEELS], SteerWheel out[STEER_WHEELS]);

/** 正解：各轮朝向和转速 → 底盘速度；各轮互相矛盾（打滑、朝向没转到位）时取最小二乘解 */
void steer_forward(const SteerConfig *cfg, const SteerWheel wheel[STEER_WHEELS], ChassisVel *vel);

/* ================================================================== */
/* 半舵半全向 half_steer
 *
 * 半舵半全向底盘运动学：一条对角线上两个舵轮、另一条对角线上两个全向轮（纯计算，电脑上可测）
 * 几何约定（俯视，底盘系 C，与舵轮一节相同）：
 * - 轮 0–3 = 左前、左后、右后、右前（逆时针），四个轮子的接地点在 (±half_wheelbase_m, ±half_track_m)；
 * - 舵轮：朝向 heading 是轮子向前滚动的方向与 X 轴的夹角，逆时针为正；正转 = 沿 heading 滚动；
 *   逆解规则同舵轮一节（最多转 90°，超过就反转轮子）；
 * - 全向轮：沿切向安装（滚动方向垂直于它到中心的连线，正方形底盘就是 45° 斜装），
 *   正转 = 推动底盘绕中心逆时针转，与全向轮一节相同。
 * 轮 i 接地点的速度：v_i = (vx − wz·y_i, vy + wz·x_i)。
 * 全向轮 i 的转速：ω_i = (−y_i·vx + x_i·vy + R_i²·wz) / (R_i·r_omni)，R_i 是它到中心的距离。
 */

#define HALF_STEER_WHEELS 4u

/** 舵轮在哪条对角线上（另一条是全向轮） */
typedef enum
{
    HALF_STEER_LF_RB, /* 舵轮：左前（轮 0）、右后（轮 2）；全向轮：左后、右前 */
    HALF_STEER_LB_RF, /* 舵轮：左后（轮 1）、右前（轮 3）；全向轮：左前、右后 */
} HalfSteerDiagonal;

typedef struct
{
    HalfSteerDiagonal steer_diagonal;
    float steer_radius_m;   /* 舵轮半径 */
    float omni_radius_m;    /* 全向轮半径 */
    float half_wheelbase_m; /* 前后轮接地点距离的一半 */
    float half_track_m;     /* 左右轮接地点距离的一半 */
} HalfSteerConfig;

/** 轮 wheel（0–3）是不是舵轮 */
bool half_steer_is_steer(const HalfSteerConfig *cfg, unsigned wheel);

/**
 * @brief   逆解：底盘速度 → 各轮目标
 * @param   heading_rad  各轮当前朝向；只用舵轮的，全向轮的值不读
 * @param   out          舵轮：目标朝向和转速；全向轮：只有转速有意义（朝向原样填当前值）
 * @pre     各尺寸 > 0
 */
void half_steer_inverse(const HalfSteerConfig *cfg, const ChassisVel *vel,
                        const float heading_rad[HALF_STEER_WHEELS],
                        SteerWheel out[HALF_STEER_WHEELS]);

/**
 * @brief   正解：各轮实测 → 底盘速度
 * @param   wheel  舵轮用朝向和转速，全向轮只用转速
 * @note    舵轮每个给两个方程、全向轮每个给一个，共 6 个方程 3 个未知数，取最小二乘解（打滑时得到“最接近”的速度）
 */
void half_steer_forward(const HalfSteerConfig *cfg, const SteerWheel wheel[HALF_STEER_WHEELS],
                        ChassisVel *vel);

#ifdef __cplusplus
}
#endif
