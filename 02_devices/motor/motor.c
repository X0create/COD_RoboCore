/**
 * @file    motor.c
 * @brief   电机的全部代码，见 motor.h。从上往下：
 *          1. 型号常数 → 2. 初始化与 ID 查重 → 3. 接收：解码反馈 → 4. 读反馈、写指令 → 5. 发送：确定指令、编码、发出
 * @note    DJI 反馈帧：角度、转速、电流、温度按大端排在 8 字节里，解码后换算成输出轴国际单位；
 *          达妙 MIT 帧：浮点按 P/V/T_MAX 范围压成定长整数传输，压缩前先截到范围内
 *          （不截的话超出范围会绕回，负的最大力矩可能变成正的最大力矩）。
 */
#include "02_devices/motor/motor.h"

#include <stddef.h>

#include "04_core/os/critical.h"
#include "05_platform/time/time.h"

/* ================================================================== */
/* 1. 型号常数                                                         */
/* ================================================================== */

#define ENCODER_HALF  (DJI_ENCODER_COUNTS / 2)
#define RPM_TO_RAD_S  (RM_TWO_PI / 60.0f)
#define RAD_PER_COUNT (RM_TWO_PI / (float)DJI_ENCODER_COUNTS)

/*
 * DJI 每种型号一行常数。力矩常数折算到转子（输出轴常数 ÷ 原装减速比），这样拆掉减速箱时只改 gear_ratio。
 * - M3508 / C620：电流 ±16384 对应 ±20 A；输出轴 0.3 N·m/A（原装减速比 3591/187）
 * - M2006 / C610：电流 ±10000 对应 ±10 A；输出轴 0.18 N·m/A（原装 36:1）。用前对照大疆手册核对
 * - GM6020：电流模式，指令和反馈都是 ±16384 对应 ±3 A；直驱，0.741 N·m/A（DJI 手册，用前核对）
 */
typedef struct
{
    float amps_per_raw;  /* 电流原始值 → A（反馈和指令共用） */
    int16_t cmd_max_raw; /* 电流指令量程（原始值） */
    float kt_rotor;      /* 转子力矩常数 N·m/A */
    bool has_temperature;
} DjiTypeParams;

static const DjiTypeParams dji_params[] = {
    [MOTOR_M3508] = { DJI_C620_MAX_A / (float)DJI_C620_RAW_MAX, DJI_C620_RAW_MAX,
                      DJI_M3508_NM_PER_A / DJI_M3508_GEAR_RATIO, true },
    [MOTOR_M2006] = { 10.0f / 10000.0f, 10000, 0.18f / DJI_M2006_GEAR_RATIO, false },
    [MOTOR_GM6020] = { 3.0f / 16384.0f, 16384, 0.741f, true },
};

/* 达妙 MIT 帧里 Kp、Kd 的范围（协议固定） */
#define DM_KP_MAX 500.0f
#define DM_KD_MAX 5.0f

/* 达妙命令帧：前 7 字节 0xFF，最后一字节是命令 */
#define DM_CMD_ENABLE      0xFCu
#define DM_CMD_DISABLE     0xFDu
#define DM_CMD_CLEAR_ERROR 0xFBu

/* 达妙状态码（反馈第 0 字节高 4 位）：0 失能、1 使能、0x8 过压 … 0xD 通信丢失、0xE 过载 */
#define DM_STATE_ENABLED   0x1u
#define DM_STATE_ERROR_MIN 0x8u

/* 达妙两条命令（使能、清错）之间至少间隔这么久：等反馈确认，没确认就在这之后重发（CAN 偶尔丢帧时能自己恢复） */
#define DM_CMD_INTERVAL_US 20000u

/*
 * DJI 电调在哪个控制帧的第几个槽位（frame 0–3 对应下表的 4 个帧 ID，slot 0–3）：
 * C6x0（M3508 / M2006）1–4 号在 0x200、5–8 号在 0x1FF；
 * GM6020 用电流模式：1–4 号在 0x1FE、5–7 号在 0x2FE（电压模式的 0x1FF / 0x2FF 不用）。
 * GM6020 n 号也认 0x1FF 的电压指令，而 0x1FF 是 C6x0 5–8 号的控制帧：两者正好也是同一个反馈 ID（0x204 + n），
 * 反馈 ID 查重已经把这种组合拒掉，不会出现 C6x0 的指令被 GM6020 当成电压的情况
 */
