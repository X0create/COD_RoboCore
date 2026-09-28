/**
 * @file    cycle_extend.c
 * @brief   32 位周期计数扩展成 64 位，见 cycle_extend.h
 */
#include "cycle_extend.h"

void cycle_extend_init(CycleExtender *ext, uint32_t raw)
{
    ext->total = raw;
    ext->last_raw = raw;
}

uint64_t cycle_extend_update(CycleExtender *ext, uint32_t raw)
{
    /* 无符号减法按 2^32 取模，自动跨过回绕：从 0xFFFFFFF0 走到 0x10，差值仍是 0x20 */
    ext->total += (uint32_t)(raw - ext->last_raw);
    ext->last_raw = raw;
    return ext->total;
}
