/**
 * @file    dr16.c
 * @brief   DR16 遥控接收机，见 dr16.h
 */
#include "dr16.h"

/* 摇杆原始值的合法范围和中位（DBUS 协议，附录 A.4）：减去中位后是 -660 ~ +660 */
#define CH_MIN    364
#define CH_MAX    1684
#define CH_OFFSET 1024

/* 摇杆值超出范围说明这一帧错位或受了干扰：整帧丢弃，不让错误数据进系统 */
static bool ch_valid(uint16_t raw)
{
    return raw >= CH_MIN && raw <= CH_MAX;
}

/* 拨杆只有 1（上）、3（中）、2（下）三个合法值 */
static bool sw_valid(uint8_t raw)
{
    return raw >= 1u && raw <= 3u;
}

bool dr16_decode(const uint8_t frame[DR16_FRAME_LEN], RcState *out)
{
    const uint8_t *b = frame;
    /* 通道和拨杆的位布局照旧工程 SBUS_TO_RC */
    const uint16_t ch[5] = {
        (uint16_t)((b[0] | (b[1] << 8)) & 0x07FF),
        (uint16_t)(((b[1] >> 3) | (b[2] << 5)) & 0x07FF),
        (uint16_t)(((b[2] >> 6) | (b[3] << 2) | (b[4] << 10)) & 0x07FF),
        (uint16_t)(((b[4] >> 1) | (b[5] << 7)) & 0x07FF),
        (uint16_t)((b[16] | (b[17] << 8)) & 0x07FF),
    };
    const uint8_t sw_left = (uint8_t)((b[5] >> 4) & 0x03u);
    const uint8_t sw_right = (uint8_t)(((b[5] >> 4) & 0x0Cu) >> 2);

    /* 拨轮（通道 4）不检查：有的接收机固件在这两个字节上发 0，检查会导致每帧都被丢弃（推测，待上板核对） */
    for (int i = 0; i < 4; i++)
    {
        if (!ch_valid(ch[i]))
        {
            return false;
        }
    }
    if (!sw_valid(sw_left) || !sw_valid(sw_right))
    {
        return false;
    }

    /* 校验通过才写 out：摇杆减去中位，键鼠字节原样搬过来 */
    for (int i = 0; i < 5; i++)
    {
        out->ch[i] = (int16_t)(ch[i] - CH_OFFSET);
    }
    out->sw[0] = (RcSwitch)sw_left;
    out->sw[1] = (RcSwitch)sw_right;
    out->mouse_x = (int16_t)(b[6] | (b[7] << 8));
    out->mouse_y = (int16_t)(b[8] | (b[9] << 8));
    out->mouse_z = (int16_t)(b[10] | (b[11] << 8));
    out->mouse_left = b[12] != 0u;
    out->mouse_right = b[13] != 0u;
    out->keys = (uint16_t)(b[14] | (b[15] << 8));
    return true;
}

void dr16_init(Dr16 *self)
{
    *self = (Dr16){ 0 };
    watchdog_register(&self->wd, "dr16", DR16_TIMEOUT_MS);
}

bool dr16_read(const Dr16 *self, RcState *out)
{
    return watchdog_read_data(&self->wd, &self->rc, out, sizeof(*out));
}

void dr16_on_bytes(Dr16 *self, const uint8_t *data, uint32_t len, uint64_t now_us)
{
    if (len == 0u)
    {
        return;
    }
    if (!self->have_last || now_us - self->last_rx_us > DR16_FRAME_GAP_US)
    {
        self->len = 0u; /* 空闲了一段时间：这一段从新帧开头开始，丢掉上一帧没收完的部分 */
    }
    self->have_last = true;
    self->last_rx_us = now_us;

    /* 任务被耽误时一次可能读到好几帧；帧是首尾相接的，按 18 字节依次切开 */
    for (uint32_t i = 0u; i < len; i++)
    {
        self->frame[self->len++] = data[i];
        if (self->len == DR16_FRAME_LEN)
        {
            self->len = 0u;
            RcState state;
            if (dr16_decode(self->frame, &state))
            {
                watchdog_feed_data(&self->wd, &self->rc, &state, sizeof(state), now_us);
            }
            else
            {
                self->bad_frames++;
            }
        }
    }
}
