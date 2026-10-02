/**
 * @file    board_link.c
 * @brief   板间通信，见 board_link.h
 */
#include "board_link.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* 本板已初始化的全部跨板消息（发送和接收），只在初始化阶段读写 */
static BoardLink *links;

/* CAN FD 帧允许的数据长度（8 以内任意，之后只有这些档位） */
static const uint8_t fd_sizes[] = { 12u, 16u, 20u, 24u, 32u, 48u, 64u };

static bool is_fd_message(const BoardLinkConfig *cfg)
{
    return cfg->payload_len > BOARD_LINK_MAX_CLASSIC;
}

/* 帧的数据长度：经典帧正好是帧头 + 载荷；FD 帧取能装下的最小档位，多出的字节填 0。两块板按同一规则算 */
static uint8_t frame_len(const BoardLinkConfig *cfg)
{
    const uint8_t need = (uint8_t)(BOARD_LINK_HEADER_LEN + cfg->payload_len);
    if (!is_fd_message(cfg))
    {
        return need;
    }
    size_t i = 0u;
    while (fd_sizes[i] < need)
    {
        i++; /* payload_len ≤ 62 已在初始化时检查，最多到 64 */
    }
    return fd_sizes[i];
}

BoardLinkInitResult board_link_init(BoardLink *link, const BoardLinkConfig *cfg, BoardLinkDir dir,
                                    const MotorGroup *motors)
{
    if (cfg->id > CAN_STD_ID_MAX || cfg->payload_len == 0u
        || cfg->payload_len > BOARD_LINK_MAX_PAYLOAD)
    {
        return BOARD_LINK_INIT_BAD_CONFIG;
    }
    if (is_fd_message(cfg) && !can_bus_is_fd(cfg->can_bus))
    {
        return BOARD_LINK_INIT_NEEDS_FD;
    }
    /* 同一个 ID 被电机用着：发出去电调会当成指令，收进来会和电机反馈混在一起 */
    if (motor_group_find_can_id(motors, cfg->can_bus, cfg->id) != NULL)
    {
        return BOARD_LINK_INIT_MOTOR_CONFLICT;
    }
    for (const BoardLink *other = links; other != NULL; other = other->next)
    {
        if (other->cfg->can_bus == cfg->can_bus && other->cfg->id == cfg->id)
        {
            return BOARD_LINK_INIT_LINK_CONFLICT;
        }
    }

    *link = (BoardLink){ .cfg = cfg, .dir = dir, .next = links };
    links = link;
    if (dir == BOARD_LINK_RX)
    {
        watchdog_register(&link->wd, cfg->name, cfg->timeout_ms);
    }
    return BOARD_LINK_INIT_OK;
}

bool board_link_send(BoardLink *link, const uint8_t *payload, uint32_t age_ms)
{
    const BoardLinkConfig *cfg = link->cfg;
    CanFrame frame = { .id = cfg->id, .len = frame_len(cfg), .is_fd = is_fd_message(cfg) };
    frame.data[0] = link->tx_seq++; /* 发送失败也算一个序号：接收方的丢帧数包含发送队列满丢掉的 */
    frame.data[1] = (age_ms >= BOARD_LINK_AGE_UNKNOWN) ? BOARD_LINK_AGE_UNKNOWN : (uint8_t)age_ms;
    memcpy(&frame.data[BOARD_LINK_HEADER_LEN], payload, cfg->payload_len); /* 其余字节已是 0 */
    return can_send(cfg->can_bus, &frame);
}

/*
 * 是它的 → 检查长度 → 按序号统计丢帧 → 年龄未知的不采用 →
 * 接收时刻按“中断里记下的收帧时刻 − 数据年龄”交给看门狗，旧数据不会被当成刚收到的。
 */
bool board_link_receive(BoardLink *link, CanBusId bus, const CanFrame *frame)
{
    const BoardLinkConfig *cfg = link->cfg;
    if (bus != cfg->can_bus || frame->id != cfg->id)
    {
        return false;
    }
    if (frame->len != frame_len(cfg))
    {
        link->bad_frames++;
        return true; /* 是它的 ID 但长度不对（两块板的编码表不一致）：丢弃、不喂狗 */
    }

    const uint8_t seq = frame->data[0];
    if (link->have_rx_seq)
    {
        link->lost +=
            (uint8_t)(seq - link->last_rx_seq - 1u); /* 按 8 位回绕计算；对方重启时会多算一次 */
    }
    link->last_rx_seq = seq;
    link->have_rx_seq = true;

    const uint8_t age_ms = frame->data[1];
    if (age_ms == BOARD_LINK_AGE_UNKNOWN)
    {
        return true; /* 发送方自己都说数据过期了：不采用，超时后自然离线 */
    }
    const uint64_t age_us = (uint64_t)age_ms * 1000u;
    const uint64_t rx_us = (frame->stamp_us > age_us) ? frame->stamp_us - age_us : 0u;
    watchdog_feed_data(&link->wd, link->payload, &frame->data[BOARD_LINK_HEADER_LEN],
                       cfg->payload_len, rx_us);
    return true;
}

bool board_link_read(const BoardLink *link, uint8_t *out)
{
    return watchdog_read_data(&link->wd, link->payload, out, link->cfg->payload_len);
}

void board_link_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

uint16_t board_link_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void board_link_put_i16(uint8_t *p, int16_t v)
{
    board_link_put_u16(p, (uint16_t)v);
}

int16_t board_link_get_i16(const uint8_t *p)
{
    return (int16_t)board_link_get_u16(p);
}

int16_t board_link_float_to_i16(float value, float lsb)
{
    if (isnan(value))
    {
        return BOARD_LINK_I16_INVALID;
    }
    const float scaled = roundf(value / lsb);
    if (scaled > (float)INT16_MAX)
    {
        return INT16_MAX;
    }
    if (scaled < -(float)INT16_MAX)
    {
        return -INT16_MAX; /* INT16_MIN 留给“无效” */
    }
    return (int16_t)scaled;
}

bool board_link_i16_to_float(int16_t raw, float lsb, float *out)
{
    if (raw == BOARD_LINK_I16_INVALID)
    {
        return false;
    }
    *out = (float)raw * lsb;
    return true;
}