static void dji_ctrl_slot(const MotorConfig *cfg, uint8_t *frame, uint8_t *slot)
{
    const uint8_t index = (uint8_t)(cfg->id - 1u); /* 0–7 */
    const uint8_t first_frame = cfg->type == MOTOR_GM6020 ? 2u : 0u;
    *frame = (uint8_t)(first_frame + index / 4u);
    *slot = (uint8_t)(index % 4u);
}

static const uint32_t dji_ctrl_frame_id[DJI_CTRL_FRAMES] = { 0x200u, 0x1FFu, 0x1FEu, 0x2FEu };

/* 反馈帧 ID：C6x0 为 0x200 + ID（0x201–0x208），GM6020 为 0x204 + ID（0x205–0x20B），达妙为配置的 Master ID */
static uint32_t feedback_id(const MotorConfig *cfg)
{
    switch (cfg->type)
    {
        case MOTOR_DM:
            return cfg->dm.master_id;
        case MOTOR_GM6020:
            return 0x204u + cfg->id;
        default:
            return 0x200u + cfg->id;
    }
}

/* ================================================================== */
/* 2. 初始化与 ID 查重                                                  */
/* ================================================================== */

/*
 * 配置检查。DJI：ID 在拨码范围内（C6x0 1–8、GM6020 1–7）、方向 ±1、减速比为正；电调做不了阻尼，停机动作只能零力矩或失能。
 * 达妙：CAN ID 1–15、方向 ±1；驱动器反馈的就是输出轴，减速比必须是 1；位置 / 速度 / 力矩量程要和驱动器上位机里设置的一致；
 * Master ID（反馈帧 ID）不能和 CAN ID 相同
 */
static bool config_valid(const MotorConfig *cfg)
{
    const bool dir_ok = cfg->direction == 1 || cfg->direction == -1;
    if (cfg->type == MOTOR_DM)
    {
        const DmConfig *dm = &cfg->dm;
        return dir_ok && cfg->id >= 1u && cfg->id <= 0x0Fu && cfg->gear_ratio == 1.0f
               && dm->p_max > 0.0f && dm->v_max > 0.0f && dm->t_max > 0.0f && dm->damp_kd >= 0.0f
               && dm->damp_kd <= DM_KD_MAX && dm->master_id >= 1u && dm->master_id <= CAN_STD_ID_MAX
               && dm->master_id != cfg->id;
    }
    const uint8_t max_id = cfg->type == MOTOR_GM6020 ? 7u : 8u;
    return dir_ok && cfg->id >= 1u && cfg->id <= max_id && cfg->gear_ratio > 0.0f
           && (cfg->stop_action == STOP_ACTION_ZERO_TORQUE
               || cfg->stop_action == STOP_ACTION_DISABLE);
}

/* 这个电机在总线上占用的 ID：DJI 为反馈 ID 和控制帧 ID，达妙为 CAN ID 和 Master ID */
static void bus_ids(const MotorConfig *cfg, uint32_t ids[2])
{
    if (cfg->type == MOTOR_DM)
    {
        ids[0] = cfg->id;
        ids[1] = cfg->dm.master_id;
        return;
    }
    uint8_t frame, slot;
    dji_ctrl_slot(cfg, &frame, &slot);
    ids[0] = feedback_id(cfg);
    ids[1] = dji_ctrl_frame_id[frame];
}

/*
 * 同一路 CAN 上的冲突：
 * - 两个 DJI：反馈 ID 相同，或占同一个控制帧槽位（它们可以共用一个控制帧）；
 * - 其余组合：占用的 ID 有重合。
 */
