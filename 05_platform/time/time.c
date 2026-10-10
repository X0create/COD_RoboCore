/**
 * @file    time.c
 * @brief   time 的芯片实现共用的辅助代码（与芯片无关，电脑上测试），见 time.h 末尾一节
 */
#include "time.h"

void cycle_extend_init(CycleExtend *ext, uint32_t raw)
{
    ext->total = raw;
    ext->last_raw = raw;
}

uint64_t cycle_extend_update(CycleExtend *ext, uint32_t raw)
{
    /* 无符号减法按 2^32 取模，自动跨过回绕：从 0xFFFFFFF0 走到 0x10，差值仍是 0x20 */
    ext->total += (uint32_t)(raw - ext->last_raw);
    ext->last_raw = raw;
    return ext->total;
}
