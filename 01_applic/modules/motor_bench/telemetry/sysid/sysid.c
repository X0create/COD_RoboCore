/**
 * @file    sysid.c
 * @brief   系统辨识插件（用法见 sysid.h）
 *
 * 本文件分四部分，从下往上依次被调用：
 *   帧    打包与逐字节解析：AA | 载荷长度 (LE16) | 类型 | 数据 | CRC16 (LE16)，与 custom_control 机械臂的 USB
 *         帧相同，同一条字节流里可以混发两边的帧；
 *   激励  一次激励一个轴，三种信号：
 *         - 扫动（只用于位置目标）：先以最慢的速度档从开始位置走到中心（开始位置 + 偏移），在中心两侧 ±幅值
 *           之间匀速来回，每个速度档来回 cycles 次，换向按给定加速度做梯形加减速，最后回到开始位置。
 *           匀速段测重力和摩擦，加减速段测惯量；
 *         - 扫频：offset = 幅值 × w(t) × sin(相位)，频率从 f0 按对数升到 f1，w(t) 在开头和结尾各用
 *           min(1 s, 时长 / 10) 从 0 平滑升到 1、再降回 0。测频率响应（带宽、延迟、惯量）；
 *         - 阶跃：最多 4 段，每段 offset 保持一个值若干秒；最后一段结束后 offset 回到 0；
 *   记录  每个控制周期一条，自带通道表：开机后、电脑请求时、每隔 SYSID_LOG_SCHEMA_EVERY 条各发一次通道表，
 *         记录工具中途连上也能解码。控制任务（唯一的生产者）和通信任务（唯一的消费者）经环形缓冲区交接，
 *         不需要关中断；缓冲区满时丢弃新记录，序号照样递增，电脑端能看出丢在哪里；
 *   插件  sysid.h 里的函数：读车上的变量、收发字节、把三部分接起来。
 * 主机测试直接 #include 本文件，以便测试内部的三部分（firmware/tests/）。
 */
#include "sysid/sysid.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

/* ================================================================ 帧：打包与逐字节解析 */

#define SYSID_FRAME_HEADER   0xAAU
#define SYSID_FRAME_OVERHEAD 6U /* 帧头 1 + 长度 2 + 类型 1 + CRC16 2 */
#define SYSID_FRAME_MAX      (SYSID_FRAME_DATA_MAX + SYSID_FRAME_OVERHEAD)

/* 帧类型 */
#define SYSID_FRAME_SCHEMA         0x10U /* 板 → 电脑：通道表（每个通道的名称、单位、类型） */
#define SYSID_FRAME_RECORD         0x11U /* 板 → 电脑：一条记录（每个控制周期一条） */
#define SYSID_FRAME_EXCITE         0x12U /* 电脑 → 板：激励命令（开始 / 停止） */
#define SYSID_FRAME_SCHEMA_REQUEST 0x13U /* 电脑 → 板：请重发通道表，没有数据 */

#define SYSID_CRC16_INIT 0xFFFFU

/** 逐字节解析的状态。调用方提供存储，用 sysid_frame_parser_init() 清零。 */
typedef struct
{
    uint8_t buffer[SYSID_FRAME_MAX];
    uint16_t count;        /**< buffer 里已有的字节数（含刚交出的帧） */
    uint16_t frame_length; /**< 刚交出的帧的总字节数，下次 push 时先移走；0 = 没有 */
    uint32_t bad_frames; /**< 长度不合理或 CRC 错误而丢弃的帧数 */
} SysIdFrameParser;

/** @brief 刚解析出的帧的类型。@pre sysid_frame_parser_push() 刚返回 true。 */
static inline uint8_t sysid_frame_type(const SysIdFrameParser *self)
{
    return self->buffer[3];
}

/** @brief 刚解析出的帧的数据部分。@pre 同上。 */
static inline const uint8_t *sysid_frame_data(const SysIdFrameParser *self)
{
    return &self->buffer[4];
}

/** @brief 刚解析出的帧的数据字节数。@pre 同上。 */
static inline uint16_t sysid_frame_length(const SysIdFrameParser *self)
{
    const unsigned payload = (unsigned)self->buffer[1] | ((unsigned)self->buffer[2] << 8U);
    return (uint16_t)(payload - 2U);
}

#define CRC16_REFLECTED 0x8408U /* 0x1021 按位反射 */

static uint16_t sysid_crc16_update(uint16_t crc, const uint8_t *data, uint16_t length);

static uint16_t sysid_crc16(const uint8_t *data, uint16_t length)
{
    return sysid_crc16_update(SYSID_CRC16_INIT, data, length);
}