static bool conflicts(const MotorConfig *a, const MotorConfig *b)
{
    if (a->can_bus != b->can_bus)
    {
        return false;
    }
    if (a->type != MOTOR_DM && b->type != MOTOR_DM)
    {
        uint8_t frame_a, slot_a, frame_b, slot_b;
        dji_ctrl_slot(a, &frame_a, &slot_a);
        dji_ctrl_slot(b, &frame_b, &slot_b);
        return feedback_id(a) == feedback_id(b) || (frame_a == frame_b && slot_a == slot_b);
    }
    uint32_t ia[2], ib[2];
    bus_ids(a, ia);
    bus_ids(b, ib);
    return ia[0] == ib[0] || ia[0] == ib[1] || ia[1] == ib[0] || ia[1] == ib[1];
}

bool motor_init(Motor *m, const MotorConfig *cfg, MotorGroup *group, const Motor **conflict)
{
    *conflict = NULL;
    if (!config_valid(cfg))
    {
        return false;
    }
    for (const Motor *other = group->head; other != NULL; other = other->next)
    {
        if (conflicts(cfg, other->cfg))
        {
            *conflict = other;
            return false;
        }
    }

    *m = (Motor){ .cfg = cfg };
    watchdog_register(&m->wd, cfg->name, MOTOR_TIMEOUT_MS);
    m->next = group->head;
    group->head = m;
    return true;
}

const Motor *motor_group_find_can_id(const MotorGroup *group, CanBusId bus, uint32_t id)
{
    for (const Motor *m = group->head; m != NULL; m = m->next)
    {
        uint32_t ids[2];
        bus_ids(m->cfg, ids);
        if (m->cfg->can_bus == bus && (ids[0] == id || ids[1] == id))
        {
            return m;
        }
    }
    return NULL;
}

/* ================================================================== */
/* 3. 接收：解码反馈（comm_rx_task）                                     */
/* ================================================================== */

/* DJI 反馈帧：[0–1] 编码器 0–8191 | [2–3] 转速 rpm | [4–5] 实际电流原始值 | [6] 温度 °C，都是大端，都是转子侧的值 */
static void dji_decode(const MotorConfig *cfg, DjiMotorState *st, const uint8_t d[8],
                       MotorFeedback *out)
{
    const DjiTypeParams *tp = &dji_params[cfg->type];
    const uint16_t encoder = (uint16_t)(((uint16_t)d[0] << 8) | d[1]) % DJI_ENCODER_COUNTS;
    const int16_t rpm = (int16_t)(((uint16_t)d[2] << 8) | d[3]);
    const int16_t current_raw = (int16_t)(((uint16_t)d[4] << 8) | d[5]);
    const float dir = (float)cfg->direction;

    /* 多圈计数：带减速箱的型号上电位置未知，以第一帧为 0；GM6020 直驱，编码器就是输出轴位置。
     * 相邻两帧转子最多转半圈（M3508 空载约 9000 rpm，1 ms 转 0.15 圈），跨过半圈就是过零 */
    if (!st->have_last)
    {
        st->zero_encoder = cfg->type == MOTOR_GM6020 ? 0u : encoder;
        st->turns = 0;
        st->have_last = true;
    }
    else
    {
        const int32_t delta = (int32_t)encoder - (int32_t)st->last_encoder;
        if (delta > ENCODER_HALF)
        {
            st->turns--;
        }
        else if (delta < -ENCODER_HALF)
        {
            st->turns++;
        }
    }
    st->last_encoder = encoder;

    /* 圈数和圈内计数分开换算成 float，不做累加，长时间运行不丢精度 */
    const float rotor_rad = (float)st->turns * RM_TWO_PI
                            + (float)((int32_t)encoder - (int32_t)st->zero_encoder) * RAD_PER_COUNT;
    const int32_t signed_count =
        (encoder >= ENCODER_HALF) ? (int32_t)encoder - DJI_ENCODER_COUNTS : encoder;

    /* 换算到输出轴国际单位：角度、转速除以减速比，力矩 = 电流 × 转子力矩常数 × 减速比；乘 direction 统一正方向 */
    out->angle_rad = dir * rotor_rad / cfg->gear_ratio;
    out->single_angle_rad = (float)signed_count * RAD_PER_COUNT;
    out->raw_encoder = encoder;
    out->speed_rad_s = dir * (float)rpm * RPM_TO_RAD_S / cfg->gear_ratio;
    out->torque_nm = dir * (float)current_raw * tp->amps_per_raw * tp->kt_rotor * cfg->gear_ratio;
    out->torque_is_estimate = true;
    out->temperature_c = tp->has_temperature ? (float)d[6] : 0.0f;
    out->error_code = cfg->type == MOTOR_GM6020 ? 0u : d[7]; /* C610/C620 错码；GM 此字节保留 */
    out->enabled = true;                                     /* DJI 电调没有使能概念 */
}

