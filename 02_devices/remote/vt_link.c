/**
 * @file    vt_link.c
 * @brief   图传链路，见 vt_link.h
 */
#include "vt_link.h"

#include <string.h>

#include "02_devices/referee/referee_frame.h"
#include "04_core/util/crc.h"
#include "05_platform/time/time.h"

/*
 * 图传链路上混着两种帧：
 *   VT13 遥控器帧：0xA9 0x53 开头，固定 21 字节，末尾 CRC16
 *   裁判系统格式帧：0xA5 开头（referee_frame），其中 0x0304 是操作手电脑的键鼠
 */
#define VT13_SOF0   0xA9u
#define VT13_SOF1   0x53u
#define CH_MIN      364 /* 摇杆原始值范围和中位，与 DR16 相同 */
#define CH_MAX      1684
#define CH_OFFSET   1024
#define CMD_KBM     0x0304u /* 键鼠命令 ID 和数据长度 */
#define CMD_KBM_LEN 12u

/* 缓冲区满时开头一定能判定（VT13 帧放得下；0xA5 帧的最大数据长度按缓冲区算），否则解析循环会原地打转 */
_Static_assert(VT13_FRAME_LEN <= VT_LINK_BUF_LEN, "VT13 帧必须放得进缓冲区");

/* 摇杆值超出范围：整帧丢弃 */
static bool ch_valid(uint16_t raw)
{
    return raw >= CH_MIN && raw <= CH_MAX;
}

bool vt_link_decode_vt13(const uint8_t frame[VT13_FRAME_LEN], VtRcState *out)
{
    const uint8_t *b = frame;
    if (!crc16_verify(b, VT13_FRAME_LEN))
    {
        return false;
    }
    /* 位布局照旧工程 VT13_Info_Update */
    const uint16_t ch[4] = {
        (uint16_t)((b[2] | (b[3] << 8)) & 0x07FF),
        (uint16_t)(((b[3] >> 3) | (b[4] << 5)) & 0x07FF),
        (uint16_t)(((b[4] >> 6) | (b[5] << 2) | (b[6] << 10)) & 0x07FF),
        (uint16_t)(((b[6] >> 1) | (b[7] << 7)) & 0x07FF),
    };
    for (int i = 0; i < 4; i++)
    {
        if (!ch_valid(ch[i]))
        {
            return false;
        }
        out->ch[i] = (int16_t)(ch[i] - CH_OFFSET);
    }
    out->mode = (VtMode)((b[7] >> 4) & 0x03u);
    out->pause = ((b[7] >> 6) & 0x01u) != 0u;
    out->custom_left = ((b[7] >> 7) & 0x01u) != 0u;
    out->custom_right = (b[8] & 0x01u) != 0u;
    out->wheel = (int16_t)((((b[8] >> 1) | (b[9] << 7)) & 0x07FF) - CH_OFFSET);
    out->trigger = ((b[9] >> 4) & 0x01u) != 0u;
    out->mouse_x = (int16_t)(b[10] | (b[11] << 8));
    out->mouse_y = (int16_t)(b[12] | (b[13] << 8));
    out->mouse_z = (int16_t)(b[14] | (b[15] << 8));
    out->mouse_left = (uint8_t)(b[16] & 0x03u);
    out->mouse_right = (uint8_t)((b[16] >> 2) & 0x03u);
    out->mouse_middle = (uint8_t)((b[16] >> 4) & 0x03u);
    out->keys = (uint16_t)(b[17] | (b[18] << 8));
    return true;
}

void vt_link_init(VtLink *self)
{
    *self = (VtLink){ 0 };
    watchdog_register(&self->rc_wd, "vt13", VT_LINK_TIMEOUT_MS);
    watchdog_register(&self->kbm_wd, "vt_kbm", VT_LINK_TIMEOUT_MS);
}

bool vt_link_read_rc(const VtLink *self, VtRcState *out)
{
    return watchdog_read_data(&self->rc_wd, &self->rc, out, sizeof(*out));
}