static uint16_t sysid_crc16_update(uint16_t crc, const uint8_t *data, uint16_t length)
{
    /* 逐位计算：一条 480 字节的记录约 4000 次移位，比查表慢但不占 512 字节的表，在传输任务里足够快。 */
    for (uint16_t i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ CRC16_REFLECTED) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static uint16_t sysid_frame_encode(uint8_t type, const uint8_t *data, uint16_t length, uint8_t *out)
{
    const uint16_t payload = (uint16_t)(length + 2U);
    const uint16_t crc = sysid_crc16(data, length);
    out[0] = SYSID_FRAME_HEADER;
    out[1] = (uint8_t)payload;
    out[2] = (uint8_t)(payload >> 8);
    out[3] = type;
    memcpy(&out[4], data, length);
    out[4U + length] = (uint8_t)crc;
    out[5U + length] = (uint8_t)(crc >> 8);
    return (uint16_t)(length + SYSID_FRAME_OVERHEAD);
}

static void sysid_frame_parser_init(SysIdFrameParser *self)
{
    memset(self, 0, sizeof(*self));
}

/* 丢掉缓冲区开头的一个字节，后面的字节前移，重新从下一个字节找帧头。 */
static void drop_first_byte(SysIdFrameParser *self)
{
    --self->count;
    memmove(self->buffer, &self->buffer[1], self->count);
}

static bool sysid_frame_parser_push(SysIdFrameParser *self, uint8_t byte)
{
    if (self->frame_length > 0U)
    {
        /* 移走上次交出的帧。重新找帧头之后，帧的后面可能还跟着已收到的字节，它们留下来接着解析 */
        self->count = (uint16_t)(self->count - self->frame_length);
        memmove(self->buffer, &self->buffer[self->frame_length], self->count);
        self->frame_length = 0U;
    }
    self->buffer[self->count++] = byte;
    while (self->count > 0U)
    {
        if (self->buffer[0] != SYSID_FRAME_HEADER)
        {
            drop_first_byte(self);
            continue;
        }
        if (self->count < 3U)
        {
            return false;
        }
        const uint16_t payload =
            (uint16_t)((unsigned)self->buffer[1] | ((unsigned)self->buffer[2] << 8U));
        if (payload < 2U || payload > SYSID_FRAME_DATA_MAX + 2U)
        {
            /* 数据里偶然出现的 0xAA 会被当成帧头，读出的长度不合理：它不是帧头 */
            ++self->bad_frames;
            drop_first_byte(self);
            continue;
        }
        const uint16_t total = (uint16_t)(payload + 4U);
        if (self->count < total)
        {
            return false;
        }
        const uint16_t length = (uint16_t)(payload - 2U);
        const uint16_t received = (uint16_t)((unsigned)self->buffer[4U + length]
                                             | ((unsigned)self->buffer[5U + length] << 8U));
        if (sysid_crc16(&self->buffer[4], length) == received)
        {
            self->frame_length = total; /* 帧内容留在 buffer 开头，直到下一次 push */
            return true;
        }
        ++self->bad_frames;
        drop_first_byte(self);
    }
    return false;
}

/* ================================================================ 激励 */

#define SYSID_EXCITE_COMMAND_SIZE 40U /* 激励命令帧的数据部分，格式见下方“激励”部分开头 */
#define SYSID_EXCITE_SPEEDS       3U  /* 扫动的速度档数 */
#define SYSID_EXCITE_STEPS        4U  /* 阶跃的段数 */
#define SYSID_EXCITE_MAX_CYCLES   10U /* 扫动每档最多来回次数 */
#define SYSID_EXCITE_NO_AXIS      0xFFU

/** 信号形状。 */
typedef enum
{
    SYSID_SHAPE_SWEEP = 0,
    SYSID_SHAPE_CHIRP = 1,
    SYSID_SHAPE_STEPS = 2,
} SysIdShape;

/** 激励状态，记录里原样发给电脑。 */
typedef enum
{
    SYSID_EXCITE_IDLE = 0, /**< 上电后还没有激励过 */
    SYSID_EXCITE_RUNNING = 1,
    SYSID_EXCITE_DONE = 2,    /**< 正常走完 */
    SYSID_EXCITE_STOPPED = 3, /**< 电脑发了停止，或车上调用了 sysid_excite_stop() */
    SYSID_EXCITE_REJECTED = 4, /**< 命令不合格，没有开始；原因见 SysIdExciteStatus.detail */
    SYSID_EXCITE_OUT_OF_WINDOW = 5,  /**< 实测位置离开允许窗口 */
    SYSID_EXCITE_TRACKING_ERROR = 6, /**< 扫动时实测位置离轨迹太远 */
    SYSID_EXCITE_FEEDBACK_LOST = 7,  /**< 接入代码报告反馈不在线 */
} SysIdExciteState;

/** 命令被拒绝的原因（SYSID_EXCITE_REJECTED 时有效）。 */
typedef enum
{
    SYSID_REJECT_NONE = 0,
    SYSID_REJECT_FORMAT = 1, /**< 版本、动作、形状等字段不认识，或参数不是有限数 */
    SYSID_REJECT_AXIS = 2, /**< 没有这个轴 */
    SYSID_REJECT_TARGET = 3, /**< 该轴不支持这个目标，或扫动用在了位置以外的目标上 */
    SYSID_REJECT_AMPLITUDE = 4, /**< 幅值、偏移或阶跃值超过上限，或幅值不为正 */
    SYSID_REJECT_SPEED =
        5, /**< 扫动速度、位置扫频的峰值速度超过上限，扫动没有任何速度档，或位置目标上的阶跃 */
    SYSID_REJECT_ACCEL = 6, /**< 扫动加速度不为正或超过上限，或位置扫频的峰值加速度超过上限 */
    SYSID_REJECT_WINDOW = 7,    /**< 轨迹（或开始位置）不在允许窗口内 */
    SYSID_REJECT_FREQUENCY = 8, /**< 扫频频率不满足 0 < f0 ≤ f1 ≤ 1/(4 × 控制周期) */
    SYSID_REJECT_DURATION = 9,  /**< 时长或扫动来回次数不合格 */
} SysIdRejectReason;

/** 激励模块的配置。 */
typedef struct
{
    const SysIdAxisConfig *axes;
    uint8_t axis_count;
    float period_s; /**< 控制周期，s；扫频最高频率不超过它的 1/4 */
} SysIdExciteConfig;

/** 控制任务每周期交给激励模块的一个轴的实测量（每个轴一项）。 */
typedef struct
{
    float position; /**< 实测位置，rad（与 SysIdAxisConfig 同单位） */
    bool is_online; /**< 反馈在线（由车上已有的离线检测给出） */
} SysIdAxisInput;

/** 本周期接入代码要用的激励量。 */
typedef struct
{
    bool active;        /**< false：用平时的控制，其余字段无意义 */
    uint8_t axis;       /**< 被激励的轴 */
    SysIdTarget target; /**< 作用目标 */
    float origin;       /**< 开始时该轴的实测位置（位置目标用） */
    float offset;       /**< 激励量（含义见文件说明的表） */
    float rate;         /**< offset 的变化率，位置目标时就是速度前馈 */
} SysIdExciteOutput;

/** 写进记录的激励状态。 */
typedef struct
{
    uint8_t axis;     /**< 最近一次命令的轴，SYSID_EXCITE_NO_AXIS = 还没有 */
    uint8_t state;    /**< SysIdExciteState */
    uint8_t shape;    /**< SysIdShape */
    uint8_t target;   /**< SysIdTarget */
    uint8_t detail;   /**< SysIdRejectReason */
    uint16_t segment; /**< 扫动的段号、阶跃的段号；扫频为 0 */
    float offset;
    float rate;
} SysIdExciteStatus;

/** 电脑发来、等控制任务取走的开始命令。 */
typedef struct
{
    uint8_t axis;
    uint8_t target;
    uint8_t shape;
    uint8_t cycles;
    uint8_t option; /**< 车的选项，插件不解释 */
    float param[8];
} SysIdExciteCommand;

/** 激励模块的状态。调用方提供存储。 */
typedef struct
{
    const SysIdExciteConfig *config;

    /* 收命令的任务写、控制任务读 */
    SysIdExciteCommand pending;
    atomic_bool pending_start;
    atomic_bool pending_stop;
    atomic_bool pending_rejected_format; /**< 命令格式不对：交给控制任务记成 REJECTED */

    /* 以下只在控制任务里读写 */
    SysIdExciteOutput output;
    SysIdExciteStatus status;
    SysIdExciteCommand command;
    float time_s; /**< 本次激励已进行的时间 */
    float q_ref;  /**< 扫动：轨迹的当前位置（绝对） */
    float v_ref;  /**< 扫动：轨迹的当前速度，有符号 */
    float centre; /**< 扫动中心（绝对） */
    float speeds[SYSID_EXCITE_SPEEDS];
    uint8_t speed_count;
    float slowest;
    uint16_t leg_count;
    float phase_rad; /**< 扫频相位，保持在 [0, 2π) */
    float step_time_s;
} SysIdExcite;

/** @brief 本周期的激励量。 */
static inline const SysIdExciteOutput *sysid_excite_output(const SysIdExcite *self)
{
    return &self->output;
}

/** @brief 写进记录的状态。 */
static inline const SysIdExciteStatus *sysid_excite_status(const SysIdExcite *self)
{
    return &self->status;
}

/**
 * 激励命令帧（类型 SYSID_FRAME_EXCITE）的数据部分，40 字节，小端：
 *
 *   偏移  类型     内容
 *   0     u8       格式版本，当前 1
 *   1     u8       动作：0 = 停止，1 = 开始
 *   2     u8       轴号，从 0 开始（停止时忽略，下同）
 *   3     u8       目标：0 位置、1 速度、2 力矩
 *   4     u8       形状：0 扫动、1 扫频、2 阶跃
 *   5     u8       扫动：每个速度档来回的次数 1–10；其他形状忽略
 *   6     u8       车的选项：插件原样交给车（sysid_option()），含义由车定，如机械臂 1 = 加摩擦前馈；0 = 不用
 *   7     u8       保留，填 0
 *   8     f32 × 8  参数，按形状：
 *                    扫动  幅值、偏移、加速度、速度档 1、速度档 2、速度档 3（0 = 不用）、—、—
 *                    扫频  幅值、f0 (Hz)、f1 (Hz)、时长 (s)、—、—、—、—
 *                    阶跃  值 1、时长 1 (s)、值 2、时长 2、值 3、时长 3、值 4、时长 4（时长 0 = 不用）
 *                  不用的参数填 0。
 */

#define COMMAND_VERSION 1U
#define ACTION_STOP     0U
#define ACTION_START    1U
#define PARAM_COUNT     8U
#define TWO_PI          6.2831853f
#define PI_F            3.14159265f
#define CHIRP_FADE_MAX  1.0f /* s，扫频开头和结尾渐入渐出的最长时间 */
#define CHIRP_FADE_PART 0.1f /* 渐入渐出最多占时长的比例 */
#define NYQUIST_PART    0.25f /* 扫频最高频率 ≤ 控制频率 × 此值，每个周期至少 4 个点 */

/* 参数在 param[] 里的位置 */
enum
{
    SWEEP_AMPLITUDE = 0,
    SWEEP_OFFSET = 1,
    SWEEP_ACCEL = 2,
    SWEEP_SPEED_FIRST = 3,
    CHIRP_AMPLITUDE = 0,
    CHIRP_F0 = 1,
    CHIRP_F1 = 2,
    CHIRP_DURATION = 3,
};

static float get_f32(const uint8_t *p)
{
    const uint32_t bits =
        (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool sysid_excite_init(SysIdExcite *self, const SysIdExciteConfig *config)
{
    memset(self, 0, sizeof(*self));
    atomic_init(&self->pending_start, false);
    atomic_init(&self->pending_stop, false);
    atomic_init(&self->pending_rejected_format, false);
    self->config = config;
    self->status.axis = SYSID_EXCITE_NO_AXIS;
    self->status.state = SYSID_EXCITE_IDLE;

    if (config->axis_count == 0U || config->axes == NULL || !(config->period_s > 0.0f))
    {
        return false;
    }
    for (uint8_t i = 0U; i < config->axis_count; ++i)
    {
        const SysIdAxisConfig *a = &config->axes[i];
        for (uint8_t t = 0U; t < SYSID_TARGET_COUNT; ++t)
        {
            if (!(a->max_offset[t] >= 0.0f))
            {
                return false;
            }
        }
        if ((a->has_window && !(a->window_min < a->window_max)) || !(a->max_speed >= 0.0f)
            || !(a->max_accel >= 0.0f) || !(a->max_tracking_error >= 0.0f))
        {
            return false;
        }
    }
    return true;
}

static void sysid_excite_command(SysIdExcite *self, const uint8_t *data, uint16_t length)
{
    if (length != SYSID_EXCITE_COMMAND_SIZE || data[0] != COMMAND_VERSION
        || (data[1] != ACTION_STOP && data[1] != ACTION_START))
    {
        atomic_store(&self->pending_rejected_format, true);
        return;
    }
    if (data[1] == ACTION_STOP)
    {
        atomic_store(&self->pending_stop, true);
        return;
    }
    if (atomic_load_explicit(&self->pending_start, memory_order_acquire))
    {
        return; /* 上一条还没被取走 */
    }
    self->pending.axis = data[2];
    self->pending.target = data[3];
    self->pending.shape = data[4];
    self->pending.cycles = data[5];
    self->pending.option = data[6];
    for (uint8_t i = 0U; i < PARAM_COUNT; ++i)
    {
        self->pending.param[i] = get_f32(&data[8U + 4U * i]);
    }
    atomic_store_explicit(&self->pending_start, true, memory_order_release);
}

/* ---------------------------------------------------------------- 开始前的检查 */

static bool in_window(const SysIdAxisConfig *a, float position)
{
    return !a->has_window || (position >= a->window_min && position <= a->window_max);
}

static SysIdRejectReason check_sweep(const SysIdAxisConfig *a, const SysIdExciteCommand *cmd,
                                     float origin)
{
    const float limit = a->max_offset[SYSID_TARGET_POSITION];
    const float amplitude = cmd->param[SWEEP_AMPLITUDE];
    const float offset = cmd->param[SWEEP_OFFSET];
    const float accel = cmd->param[SWEEP_ACCEL];
    if (cmd->target != SYSID_TARGET_POSITION)
    {
        return SYSID_REJECT_TARGET;
    }
    if (cmd->cycles < 1U || cmd->cycles > SYSID_EXCITE_MAX_CYCLES)
    {
        return SYSID_REJECT_DURATION;
    }
    /* 有窗口的轴，偏移由下面的窗口检查限制（从限位上起步的关节要离开限位较远，如机械臂关节 3 偏移 1.6 rad）；
     * 没有窗口的轴只能靠幅值上限限制偏移 */
    if (!(amplitude > 0.0f && amplitude <= limit && (a->has_window || fabsf(offset) <= limit)))
    {
        return SYSID_REJECT_AMPLITUDE;
    }
    if (!(accel > 0.0f && accel <= a->max_accel))
    {
        return SYSID_REJECT_ACCEL;
    }
    uint8_t used = 0U;
    for (uint8_t i = 0U; i < SYSID_EXCITE_SPEEDS; ++i)
    {
        const float s = cmd->param[SWEEP_SPEED_FIRST + i];
        if (!(s >= 0.0f && s <= a->max_speed))
        {
            return SYSID_REJECT_SPEED;
        }
        used = (uint8_t)(used + (s > 0.0f ? 1U : 0U));
    }
    if (used == 0U)
    {
        return SYSID_REJECT_SPEED;
    }
    /* 从开始位置走到中心的一段夹在开始位置和中心之间，开始位置是轴实际所在的地方，不用另查 */
    const float centre = origin + offset;
    if (!in_window(a, centre - amplitude) || !in_window(a, centre + amplitude))
    {
        return SYSID_REJECT_WINDOW;
    }
    return SYSID_REJECT_NONE;
}

static SysIdRejectReason check_chirp(const SysIdExcite *self, const SysIdAxisConfig *a,
                                     const SysIdExciteCommand *cmd, float origin)
{
    const float amplitude = cmd->param[CHIRP_AMPLITUDE];
    const float f0 = cmd->param[CHIRP_F0];
    const float f1 = cmd->param[CHIRP_F1];
    if (!(amplitude > 0.0f && amplitude <= a->max_offset[cmd->target]))
    {
        return SYSID_REJECT_AMPLITUDE;
    }
    if (!(f0 > 0.0f && f0 <= f1 && f1 <= NYQUIST_PART / self->config->period_s))
    {
        return SYSID_REJECT_FREQUENCY;
    }
    if (!(cmd->param[CHIRP_DURATION] > 0.0f))
    {
        return SYSID_REJECT_DURATION;
    }
    if (cmd->target != SYSID_TARGET_POSITION)
    {
        return SYSID_REJECT_NONE;
    }
    if (!in_window(a, origin - amplitude) || !in_window(a, origin + amplitude))
    {
        return SYSID_REJECT_WINDOW;
    }
    /* 位置扫频的峰值速度、加速度也不超过该轴的上限（防参数写错让轴猛抖）。对 offset = A·w·sin(φ) 求导取上界，
     * ω1 = 2π·f1，渐入渐出 w = (1 − cos(π·t/渐变时间)) / 2 使 |w'| ≤ ωf = π / (2·渐变时间)、|w''| ≤ 2ωf²，
     * 对数升频 ω' = ω·ln(f1/f0) / 时长：
     *   |rate| ≤ A·(ω1 + ωf)
     *   |d rate / dt| ≤ A·(ω1² + 2·ω1·ωf + 2·ωf² + ω1·ln(f1/f0) / 时长) */
    const float duration = cmd->param[CHIRP_DURATION];
    const float w1 = TWO_PI * f1;
    const float wf = 0.5f * PI_F / fminf(CHIRP_FADE_MAX, duration * CHIRP_FADE_PART);
    if (!(amplitude * (w1 + wf) <= a->max_speed))
    {
        return SYSID_REJECT_SPEED;
    }
    if (!(amplitude * (w1 * w1 + 2.0f * w1 * wf + 2.0f * wf * wf + w1 * logf(f1 / f0) / duration)
          <= a->max_accel))
    {
        return SYSID_REJECT_ACCEL;
    }
    return SYSID_REJECT_NONE;
}

/* 阶跃。位置目标上的阶跃等于让轴瞬间跳到另一个位置（速度无穷大），超过任何速度上限，不接受——防一条命令让
 * 关节猛跳（如机械臂 KP 8 时阶跃 1 rad）；速度、力矩目标上的阶跃受幅值上限限制。 */
static SysIdRejectReason check_steps(const SysIdAxisConfig *a, const SysIdExciteCommand *cmd)
{
    if (cmd->target == SYSID_TARGET_POSITION)
    {
        return SYSID_REJECT_SPEED;
    }
    bool any = false;
    for (uint8_t i = 0U; i < SYSID_EXCITE_STEPS; ++i)
    {
        const float value = cmd->param[2U * i];
        const float duration = cmd->param[2U * i + 1U];
        if (!(duration >= 0.0f))
        {
            return SYSID_REJECT_DURATION;
        }
        if (duration == 0.0f)
        {
            continue;
        }
        any = true;
        if (!(fabsf(value) <= a->max_offset[cmd->target]))
        {
            return SYSID_REJECT_AMPLITUDE;
        }
    }
    return any ? SYSID_REJECT_NONE : SYSID_REJECT_DURATION;
}

static SysIdRejectReason check_command(const SysIdExcite *self, const SysIdExciteCommand *cmd,
                                       const SysIdAxisInput inputs[])
{
    if (cmd->axis >= self->config->axis_count)
    {
        return SYSID_REJECT_AXIS;
    }
    if (cmd->target >= SYSID_TARGET_COUNT || cmd->shape > SYSID_SHAPE_STEPS)
    {
        return SYSID_REJECT_FORMAT;
    }
    for (uint8_t i = 0U; i < PARAM_COUNT; ++i)
    {
        if (!isfinite(cmd->param[i]))
        {
            return SYSID_REJECT_FORMAT;
        }
    }
    const SysIdAxisConfig *a = &self->config->axes[cmd->axis];
    if (!(a->max_offset[cmd->target] > 0.0f))
    {
        return SYSID_REJECT_TARGET;
    }
    const float origin = inputs[cmd->axis].position;
    switch ((SysIdShape)cmd->shape)
    {
        case SYSID_SHAPE_SWEEP:
            return check_sweep(a, cmd, origin);
        case SYSID_SHAPE_CHIRP:
            return check_chirp(self, a, cmd, origin);
        case SYSID_SHAPE_STEPS:
        default:
            return check_steps(a, cmd);
    }
}

/* ---------------------------------------------------------------- 开始与结束 */

static void finish(SysIdExcite *self, SysIdExciteState state)
{
    self->output.active = false;
    self->output.offset = 0.0f;
    self->output.rate = 0.0f;
    self->status.state = (uint8_t)state;
    self->status.offset = 0.0f;
    self->status.rate = 0.0f;
}

static void start(SysIdExcite *self, const SysIdExciteCommand *cmd, const SysIdAxisInput inputs[])
{
    self->status.axis = cmd->axis;
    self->status.shape = cmd->shape;
    self->status.target = cmd->target;
    self->status.segment = 0U;
    self->status.offset = 0.0f;
    self->status.rate = 0.0f;
    const SysIdRejectReason reason = check_command(self, cmd, inputs);
    self->status.detail = (uint8_t)reason;
    if (reason != SYSID_REJECT_NONE)
    {
        self->status.state = SYSID_EXCITE_REJECTED;
        return;
    }

    self->command = *cmd;
    self->time_s = 0.0f;
    self->step_time_s = 0.0f;
    self->phase_rad = 0.0f;
    const float origin = inputs[cmd->axis].position;
    if (cmd->shape == SYSID_SHAPE_SWEEP)
    {
        self->centre = origin + cmd->param[SWEEP_OFFSET];
        self->q_ref = origin;
        self->v_ref = 0.0f;
        self->speed_count = 0U;
        self->slowest = INFINITY;
        for (uint8_t i = 0U; i < SYSID_EXCITE_SPEEDS; ++i)
        {
            const float s = cmd->param[SWEEP_SPEED_FIRST + i];
            if (s > 0.0f)
            {
                self->speeds[self->speed_count++] = s;
                self->slowest = fminf(self->slowest, s);
            }
        }
        /* 第 0 段走到中心；每个速度档 cycles 个来回（每个来回两段）；最后一段回到开始位置 */
        self->leg_count = (uint16_t)(self->speed_count * cmd->cycles * 2U + 2U);
    }
    self->output.active = true;
    self->output.axis = cmd->axis;
    self->output.target = (SysIdTarget)cmd->target;
    self->output.origin = origin;
    self->output.offset = 0.0f;
    self->output.rate = 0.0f;
    self->status.state = SYSID_EXCITE_RUNNING;
}

/* ---------------------------------------------------------------- 三种信号 */

/* 扫动：沿当前段走一步。梯形速度，最高为本段速度，按给定加速度加减速，到段终点时速度为 0。 */
static void advance_sweep(SysIdExcite *self, float dt_s)
{
    const SysIdExciteCommand *cmd = &self->command;
    const float amplitude = cmd->param[SWEEP_AMPLITUDE];
    const float accel = cmd->param[SWEEP_ACCEL];
    const uint16_t leg = self->status.segment;
    const uint16_t per_speed = (uint16_t)(cmd->cycles * 2U);
    float target;
    float speed;
    if (leg == 0U)
    {
        target = self->centre; /* 偏移为 0 时这一段长度为 0，第一步就走完 */
        speed = self->slowest;
    }
    else if (leg + 1U == self->leg_count)
    {
        target = self->output.origin;
        speed = self->slowest;
    }
    else
    {
        const uint16_t k = (uint16_t)(leg - 1U);
        target = (k % 2U == 0U) ? self->centre + amplitude : self->centre - amplitude;
        speed = self->speeds[k / per_speed];
    }

    /* 刹车速度：从速度 v 起每步减 a·dt 直到 0，走过的路程是 v²/(2a) + v·dt/2；令它等于剩余距离解出 v。
     * 连续时间的 √(2a·d) 在离散步进下每步多减一点，越接近终点减得越多（最后约 2a）。 */
    const float distance = target - self->q_ref;
    const float half_step = 0.5f * accel * dt_s;
    const float brake = sqrtf(half_step * half_step + 2.0f * accel * fabsf(distance)) - half_step;
    const float magnitude = fminf(fminf(speed, fabsf(self->v_ref) + accel * dt_s), brake);
    if (magnitude * dt_s >= fabsf(distance))
    {
        self->q_ref = target;
        self->v_ref = 0.0f;
        self->status.segment = (uint16_t)(leg + 1U);
        if (self->status.segment == self->leg_count)
        {
            finish(self, SYSID_EXCITE_DONE);
            return;
        }
    }
    else
    {
        self->v_ref = (distance > 0.0f) ? magnitude : -magnitude;
        self->q_ref += self->v_ref * dt_s;
    }
    self->output.offset = self->q_ref - self->output.origin;
    self->output.rate = self->v_ref;
}

/* 扫频：频率按对数从 f0 升到 f1；相位逐周期累加（只需要每周期频率的近似，实际发出的 offset 会记录下来） */
static void advance_chirp(SysIdExcite *self, float dt_s)
{
    const SysIdExciteCommand *cmd = &self->command;
    const float amplitude = cmd->param[CHIRP_AMPLITUDE];
    const float f0 = cmd->param[CHIRP_F0];
    const float f1 = cmd->param[CHIRP_F1];
    const float duration = cmd->param[CHIRP_DURATION];
    self->time_s += dt_s;
    if (self->time_s >= duration)
    {
        finish(self, SYSID_EXCITE_DONE);
        return;
    }

    const float freq_hz = f0 * expf(logf(f1 / f0) * self->time_s / duration);
    self->phase_rad += TWO_PI * freq_hz * dt_s;
    if (self->phase_rad >= TWO_PI)
    {
        self->phase_rad -= TWO_PI; /* f ≤ 控制频率 / 4，一步不超过 π/2，减一次就够 */
    }

    /* 渐入渐出：w 从 0 平滑升到 1（半个余弦），结尾对称降回 0 */
    const float fade = fminf(CHIRP_FADE_MAX, duration * CHIRP_FADE_PART);
    const float edge = fminf(self->time_s, duration - self->time_s);
    float w = 1.0f;
    float w_rate = 0.0f;
    if (edge < fade)
    {
        w = 0.5f - 0.5f * cosf(PI_F * edge / fade);
        w_rate = 0.5f * PI_F / fade * sinf(PI_F * edge / fade);
        if (self->time_s > duration - self->time_s)
        {
            w_rate = -w_rate; /* 结尾段 edge 随时间减小 */
        }
    }
    const float s = sinf(self->phase_rad);
    const float c = cosf(self->phase_rad);
    self->output.offset = amplitude * w * s;
    self->output.rate = amplitude * (w_rate * s + w * TWO_PI * freq_hz * c);
}

/* 阶跃：时长为 0 的段跳过；最后一段结束即完成 */
static void advance_steps(SysIdExcite *self, float dt_s)
{
    const SysIdExciteCommand *cmd = &self->command;
    self->step_time_s += dt_s;
    uint16_t i = self->status.segment;
    while (i < SYSID_EXCITE_STEPS)
    {
        const float duration = cmd->param[2U * i + 1U];
        if (duration > 0.0f && self->step_time_s < duration)
        {
            break;
        }
        if (duration > 0.0f)
        {
            self->step_time_s -= duration;
        }
        ++i;
    }
    self->status.segment = i;
    if (i == SYSID_EXCITE_STEPS)
    {
        finish(self, SYSID_EXCITE_DONE);
        return;
    }
    self->output.offset = cmd->param[2U * i];
    self->output.rate = 0.0f;
}

/* ---------------------------------------------------------------- 每周期 */

/* 运行中的停止条件，各自对应的故障见 sysid.h 的“安全” */
static void check_running(SysIdExcite *self, const SysIdAxisInput inputs[])
{
    const SysIdAxisInput *in = &inputs[self->output.axis];
    const SysIdAxisConfig *a = &self->config->axes[self->output.axis];
    if (!in->is_online)
    {
        finish(self, SYSID_EXCITE_FEEDBACK_LOST);
    }
    /* 位置目标的整条轨迹在开始前已查过窗口，开始位置可以在窗口外（靠在机械限位上起步） */
    else if (self->output.target != SYSID_TARGET_POSITION && !in_window(a, in->position))
    {
        finish(self, SYSID_EXCITE_OUT_OF_WINDOW);
    }
    else if (self->command.shape == SYSID_SHAPE_SWEEP && a->max_tracking_error > 0.0f
             && fabsf(in->position - self->q_ref) > a->max_tracking_error)
    {
        finish(self, SYSID_EXCITE_TRACKING_ERROR);
    }
}

static void sysid_excite_step(SysIdExcite *self, float dt_s, const SysIdAxisInput inputs[])
{
    if (atomic_exchange(&self->pending_stop, false))
    {
        atomic_store(&self->pending_start, false);
        if (self->output.active)
        {
            finish(self, SYSID_EXCITE_STOPPED);
        }
    }
    if (atomic_exchange(&self->pending_rejected_format, false) && !self->output.active)
    {
        self->status.state = SYSID_EXCITE_REJECTED;
        self->status.detail = SYSID_REJECT_FORMAT;
    }
    if (atomic_load_explicit(&self->pending_start, memory_order_acquire))
    {
        const SysIdExciteCommand cmd = self->pending;
        atomic_store_explicit(&self->pending_start, false, memory_order_release);
        if (!self->output.active)
        {
            start(self, &cmd, inputs);
        }
    }
    if (!self->output.active)
    {
        return;
    }

    switch ((SysIdShape)self->command.shape)
    {
        case SYSID_SHAPE_SWEEP:
            advance_sweep(self, dt_s);
            break;
        case SYSID_SHAPE_CHIRP:
            advance_chirp(self, dt_s);
            break;
        case SYSID_SHAPE_STEPS:
        default:
            advance_steps(self, dt_s);
            break;
    }
    if (!self->output.active)
    {
        return; /* 刚走完：本周期起回到平时的控制 */
    }
    check_running(self, inputs);
    self->status.offset = self->output.offset;
    self->status.rate = self->output.rate;
}

static void sysid_excite_stop(SysIdExcite *self)
{
    atomic_store(&self->pending_start, false);
    if (self->output.active)
    {
        finish(self, SYSID_EXCITE_STOPPED);
    }
}

/* ================================================================ 记录 */

/** 每发出这么多条记录，重发一次通道表。 */
#ifndef SYSID_LOG_SCHEMA_EVERY
#define SYSID_LOG_SCHEMA_EVERY 1000U
#endif

#define SYSID_LOG_NAME_MAX    31U /* 来源名、通道名的最大字节数 */
#define SYSID_LOG_UNIT_MAX    15U /* 单位的最大字节数 */
#define SYSID_LOG_HEADER_SIZE 32U /* 记录帧数据部分的固定头，格式见下方“记录”部分开头 */

/** 记录模块的配置。 */
typedef struct
{
    const char *source; /**< 来源，如 "custom_control arm"；不超过 SYSID_LOG_NAME_MAX 字节 */
    const SysIdChannel *channels; /**< 通道表，须一直有效 */
    uint8_t channel_count;
    uint32_t clock_hz; /**< 记录时间戳的时钟频率，Hz（如 DWT 周期计数时就是内核频率） */
    float period_s; /**< 名义记录周期，s（只供电脑端参考，实际间隔看时间戳） */
} SysIdLogConfig;

/** 一个通道的值：按通道类型写 f32 或 u32。 */
typedef union
{
    float f32;
    uint32_t u32;
} SysIdValue;

/** 记录模块的状态。调用方提供存储（约 SYSID_LOG_RING_SIZE × SYSID_FRAME_DATA_MAX 字节）。 */
typedef struct
{
    const SysIdLogConfig *config;
    uint16_t schema_id; /**< 通道表的 CRC16，写进每条记录，电脑端用它确认通道表没变 */
    uint16_t record_size; /**< 一条记录的数据部分字节数 */
    uint8_t schema_pages; /**< 通道表分几帧发 */

    /* 控制任务（生产者）独占 */
    uint32_t next_sequence;
    uint32_t dropped;

    /* 环形缓冲区：head 由生产者写，tail 由消费者写 */
    uint8_t ring[SYSID_LOG_RING_SIZE][SYSID_FRAME_DATA_MAX];
    _Atomic uint32_t head;
    _Atomic uint32_t tail;

    /* 传输任务（消费者）独占 */
    bool schema_sending;
    bool peeked_schema;
    uint8_t schema_page;
    uint8_t schema_next_channel;
    uint8_t schema_page_channels;
    uint32_t records_since_schema;
    uint8_t schema_buffer[SYSID_FRAME_DATA_MAX];
} SysIdLog;

/**
 * 两种帧的数据部分，全部小端。
 *
 * 通道表（类型 SYSID_FRAME_SCHEMA），通道多时分几帧（“页”）发：
 *
 *   偏移  类型   内容
 *   0     u8     格式版本，当前 1
 *   1     u8     通道总数
 *   2     u16    通道表编号（下面全部描述的 CRC16，记录帧里有同一个数）
 *   4     u32    时间戳时钟频率，Hz
 *   8     f32    名义记录周期，s
 *   12    u8     本页序号，从 0 开始
 *   13    u8     总页数
 *   14    u8     本页第一个通道的序号
 *   15    u8     本页通道数
 *   16    u8     来源名长度 L，后跟 L 字节来源名
 *   …     每个通道：u8 类型（0 = f32，1 = u32）、u8 名称长度 + 名称、u8 单位长度 + 单位
 *
 * 记录（类型 SYSID_FRAME_RECORD）：
 *
 *   偏移  类型   内容
 *   0     u8     格式版本，当前 1
 *   1     u8     通道数
 *   2     u16    通道表编号
 *   4     u32    序号（从 0 递增；跳号 = 丢了记录）
 *   8     u32    时刻，时钟计数（会回绕）
 *   12    u32    因缓冲区满而丢弃的记录总数
 *   16    u8     激励的轴（0xFF = 还没有激励过）
 *   17    u8     激励状态（SysIdExciteState）
 *   18    u8     激励形状（SysIdShape）
 *   19    u8     激励目标（SysIdTarget）
 *   20    u8     拒绝原因（SysIdRejectReason）
 *   21    u8     保留，0
 *   22    u16    激励段号
 *   24    f32    激励量 offset
 *   28    f32    激励量变化率 rate
 *   32    4 × N  各通道的值，按通道表的顺序
 */

#define FORMAT_VERSION     1U
#define SCHEMA_HEADER_SIZE 16U

_Static_assert(SYSID_FRAME_DATA_MAX
                   >= SCHEMA_HEADER_SIZE + 2U * (1U + SYSID_LOG_NAME_MAX) + 2U + SYSID_LOG_UNIT_MAX,
               "SYSID_FRAME_DATA_MAX 太小，一页通道表装不下一个通道");

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    put_u16(p, (uint16_t)value);
    put_u16(&p[2], (uint16_t)(value >> 16));
}

static void put_f32(uint8_t *p, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_u32(p, bits);
}

/* 一个通道在通道表里的字节：类型、名称长度、名称、单位长度、单位。返回字节数。 */
static uint16_t put_channel(uint8_t *p, const SysIdChannel *c)
{
    const uint8_t name_len = (uint8_t)strlen(c->name);
    const uint8_t unit_len = (uint8_t)strlen(c->unit);
    p[0] = (uint8_t)c->type;
    p[1] = name_len;
    memcpy(&p[2], c->name, name_len);
    p[2U + name_len] = unit_len;
    memcpy(&p[3U + name_len], c->unit, unit_len);
    return (uint16_t)(3U + name_len + unit_len);
}

static uint16_t channel_size(const SysIdChannel *c)
{
    return (uint16_t)(3U + strlen(c->name) + strlen(c->unit));
}

/* 打包通道表的一页：从 first 开始放尽量多的通道。返回放进去的通道数，*length 为数据字节数。 */
static uint8_t pack_schema_page(const SysIdLog *self, uint8_t page, uint8_t first, uint8_t *out,
                                uint16_t *length)
{
    const SysIdLogConfig *cfg = self->config;
    const uint8_t source_len = (uint8_t)strlen(cfg->source);
    out[0] = FORMAT_VERSION;
    out[1] = cfg->channel_count;
    put_u16(&out[2], self->schema_id);
    put_u32(&out[4], cfg->clock_hz);
    put_f32(&out[8], cfg->period_s);
    out[12] = page;
    out[13] = self->schema_pages;
    out[14] = first;
    out[16] = source_len;
    memcpy(&out[17], cfg->source, source_len);
    uint16_t n = (uint16_t)(17U + source_len);
    uint8_t count = 0U;
    while (first + count < cfg->channel_count)
    {
        const SysIdChannel *c = &cfg->channels[first + count];
        if (n + channel_size(c) > SYSID_FRAME_DATA_MAX)
        {
            break;
        }
        n = (uint16_t)(n + put_channel(&out[n], c));
        ++count;
    }
    out[15] = count;
    *length = n;
    return count;
}

/* 通道表编号：来源、时钟、周期和全部通道描述的 CRC16。通道表改了，编号就变。 */
static uint16_t schema_crc(const SysIdLogConfig *cfg)
{
    uint8_t buf[3U + SYSID_LOG_NAME_MAX + SYSID_LOG_UNIT_MAX];
    uint16_t crc = sysid_crc16_update(SYSID_CRC16_INIT, (const uint8_t *)cfg->source,
                                      (uint16_t)strlen(cfg->source));
    put_u32(buf, cfg->clock_hz);
    put_f32(&buf[4], cfg->period_s);
    crc = sysid_crc16_update(crc, buf, 8U);
    for (uint8_t i = 0U; i < cfg->channel_count; ++i)
    {
        crc = sysid_crc16_update(crc, buf, put_channel(buf, &cfg->channels[i]));
    }
    return crc;
}

static void start_schema(SysIdLog *self)
{
    self->schema_sending = true;
    self->schema_page = 0U;
    self->schema_next_channel = 0U;
    self->records_since_schema = 0U;
}

static bool sysid_log_init(SysIdLog *self, const SysIdLogConfig *config)
{
    memset(self, 0, sizeof(*self));
    atomic_init(&self->head, 0U);
    atomic_init(&self->tail, 0U);
    self->config = config;
    if (config->channel_count == 0U || config->channels == NULL || config->clock_hz == 0U
        || config->source == NULL || strlen(config->source) > SYSID_LOG_NAME_MAX)
    {
        return false;
    }
    for (uint8_t i = 0U; i < config->channel_count; ++i)
    {
        const SysIdChannel *c = &config->channels[i];
        if (c->name == NULL || c->unit == NULL || strlen(c->name) > SYSID_LOG_NAME_MAX
            || strlen(c->unit) > SYSID_LOG_UNIT_MAX
            || (c->type != SYSID_VALUE_F32 && c->type != SYSID_VALUE_U32))
        {
            return false;
        }
    }
    const uint32_t record_size = SYSID_LOG_HEADER_SIZE + 4U * (uint32_t)config->channel_count;
    if (record_size > SYSID_FRAME_DATA_MAX)
    {
        return false;
    }
    self->record_size = (uint16_t)record_size;
    self->schema_id = schema_crc(config);

    /* 先按同样的方式打包一遍，数出页数 */
    uint8_t first = 0U;
    uint16_t length;
    while (first < config->channel_count)
    {
        first = (uint8_t)(first
                          + pack_schema_page(self, self->schema_pages, first, self->schema_buffer,
                                             &length));
        ++self->schema_pages;
    }
    start_schema(self);
    return true;
}

static void sysid_log_record(SysIdLog *self, uint32_t time_ticks, const SysIdValue values[],
                             const SysIdExciteStatus *excite)
{
    const uint32_t head = atomic_load_explicit(&self->head, memory_order_relaxed);
    const uint32_t next = (head + 1U) % SYSID_LOG_RING_SIZE;
    const uint32_t sequence = self->next_sequence++;
    if (next == atomic_load_explicit(&self->tail, memory_order_acquire))
    {
        ++self->dropped;
        return;
    }

    uint8_t *p = self->ring[head];
    const uint8_t count = self->config->channel_count;
    p[0] = FORMAT_VERSION;
    p[1] = count;
    put_u16(&p[2], self->schema_id);
    put_u32(&p[4], sequence);
    put_u32(&p[8], time_ticks);
    put_u32(&p[12], self->dropped);
    if (excite != NULL)
    {
        p[16] = excite->axis;
        p[17] = excite->state;
        p[18] = excite->shape;
        p[19] = excite->target;
        p[20] = excite->detail;
        p[21] = 0U;
        put_u16(&p[22], excite->segment);
        put_f32(&p[24], excite->offset);
        put_f32(&p[28], excite->rate);
    }
    else
    {
        memset(&p[16], 0, 16U);
        p[16] = SYSID_EXCITE_NO_AXIS;
    }
    for (uint8_t i = 0U; i < count; ++i)
    {
        put_u32(&p[SYSID_LOG_HEADER_SIZE + 4U * i], values[i].u32); /* f32 和 u32 都按位原样写 */
    }
    atomic_store_explicit(&self->head, next, memory_order_release);
}

static bool sysid_log_peek(SysIdLog *self, uint8_t *type, const uint8_t **data, uint16_t *length)
{
    if (self->schema_sending)
    {
        self->schema_page_channels = pack_schema_page(
            self, self->schema_page, self->schema_next_channel, self->schema_buffer, length);
        self->peeked_schema = true;
        *type = SYSID_FRAME_SCHEMA;
        *data = self->schema_buffer;
        return true;
    }
    const uint32_t tail = atomic_load_explicit(&self->tail, memory_order_relaxed);
    if (tail == atomic_load_explicit(&self->head, memory_order_acquire))
    {
        return false;
    }
    self->peeked_schema = false;
    *type = SYSID_FRAME_RECORD;
    *data = self->ring[tail];
    *length = self->record_size;
    return true;
}

static void sysid_log_pop(SysIdLog *self)
{
    if (self->peeked_schema)
    {
        self->peeked_schema = false;
        self->schema_next_channel =
            (uint8_t)(self->schema_next_channel + self->schema_page_channels);
        if (++self->schema_page == self->schema_pages)
        {
            self->schema_sending = false;
        }
        return;
    }
    const uint32_t tail = atomic_load_explicit(&self->tail, memory_order_relaxed);
    atomic_store_explicit(&self->tail, (tail + 1U) % SYSID_LOG_RING_SIZE, memory_order_release);
    if (++self->records_since_schema >= SYSID_LOG_SCHEMA_EVERY)
    {
        start_schema(self);
    }
}

static void sysid_log_request_schema(SysIdLog *self)
{
    if (!self->schema_sending)
    {
        start_schema(self);
    }
}

/* ================================================================ 插件 */

#define MAX_CHANNELS ((SYSID_FRAME_DATA_MAX - SYSID_LOG_HEADER_SIZE) / 4U)

static struct
{
    const SysIdConfig *config;
    bool ready;
    SysIdLogConfig log_config;
    SysIdExciteConfig excite_config;
    SysIdLog log;
    SysIdExcite excite;

    /* 控制任务 */
    SysIdValue values[MAX_CHANNELS];
    uint32_t last_count[SYSID_MAX_AXES]; /* 每个轴上次看到的反馈计数 */
    float silent_s[SYSID_MAX_AXES];      /* 计数多久没变 */

    /* 收：sysid_receive() 写 rx_head，sysid_poll() 写 rx_tail */
    uint8_t rx[SYSID_RX_RING_SIZE];
    _Atomic uint32_t rx_head;
    _Atomic uint32_t rx_tail;

    /* 通信任务：解析收到的字节；两块发送缓冲轮流用，交给 send 的那块在下一次发送成功前不改写 */
    SysIdFrameParser parser;
    uint8_t tx[2][SYSID_FRAME_MAX];
    uint16_t tx_length;
    uint8_t tx_index;
    bool reply_pending, tx_is_reply;
    uint8_t reply_type;
    uint16_t reply_length;
    uint8_t reply_data[SYSID_FRAME_DATA_MAX];
    bool tx_pending; /* tx[tx_index] 里有一帧还没被 send 接受 */
} sysid;

static bool has_axes(void)
{
    return sysid.config->axis_count > 0U;
}

bool sysid_init(const SysIdConfig *config)
{
    memset(&sysid, 0, sizeof(sysid));
    atomic_init(&sysid.rx_head, 0U);
    atomic_init(&sysid.rx_tail, 0U);
    sysid.config = config;
    if (config->send == NULL || config->channel_count > MAX_CHANNELS
        || config->axis_count > SYSID_MAX_AXES || (config->axis_count > 0U && config->axes == NULL))
    {
        return false;
    }
    for (uint8_t i = 0U; i < config->channel_count; ++i)
    {
        if (config->channels[i].source == NULL)
        {
            return false;
        }
    }
    for (uint8_t i = 0U; i < config->axis_count; ++i)
    {
        if (config->axes[i].position == NULL || !(config->axes[i].feedback_timeout_s > 0.0f))
        {
            return false;
        }
    }
    sysid.log_config = (SysIdLogConfig){ config->source, config->channels, config->channel_count,
                                         config->clock_hz, config->period_s };
    if (!sysid_log_init(&sysid.log, &sysid.log_config))
    {
        return false;
    }
    if (has_axes())
    {
        sysid.excite_config =
            (SysIdExciteConfig){ config->axes, config->axis_count, config->period_s };
        if (!sysid_excite_init(&sysid.excite, &sysid.excite_config))
        {
            return false;
        }
    }
    sysid_frame_parser_init(&sysid.parser);
    sysid.ready = true;
    return true;
}

void sysid_receive(const uint8_t *data, uint32_t length)
{
    if (!sysid.ready)
    {
        return;
    }
    uint32_t head = atomic_load_explicit(&sysid.rx_head, memory_order_relaxed);
    const uint32_t tail = atomic_load_explicit(&sysid.rx_tail, memory_order_acquire);
    for (uint32_t i = 0U; i < length; ++i)
    {
        const uint32_t next = (head + 1U) % SYSID_RX_RING_SIZE;
        if (next == tail)
        {
            break; /* 满：其余字节丢掉，解析器会重新找帧头 */
        }
        sysid.rx[head] = data[i];
        head = next;
    }
    atomic_store_explicit(&sysid.rx_head, head, memory_order_release);
}

/* 一帧收齐：只处理系统辨识的两种命令，车上别的协议的帧忽略 */
static void handle_frame(void)
{
    const uint8_t type = sysid_frame_type(&sysid.parser);
    if (type == SYSID_FRAME_EXCITE && has_axes())
    {
        sysid_excite_command(&sysid.excite, sysid_frame_data(&sysid.parser),
                             sysid_frame_length(&sysid.parser));
    }
    else if (type == SYSID_FRAME_SCHEMA_REQUEST)
    {
        sysid_log_request_schema(&sysid.log);
    }
    else if (sysid.config->command != NULL)
    {
        sysid.config->command(type, sysid_frame_data(&sysid.parser),
                              sysid_frame_length(&sysid.parser));
    }
}

bool sysid_reply(uint8_t type, const uint8_t *data, uint16_t length)
{
    if (!sysid.ready || sysid.reply_pending || length > SYSID_FRAME_DATA_MAX)
    {
        return false;
    }
    memcpy(sysid.reply_data, data, length);
    sysid.reply_type = type;
    sysid.reply_length = length;
    sysid.reply_pending = true;
    return true;
}

void sysid_poll(void)
{
    if (!sysid.ready)
    {
        return;
    }
    uint32_t tail = atomic_load_explicit(&sysid.rx_tail, memory_order_relaxed);
    const uint32_t head = atomic_load_explicit(&sysid.rx_head, memory_order_acquire);
    while (tail != head)
    {
        if (sysid_frame_parser_push(&sysid.parser, sysid.rx[tail]))
        {
            handle_frame();
        }
        tail = (tail + 1U) % SYSID_RX_RING_SIZE;
    }
    atomic_store_explicit(&sysid.rx_tail, tail, memory_order_release);

    for (;;)
    {
        if (!sysid.tx_pending)
        {
            uint8_t type;
            const uint8_t *data;
            uint16_t length;
            sysid.tx_is_reply = sysid.reply_pending;
            if (sysid.reply_pending)
            {
                type = sysid.reply_type;
                data = sysid.reply_data;
                length = sysid.reply_length;
                sysid.reply_pending = false;
            }
            else if (!sysid_log_peek(&sysid.log, &type, &data, &length))
            {
                return;
            }
            sysid.tx_length = sysid_frame_encode(type, data, length, sysid.tx[sysid.tx_index]);
            sysid.tx_pending = true;
        }
        if (!sysid.config->send(sysid.tx[sysid.tx_index], sysid.tx_length))
        {
            return; /* 忙：下次再发同一帧 */
        }
        if (!sysid.tx_is_reply)
        {
            sysid_log_pop(&sysid.log);
        }
        sysid.tx_pending = false;
        sysid.tx_index ^= 1U;
    }
}

void sysid_control(float dt_s)
{
    if (!sysid.ready || !has_axes())
    {
        return;
    }
    SysIdAxisInput inputs[SYSID_MAX_AXES];
    for (uint8_t i = 0U; i < sysid.config->axis_count; ++i)
    {
        const SysIdAxisConfig *a = &sysid.config->axes[i];
        inputs[i].position = *a->position;
        inputs[i].is_online = true;
        if (a->feedback_count != NULL)
        {
            const uint32_t count = *a->feedback_count;
            if (count != sysid.last_count[i])
            {
                sysid.last_count[i] = count;
                sysid.silent_s[i] = 0.0f;
            }
            else
            {
                sysid.silent_s[i] += dt_s;
            }
            /* 计数从 0 开始：上电后还没收到过反馈的轴也算不在线 */
            inputs[i].is_online = count != 0U && sysid.silent_s[i] < a->feedback_timeout_s;
        }
    }
    sysid_excite_step(&sysid.excite, dt_s, inputs);
}

bool sysid_active(void)
{
    return sysid.ready && has_axes() && sysid_excite_output(&sysid.excite)->active;
}

bool sysid_position(uint8_t axis, float *position, float *velocity)
{
    if (!sysid_active())
    {
        return false;
    }
    const SysIdExciteOutput *out = sysid_excite_output(&sysid.excite);
    if (out->axis != axis || out->target != SYSID_TARGET_POSITION)
    {
        return false;
    }
    *position = out->origin + out->offset;
    *velocity = out->rate;
    return true;
}

uint8_t sysid_option(void)
{
    return sysid_active() ? sysid.excite.command.option : 0U;
}

float sysid_offset(uint8_t axis, SysIdTarget target)
{
    if (!sysid_active())
    {
        return 0.0f;
    }
    const SysIdExciteOutput *out = sysid_excite_output(&sysid.excite);
    return (out->axis == axis && out->target == target) ? out->offset : 0.0f;
}

void sysid_record(uint32_t time_ticks)
{
    if (!sysid.ready)
    {
        return;
    }
    for (uint8_t i = 0U; i < sysid.config->channel_count; ++i)
    {
        const SysIdChannel *c = &sysid.config->channels[i];
        if (c->type == SYSID_VALUE_F32)
        {
            sysid.values[i].f32 = *(const volatile float *)c->source;
        }
        else
        {
            sysid.values[i].u32 = *(const volatile uint32_t *)c->source;
        }
    }
    sysid_log_record(&sysid.log, time_ticks, sysid.values,
                     has_axes() ? sysid_excite_status(&sysid.excite) : NULL);
}

void sysid_stop(void)
{
    if (sysid.ready && has_axes())
    {
        sysid_excite_stop(&sysid.excite);
    }
}