/* 整数 → 浮点：[0, 2^bits − 1] 线性映射到 [lo, hi]（旧代码 uint_to_float） */
static float uint_to_float(uint32_t x, float lo, float hi, uint32_t bits)
{
    return (float)x * (hi - lo) / (float)((1u << bits) - 1u) + lo;
}

/* 达妙反馈帧：[0] 高 4 位状态、低 4 位 ID | [1–2] 位置 16 位 | [3–4 高] 速度 12 位 | [4 低–5] 力矩 12 位 | [6] MOS 温度 | [7] 线圈温度 */
static void dm_decode(const MotorConfig *cfg, const uint8_t d[8], MotorFeedback *out)
{
    const DmConfig *dm = &cfg->dm;
    const float dir = (float)cfg->direction;
    const uint8_t state = d[0] >> 4;
    const uint32_t p = ((uint32_t)d[1] << 8) | d[2];
    const uint32_t v = ((uint32_t)d[3] << 4) | (d[4] >> 4);
    const uint32_t t = ((uint32_t)(d[4] & 0x0Fu) << 8) | d[5];
    const float pos = uint_to_float(p, -dm->p_max, dm->p_max, 16u);

    out->angle_rad = dir * pos; /* 驱动器给出的就是输出轴位置，范围 ±p_max */
    out->single_angle_rad = pos;
    out->raw_encoder = (uint16_t)p;
    out->speed_rad_s = dir * uint_to_float(v, -dm->v_max, dm->v_max, 12u);
    out->torque_nm = dir * uint_to_float(t, -dm->t_max, dm->t_max, 12u);
    out->torque_is_estimate = true;
    out->temperature_c = (float)d[7]; /* 线圈温度（d[6] 是 MOS 温度） */
    out->error_code = (state >= DM_STATE_ERROR_MIN) ? state : 0u;
    out->enabled = state == DM_STATE_ENABLED;
}

/*
 * 逐个问组里的电机：总线和反馈 ID 都对上才是它的。
 * 是它的 → 按品牌解码成输出轴国际单位 → 关中断，把反馈和中断里记下的接收时刻一起存下。
 */
bool motor_group_receive(MotorGroup *group, CanBusId bus, const CanFrame *frame)
{
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        if (bus != m->cfg->can_bus || frame->id != feedback_id(m->cfg))
        {
            continue; /* 不是这个电机的反馈 */
        }
        if (frame->len != 8u)
        {
            return true; /* 是它的 ID 但长度不对：丢弃、不喂狗 */
        }

        MotorFeedback fb;
        if (m->cfg->type == MOTOR_DM)
        {
            dm_decode(m->cfg, frame->data, &fb);
        }
        else
        {
            dji_decode(m->cfg, &m->brand.dji, frame->data, &fb);
        }
        fb.online = false; /* 在线与否由 motor_read_feedback() 读取时计算 */

        /* 反馈和接收时刻在同一个临界区里写，读的一方拿到的数据和时刻一定对得上。
         * 用中断里记下的时刻：任务被耽误、帧积压时，旧帧按真正的年龄判断，不会让电机“上线” */
        rm_critical_enter();
        fb.stamp_us = frame->stamp_us;
        fb.sequence = m->fb.sequence + 1u;
        m->fb = fb;
        watchdog_feed(&m->wd, frame->stamp_us);
        rm_critical_exit();
        return true;
    }
    return false;
}

