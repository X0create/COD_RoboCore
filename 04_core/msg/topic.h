/**
 * @file    topic.h
 * @brief   话题的通用实现：最新值 + 时间戳 + 唯一发布者（《架构设计》核心机制第 2 节、运行时契约第 2 节）
 * @note    业务代码不直接用这里，而是用 msgs/ 里每种消息自己的一组函数（如 rc_state_publish），
 *          那些函数带类型检查，内部调用这里。
 *
 *          - 最新值：发布覆盖上一次的值，不排队；
 *          - 发布和读取在任务临界区里整体拷贝数据和时间戳，读到的数据和它的时间一定对得上；
 *          - 只能在任务里调用，中断里禁止（中断只通知任务，由任务发布）；
 *          - 一个话题只有一个发布者，初始化时用 topic_claim() 认领。
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

/** topic_read() 的 max_age_ms 传这个值表示不限新旧 */
#define TOPIC_ANY_AGE UINT32_MAX

/** 每种消息的话题类型把它作为第一个成员：typedef struct { Topic base; Xxx data; } XxxTopic; */
typedef struct
{
    const char *owner; /* 认领者名字，NULL 表示还没人认领 */
    uint64_t stamp_us; /* 最近一次发布的时刻 */
    bool published;    /* 是否发布过 */
} Topic;

/**
 * @brief   认领发布权
 * @return  false：已被别的模块认领（配置错误，robot.c 的 robot_init() 应失败）
 * @pre     只在初始化阶段（调度器启动前）调用
 */
RM_NODISCARD bool topic_claim(Topic *topic, const char *owner);

/**
 * @brief   发布：把 src 的 size 字节拷进话题的数据区 slot，并记下当前时刻
 * @pre     已认领；只由认领者调用
 */
void topic_publish(Topic *topic, void *slot, const void *src, size_t size);

/**
 * @brief   读取最新值
 * @param   max_age_ms  数据最多允许多旧；TOPIC_ANY_AGE 表示不限
 * @return  false：从未发布过，或者数据比 max_age_ms 更旧；此时 out 不被修改
 */
RM_NODISCARD bool topic_read(const Topic *topic, const void *slot, void *out, size_t size,
                             uint32_t max_age_ms);

#ifdef __cplusplus
}
#endif
