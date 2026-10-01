/**
 * @file    snapshot.h
 * @brief   快照：一份带写入时刻的最新数据，供一个任务写、其他任务读（《架构设计》运行时契约第 2 节）
 * @note    业务代码不直接用这里，而是用产生数据的模块自己的读函数（如 dr16_read()、ins_read()），
 *          那些模块把数据和一个 Snapshot 放在自己的结构体里，内部调用这里。
 *          - 写覆盖上一次的值，不排队；
 *          - 写和读都在任务临界区里整份拷贝数据和时刻，读到的数据和它的时刻一定对得上，不会读到写了一半的数据；
 *          - 只能在任务里调用，中断里禁止（中断只通知任务，由任务写）。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** snapshot_read() 的 max_age_ms 传这个值表示不限新旧 */
#define SNAPSHOT_ANY_AGE UINT32_MAX

typedef struct
{
    uint64_t stamp_us; /* 最近一次写入的时刻 */
    bool written;      /* 是否写过 */
} Snapshot;

/** 把 src（size 字节）整份拷进 slot，并记下当前时刻 */
void snapshot_write(Snapshot *snap, void *slot, const void *src, size_t size);

/**
 * @brief   数据够新时把 slot 整份拷到 out
 * @return  false：从未写过，或比 max_age_ms 更旧；out 不被修改
 */
RM_NODISCARD bool snapshot_read(const Snapshot *snap, const void *slot, void *out, size_t size,
                                uint32_t max_age_ms);

#ifdef __cplusplus
}
#endif
