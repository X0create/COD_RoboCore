#include "01_applic/modules/motor_bench/motor_bench_link.h"

#include <string.h>

#include "05_platform/time/time.h"

enum
{
    LINK_IDLE,
    LINK_REQUEST,
    LINK_REPLY
};
enum
{
    ACTION_GET,
    ACTION_SET,
    ACTION_DISARM,
    ACTION_START,
    ACTION_HEARTBEAT
};
enum
{
    STATUS_OK,
    STATUS_FORMAT,
    STATUS_VALUES = 3
};

static float read_float(const uint8_t *p)
{
    uint32_t bits =
        (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static uint32_t read_id(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_float(uint8_t *p, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (uint32_t i = 0u; i < sizeof(bits); ++i)
    {
        p[i] = (uint8_t)(bits >> (8u * i));
    }
}

void motor_bench_link_init(MotorBenchLink *self)
{
    memset(self, 0, sizeof(*self));
    atomic_init(&self->state, LINK_IDLE);
    atomic_init(&self->disarm_requested, false);
    atomic_init(&self->session, 0u);
    atomic_init(&self->heartbeat_us, 0u);
}

uint32_t motor_bench_link_session(const MotorBenchLink *self)
{
    return atomic_load_explicit(&self->session, memory_order_acquire);
}

void motor_bench_link_receive(MotorBenchLink *self, const uint8_t *data, uint16_t length)
{
    /* 撤销准备是锁存状态；即使普通参数邮箱忙，也不能丢掉已校验的撤销请求。 */
    const bool valid = length == MOTOR_BENCH_MESSAGE_SIZE && memcmp(data, "MB02", 4u) == 0
                       && (data[9] & ~7u) == 0u;
    if (valid && data[8] == ACTION_HEARTBEAT)
    {
        /* 心跳不占用参数邮箱，也不生成回执；会话 ID 必须与已确认的 SET 相同。 */
        if (read_id(data + 4) == atomic_load_explicit(&self->session, memory_order_acquire))
        {
            atomic_store_explicit(&self->heartbeat_us, (uint32_t)rm_time_now_us(),
                                  memory_order_release);
        }
        return;
    }
    if (valid && data[8] == ACTION_DISARM)
    {
        atomic_store_explicit(&self->disarm_requested, true, memory_order_release);
    }
    if (atomic_load_explicit(&self->state, memory_order_acquire) != LINK_IDLE)
    {
        return;
    }
    memset(self->request, 0, sizeof(self->request));
    memcpy(self->request, data, length < sizeof(self->request) ? length : sizeof(self->request));
    self->length = length;
    atomic_store_explicit(&self->state, LINK_REQUEST, memory_order_release);
}

void motor_bench_link_control(MotorBenchLink *self, MotorBench *bench, MotorBenchInput *in)
{
    const uint32_t session = atomic_load_explicit(&self->session, memory_order_acquire);
    /* 先读接收时刻再读 now，防止高优先级接收任务抢占后时间戳在 now 的未来。 */
    const uint32_t heartbeat_us = atomic_load_explicit(&self->heartbeat_us, memory_order_acquire);
    const uint32_t now = (uint32_t)rm_time_now_us();
    in->usb_online = session != 0u && now - heartbeat_us < MOTOR_BENCH_USB_TIMEOUT_US;
    in->usb_start = false;
    const bool disarm =
        atomic_exchange_explicit(&self->disarm_requested, false, memory_order_acq_rel);
    if (atomic_load_explicit(&self->state, memory_order_acquire) != LINK_REQUEST)
    {
        if (disarm)
        {
            bench->settings.allow_motion = false;
        }
        in->settings = bench->settings;
        return;
    }
    const uint8_t *r = self->request;
    uint8_t status = STATUS_OK;
    if (self->length != MOTOR_BENCH_MESSAGE_SIZE || memcmp(r, "MB02", 4u) != 0
        || r[8] > ACTION_START || (r[9] & ~7u) != 0u)
    {
        status = STATUS_FORMAT;
    }
    else if (r[8] == ACTION_DISARM)
    {
        bench->settings.allow_motion = false;
    }
    else if (r[8] == ACTION_SET)
    {
        const MotorBenchParameters p = { read_float(r + 12), read_float(r + 16), read_float(r + 20),
                                         read_float(r + 24), read_float(r + 28), read_float(r + 32),
                                         read_float(r + 36) };
        const MotorBenchSettings s = { .allow_motion = (r[9] & 1u) != 0u,
                                       .feedforward = (r[9] & 2u) != 0u,
                                       .direction = (int8_t)r[10],
                                       .gain = r[11],
                                       .usb_control = (r[9] & 4u) != 0u };
        if (read_id(r + 4) == 0u || !motor_bench_configure(bench, &p, &s, in))
        {
            status = STATUS_VALUES;
        }
        else
        {
            /* SET 本身建立新会话租约；后续心跳只能续期，不能重新准备。 */
            atomic_store_explicit(&self->heartbeat_us, now, memory_order_release);
            atomic_store_explicit(&self->session, read_id(r + 4), memory_order_release);
            in->usb_online = true;
        }
    }
    else if (r[8] == ACTION_START)
    {
        if (!bench->settings.usb_control || !bench->settings.allow_motion
            || bench->mode != MOTOR_BENCH_READY || !in->usb_online || read_id(r + 4) != session
            || disarm)
        {
            status = STATUS_VALUES;
        }
        else
        {
            in->usb_start = true;
        }
    }
    if (disarm)
    {
        bench->settings.allow_motion = false;
    }
    uint8_t *out = self->reply;
    memcpy(out, "MB02", 4u);
    memcpy(out + 4, r + 4, 4u);
    out[8] = status;
    out[9] =
        (uint8_t)((bench->settings.allow_motion ? 1u : 0u) | (bench->settings.feedforward ? 2u : 0u)
                  | (bench->settings.usb_control ? 4u : 0u));
    out[10] = (uint8_t)bench->settings.direction;
    out[11] = (uint8_t)bench->settings.gain;
    const MotorBenchParameters p = motor_bench_parameters(bench);
    const float values[] = { p.torque_limit_nm,   p.low_rad_s,         p.high_rad_s,
                             p.accel_rad_s2,      p.speed_limit_rad_s, p.start_temperature_c,
                             p.stop_temperature_c };
    for (uint32_t i = 0u; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        write_float(out + 12u + 4u * i, values[i]);
    }
    in->settings = bench->settings;
    atomic_store_explicit(&self->state, LINK_REPLY, memory_order_release);
}

bool motor_bench_link_reply(MotorBenchLink *self, uint8_t *reply)
{
    if (atomic_load_explicit(&self->state, memory_order_acquire) != LINK_REPLY)
    {
        return false;
    }
    memcpy(reply, self->reply, sizeof(self->reply));
    atomic_store_explicit(&self->state, LINK_IDLE, memory_order_release);
    return true;
}