/* ================================================================== */
/* 4. 读反馈、写指令（control_task）                                     */
/* ================================================================== */

bool motor_read_feedback(const Motor *m, MotorFeedback *out)
{
    rm_critical_enter();
    *out = m->fb;
    out->online = watchdog_is_online(&m->wd); /* 20 ms 内收到过反馈 */
    rm_critical_exit();
    return out->online;
}

void motor_set_torque(Motor *m, float torque_nm)
{
    /* 只记下本周期的指令，真正发送在 motor_group_send()；发完清空，下个周期不写就发零力矩 */
    m->torque_cmd_nm = torque_nm;
    m->torque_set = true;
}

void motor_group_read_tx(const MotorGroup *group, MotorTxSnapshot *out)
{
    *out = group->last_tx;
}

void motor_apply_stop_action(Motor *m, StopAction action)
{
    /* 停机动作优先于力矩指令（见 motor_group_send 第 1 步），同样只管本周期 */
    m->stop_action = action;
    m->stop_set = true;
}

/* 使能 / 失能只对达妙有意义（DJI 电调上电就能接收电流指令）；命令由 motor_group_send() 按间隔发出 */
void motor_request_enable(Motor *m)
{
    if (m->cfg->type == MOTOR_DM)
    {
        m->brand.dm.want_enabled = true;
        m->brand.dm.clear_requested = true;
    }
}

void motor_request_disable(Motor *m)
{
    if (m->cfg->type == MOTOR_DM)
    {
        m->brand.dm.want_enabled = false;
    }
}

void motor_group_apply_stop_all(MotorGroup *group)
{
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        motor_apply_stop_action(m, m->cfg->stop_action);
    }
}

/* ================================================================== */
/* 5. 发送：确定指令 → 编码 → 发出（control_task 周期末尾）              */
/* ================================================================== */

/*
 * 第 1 步：本周期这个电机最终发什么。全部规则只写在这里，按优先级从上往下：
 *   1. 有停机动作（子系统的机构停，或 motor_group_apply_stop_all() 的全车停）→ 执行停机动作
 *   2. 本周期没写力矩指令                                                → 零力矩，不保持上一帧
 *   3. 反馈离线（20 ms 没收到）                                          → 零力矩
 *   4. 否则                                                              → 发写入的力矩
 */
static MotorOutput final_output(const Motor *m)
{
    if (m->stop_set)
    {
        switch (m->stop_action)
        {
            case STOP_ACTION_ZERO_TORQUE:
                break;
            case STOP_ACTION_DAMP:
                return (MotorOutput){ .kind = MOTOR_OUT_DAMP };
            case STOP_ACTION_DISABLE:
                return (MotorOutput){ .kind = MOTOR_OUT_DISABLE };
        }
        return (MotorOutput){ .kind = MOTOR_OUT_ZERO_TORQUE };
    }
    if (!m->torque_set || !watchdog_is_online(&m->wd))
    {
        return (MotorOutput){ .kind = MOTOR_OUT_ZERO_TORQUE };
    }
    return (MotorOutput){ .kind = MOTOR_OUT_TORQUE, .torque_nm = m->torque_cmd_nm };
}

/*
 * 第 2、3 步（DJI）：一条总线上同一控制帧（0x200 / 0x1FF …）的电调共用一帧，每台占 2 字节大端电流原始值。
 * 力矩 → 电流原始值是反馈换算的逆运算，超出量程就截断；力矩以外的输出（零力矩、失能）都发 0。
 */
