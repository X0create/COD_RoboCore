#include "01_applic/modules/motor_bench/motor_bench_link.h"
#include <string.h>

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
    ACTION_DISARM
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
}

void motor_bench_link_receive(MotorBenchLink *self, const uint8_t *data, uint16_t length)
{
    /* 撤销准备是锁存状态；即使普通参数邮箱忙，也不能丢掉已校验的撤销请求。 */
    if (length == MOTOR_BENCH_MESSAGE_SIZE && memcmp(data, "MB01", 4u) == 0
        && data[8] == ACTION_DISARM && (data[9] & ~3u) == 0u)
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
    if (self->length != MOTOR_BENCH_MESSAGE_SIZE || memcmp(r, "MB01", 4u) != 0
        || r[8] > ACTION_DISARM || (r[9] & ~3u) != 0u)
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
                                       .gain = r[11] };
        if (!motor_bench_configure(bench, &p, &s, in))
        {
            status = STATUS_VALUES;
        }
    }
    if (disarm)
    {
        bench->settings.allow_motion = false;
    }
    uint8_t *out = self->reply;
    memcpy(out, "MB01", 4u);
    memcpy(out + 4, r + 4, 4u);
    out[8] = status;
    out[9] = (uint8_t)((bench->settings.allow_motion ? 1u : 0u)
                       | (bench->settings.feedforward ? 2u : 0u));
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