bool vt_link_read_kbm(const VtLink *self, KbmState *out)
{
    return watchdog_read_data(&self->kbm_wd, &self->kbm, out, sizeof(*out));
}

/* 校验通过的 0xA5 帧：目前只处理键鼠（0x0304），其他命令只计数 */
static void handle_referee_frame(VtLink *self, const RefereeFrame *f)
{
    if (f->cmd_id != CMD_KBM || f->data_len != CMD_KBM_LEN)
    {
        self->ignored_frames++;
        return;
    }
    const uint8_t *d = f->data; /* 位布局照旧工程 remote_control_t */
    const KbmState kbm = {
        .mouse_x = (int16_t)(d[0] | (d[1] << 8)),
        .mouse_y = (int16_t)(d[2] | (d[3] << 8)),
        .mouse_z = (int16_t)(d[4] | (d[5] << 8)),
        .mouse_left = d[6] != 0u,
        .mouse_right = d[7] != 0u,
        .keys = (uint16_t)(d[8] | (d[9] << 8)),
    };
    watchdog_feed_data(&self->kbm_wd, &self->kbm, &kbm, sizeof(kbm), rm_time_now_us());
}

typedef enum
{
    PARSE_NEED_MORE, /* 字节还不够判断，等下一段 */
    PARSE_BAD,       /* 开头不是合法帧：丢一个字节重新找 */
    PARSE_CONSUMED,  /* 处理了一帧，consumed 是它的长度 */
} ParseResult;

/* 看缓冲区开头：是完整的合法帧就处理，返回消耗的字节数 */
static ParseResult parse_head(VtLink *self, size_t *consumed)
{
    const uint8_t *b = self->buf;
    if (b[0] == VT13_SOF0)
    {
        if (self->len < 2u)
        {
            return PARSE_NEED_MORE;
        }
        if (b[1] != VT13_SOF1)
        {
            return PARSE_BAD;
        }
        if (self->len < VT13_FRAME_LEN)
        {
            return PARSE_NEED_MORE;
        }
        VtRcState rc;
        if (!vt_link_decode_vt13(b, &rc))
        {
            return PARSE_BAD;
        }
        watchdog_feed_data(&self->rc_wd, &self->rc, &rc, sizeof(rc), rm_time_now_us());
        *consumed = VT13_FRAME_LEN;
        return PARSE_CONSUMED;
    }

    RefereeFrame f;
    switch (referee_frame_check(b, self->len, VT_LINK_BUF_LEN - REFEREE_FRAME_OVERHEAD, &f))
    {
        case REFEREE_FRAME_NEED_MORE:
            return PARSE_NEED_MORE;
        case REFEREE_FRAME_BAD:
            return PARSE_BAD;
        case REFEREE_FRAME_OK:
            break;
    }
    handle_referee_frame(self, &f);
    *consumed = f.frame_len;
    return PARSE_CONSUMED;
}

/* 收到的字节先攒进 buf，再从 buf 开头反复找帧（和 vision_link_on_bytes 同一个思路，这里有两种帧头） */
void vt_link_on_bytes(VtLink *self, const uint8_t *data, uint32_t len)
{
    uint32_t in = 0u;
    while (in < len || self->len > 0u)
    {
        /* 先把能放下的字节放进缓冲区 */
        while (in < len && self->len < VT_LINK_BUF_LEN)
        {
            self->buf[self->len++] = data[in++];
        }
        if (self->len == 0u)
        {
            break;
        }

        size_t consumed = 0u;
        const ParseResult r = parse_head(self, &consumed);
        if (r == PARSE_NEED_MORE)
        {
            if (in < len)
            {
                continue; /* 还有字节没放进来；缓冲区满时不会走到这里（见上面的 _Static_assert） */
            }
            break;
        }
        if (r == PARSE_BAD)
        {
            consumed = 1u; /* 丢掉开头一个字节，从下一个字节重新找帧头 */
            self->bad_bytes++;
        }
        memmove(self->buf, self->buf + consumed, self->len - consumed);
        self->len -= (uint32_t)consumed;
    }
}