static void send_dji_frame(MotorGroup *group, CanBusId bus, uint8_t frame_index)
{
    CanFrame frame = { .id = dji_ctrl_frame_id[frame_index], .len = 8u, .is_fd = false };
    bool used = false;
    bool all_disabled = true;
    MotorTxSnapshot tx = { .bus = bus, .frame_id = frame.id };

    for (const Motor *m = group->head; m != NULL; m = m->next)
    {
        if (m->cfg->type == MOTOR_DM)
        {
            continue; /* 达妙每台一帧，见 send_dm_frame */
        }
        uint8_t f, slot;
        dji_ctrl_slot(m->cfg, &f, &slot);
        if (m->cfg->can_bus != bus || f != frame_index)
        {
            continue;
        }

        const DjiTypeParams *tp = &dji_params[m->cfg->type];
        const float max = (float)tp->cmd_max_raw;
        float raw = 0.0f;
        if (m->out.kind == MOTOR_OUT_TORQUE)
        {
            raw = (float)m->cfg->direction * m->out.torque_nm / (tp->kt_rotor * m->cfg->gear_ratio)
                  / tp->amps_per_raw;
            raw = raw > max ? max : (raw < -max ? -max : raw);
        }
        const int16_t raw_i16 = (int16_t)(raw >= 0.0f ? raw + 0.5f : raw - 0.5f); /* 四舍五入 */
        tx.raw_current[slot] = raw_i16;

        used = true;
        all_disabled = all_disabled && m->out.kind == MOTOR_OUT_DISABLE;
        frame.data[2u * slot] = (uint8_t)((uint16_t)raw_i16 >> 8);
        frame.data[2u * slot + 1u] = (uint8_t)((uint16_t)raw_i16 & 0xFFu);
    }

    if (!used)
    {
        return;
    }
    /* 帧里的电调全部失能：发过一次 0 之后就不再发，让电调自己进入无指令状态 */
    if (all_disabled && group->disabled_sent[bus][frame_index])
    {
        return;
    }
    tx.sequence = group->last_tx.sequence + 1u;
    tx.stamp_us = rm_time_now_us();
    tx.accepted = can_send(bus, &frame);
    group->last_tx = tx;
    if (!tx.accepted)
    {
        /* 队列满：丢帧，下个周期重新生成（控制指令只要最新的，不排队补发旧的） */
        group->tx_dropped++;
        return; /* 没发出去：“已发过 0”的记录不变，全部失能时下个周期重发 */
    }
    group->disabled_sent[bus][frame_index] = all_disabled;
}

/* 浮点 → 整数：把 [lo, hi] 线性映射到 [0, 2^bits − 1]（截尾取整），超出范围先截断（达妙官方例程同样的换算） */
static uint32_t float_to_uint(float x, float lo, float hi, uint32_t bits)
{
    x = (x < lo) ? lo : ((x > hi) ? hi : x);
    return (uint32_t)((x - lo) * (float)((1u << bits) - 1u) / (hi - lo));
}

/*
 * 达妙 MIT 帧：位置 16 位 | 速度 12 位 | Kp 12 位 | Kd 12 位 | 力矩 12 位，共 64 位按位紧挨着排；
 * 驱动器按 力矩 = Kp·(位置目标 − 位置) + Kd·(速度目标 − 速度) + 力矩目标 在电机里闭环。
 * 本框架只用阻尼（只给 Kd）和纯力矩，位置、速度、Kp 恒为 0（位置环、速度环都在主控里用 PID 做），
 * 所以只传 Kd 和力矩
 */
static void dm_encode_mit(const MotorConfig *cfg, float kd, float torque_nm, uint8_t out[8])
{
    const DmConfig *dm = &cfg->dm;
    const uint32_t p = float_to_uint(0.0f, -dm->p_max, dm->p_max, 16u);
    const uint32_t v = float_to_uint(0.0f, -dm->v_max, dm->v_max, 12u);
    const uint32_t kp_u = float_to_uint(0.0f, 0.0f, DM_KP_MAX, 12u);
    const uint32_t kd_u = float_to_uint(kd, 0.0f, DM_KD_MAX, 12u);
    const uint32_t t = float_to_uint((float)cfg->direction * torque_nm, -dm->t_max, dm->t_max, 12u);

    out[0] = (uint8_t)(p >> 8);
    out[1] = (uint8_t)p;
    out[2] = (uint8_t)(v >> 4);
    out[3] = (uint8_t)(((v & 0x0Fu) << 4) | (kp_u >> 8));
    out[4] = (uint8_t)kp_u;
    out[5] = (uint8_t)(kd_u >> 4);
    out[6] = (uint8_t)(((kd_u & 0x0Fu) << 4) | (t >> 8));
    out[7] = (uint8_t)t;
}

/*
 * 第 2、3 步（达妙）：每台每个周期必发一帧（驱动器只在收到帧时回反馈，不发就判断不了在不在线）。
 * 使能按“期望状态”对齐：期望与反馈不一致时先发命令（清错 / 使能 / 失能），两条命令至少间隔 DM_CMD_INTERVAL_US；
 * 不发命令的周期按第 1 步的输出发 MIT 帧。
 */
static void send_dm_frame(MotorGroup *group, Motor *m, uint64_t now_us)
{
    DmMotorState *st = &m->brand.dm;
    const MotorConfig *cfg = m->cfg;

    rm_critical_enter();
    const bool enabled = m->fb.enabled;
    const uint8_t error = m->fb.error_code;
    const bool online = watchdog_is_online(&m->wd);
    rm_critical_exit();

    if (!online || m->out.kind == MOTOR_OUT_DISABLE)
    {
        st->want_enabled = false; /* 离线后重新上线不自动使能；失能停机同样放弃使能 */
    }

    CanFrame frame = { .id = cfg->id, .len = 8u, .is_fd = can_bus_is_fd(cfg->can_bus) };
    const bool cmd_ready = !st->cmd_sent || now_us - st->last_cmd_us >= DM_CMD_INTERVAL_US;
    uint8_t cmd = 0u; /* 0：这个周期不发命令 */
    if (cmd_ready && online && !m->stop_set && st->want_enabled && error != 0u
        && st->clear_requested)
    {
        cmd = DM_CMD_CLEAR_ERROR; /* 这次使能请求只清一次错（发出去以后才算清过，见下面） */
    }
    else if (cmd_ready && online && !m->stop_set && st->want_enabled && !enabled && error == 0u)
    {
        cmd = DM_CMD_ENABLE;
    }
    else if (cmd_ready && online && !st->want_enabled && enabled)
    {
        cmd = DM_CMD_DISABLE;
    }

    if (cmd != 0u)
    {
        for (int i = 0; i < 7; i++) /* 命令帧：前 7 字节 0xFF，最后一字节是命令 */
        {
            frame.data[i] = 0xFFu;
        }
        frame.data[7] = cmd;
    }
    else if (m->out.kind == MOTOR_OUT_DAMP)
    {
        dm_encode_mit(cfg, cfg->dm.damp_kd, 0.0f, frame.data);
    }
    else if (m->out.kind == MOTOR_OUT_TORQUE)
    {
        dm_encode_mit(cfg, 0.0f, m->out.torque_nm, frame.data);
    }
    else
    {
        dm_encode_mit(cfg, 0.0f, 0.0f, frame.data); /* 零力矩：Kd、力矩全 0 */
    }

    if (!can_send(cfg->can_bus, &frame))
    {
        group->tx_dropped++;
        return; /* 没发出去：命令状态不变，下个周期重新判断、重发（清错请求不会白白用掉） */
    }
    if (cmd != 0u)
    {
        st->cmd_sent = true;
        st->last_cmd_us = now_us;
        if (cmd == DM_CMD_CLEAR_ERROR)
        {
            st->clear_requested = false;
        }
    }
}

void motor_group_send(MotorGroup *group)
{
    /* 1. 确定指令：每个电机本周期最终发什么（停机动作 > 没写指令 > 离线 > 力矩，见 final_output） */
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        m->out = final_output(m);
    }

    /* 2. 编码 → 3. 发送：DJI 按控制帧打包，达妙每台一帧 */
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        for (uint8_t f = 0u; f < DJI_CTRL_FRAMES; f++)
        {
            send_dji_frame(group, (CanBusId)bus, f);
        }
    }
    const uint64_t now_us = rm_time_now_us();
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        if (m->cfg->type == MOTOR_DM)
        {
            send_dm_frame(group, m, now_us);
        }
    }

    /* 4. 清理：本周期的指令只用一次，下个周期不再写就发零力矩 */
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        m->torque_set = false;
        m->stop_set = false;
    }
}
