/** @file trace.c
 * @brief 固定容量事件环和捕获状态机；硬件锁 / 时钟由芯片实现提供。
 */
#include "05_platform/trace/trace.h"
#include <string.h>

volatile RmTrace rm_trace;
_Static_assert(sizeof(RmTraceEvent) == 16u, "MBT1 event layout");
_Static_assert(sizeof(RmTrace) == 160u + RM_TRACE_CAPACITY * sizeof(RmTraceEvent), "MBT1 layout");
_Static_assert((RM_TRACE_CAPACITY & (RM_TRACE_CAPACITY - 1u)) == 0u, "power-of-two ring");

static bool recording(void)
{
    return rm_trace.state == RM_TRACE_ARMED || rm_trace.state == RM_TRACE_TRIGGERED;
}

/* 调用方已屏蔽中断。先写字段，最后发布序号，避免调试器读到半条事件。 */
static void append(RmTraceEventType event, uint32_t data)
{
    const uint32_t index = rm_trace.count & (RM_TRACE_CAPACITY - 1u);
    volatile RmTraceEvent *slot = &rm_trace.events[index];
    slot->cycles = rm_trace_hw_cycles();
    slot->event = (rm_trace_hw_context() << 16) | (uint32_t)event;
    slot->data = data;
    rm_trace_hw_barrier();
    slot->sequence = ++rm_trace.count;
    if (rm_trace.count > RM_TRACE_CAPACITY)
    {
        ++rm_trace.overwritten;
    }
}

static void freeze(RmTraceReason reason)
{
    append(RM_TRACE_FREEZE, (uint32_t)reason);
    rm_trace.reason = (uint32_t)reason;
    rm_trace.frozen_cycles = rm_trace_hw_cycles();
    rm_trace_hw_irqs(); /* 合并 arm / freeze 时实际开启的 IRQ，不猜测运行配置。 */
    rm_trace_hw_barrier();
    rm_trace.state = RM_TRACE_FROZEN;
}

void rm_trace_reset(uint32_t clock_hz)
{
    /* 只在调度器启动前初始化；arm 时不清空 32 KiB 缓冲，不制造长关中断段。 */
    memset((void *)&rm_trace, 0, sizeof(rm_trace));
    rm_trace.magic = RM_TRACE_MAGIC;
    rm_trace.version = 1u;
    rm_trace.capacity = RM_TRACE_CAPACITY;
    rm_trace.clock_hz = clock_hz;
    rm_trace.threshold_us = RM_TRACE_THRESHOLD_US;
}

void rm_trace_bind_tasks(uintptr_t control, uintptr_t rx, uintptr_t detect)
{
    rm_trace.control_task = (uint32_t)control;
    rm_trace.rx_task = (uint32_t)rx;
    rm_trace.detect_task = (uint32_t)detect;
}

void rm_trace_emit(RmTraceEventType event, uint32_t data)
{
    if (!recording())
    {
        return;
    }
    const uint32_t saved = rm_trace_hw_lock();
    if (recording())
    {
        append(event, data);
    }
    rm_trace_hw_unlock(saved);
}

void rm_trace_ready(uintptr_t task)
{
    if (rm_trace.control_task != 0u && (uint32_t)task == rm_trace.control_task)
    {
        rm_trace_emit(RM_TRACE_READY, (uint32_t)task);
    }
}

void rm_trace_control_enter(uint32_t start_us)
{
    if (!recording())
    {
        return;
    }
    const uint32_t saved = rm_trace_hw_lock();
    ++rm_trace.control_cycle;
    rm_trace.last_period_us = rm_trace.control_cycle > 1u ? start_us - rm_trace.last_start_us : 0u;
    rm_trace.last_start_us = start_us;
    append(RM_TRACE_CONTROL_ENTER, rm_trace.control_cycle);
    rm_trace_hw_unlock(saved);
}

void rm_trace_control_state(uint32_t session, bool allow_motion)
{
    const uint32_t saved = rm_trace_hw_lock();
    const uint32_t request = rm_trace.request;
    rm_trace.request = 0u;
    if (request == 1u && !recording())
    {
        if (session == 0u || allow_motion
            || (rm_trace.threshold_us != RM_TRACE_THRESHOLD_US
                && rm_trace.threshold_us != RM_TRACE_SCHEMA_THRESHOLD_US))
        {
            rm_trace.reason = allow_motion ? RM_TRACE_REASON_MOTION : RM_TRACE_REASON_CONFIG;
            rm_trace.state = RM_TRACE_REJECTED;
            rm_trace_hw_unlock(saved);
            return;
        }
        rm_trace.session = session;
        ++rm_trace.capture_id;
        rm_trace.count = 0u;
        rm_trace.overwritten = 0u;
        rm_trace.trigger_sequence = 0u;
        rm_trace.trigger_cycle = 0u;
        rm_trace.tail_remaining = 0u;
        rm_trace.control_cycle = 0u;
        rm_trace.last_start_us = 0u;
        rm_trace.last_period_us = 0u;
        rm_trace.frozen_cycles = 0u;
        rm_trace.reason = RM_TRACE_REASON_NONE;
        rm_trace.captured_threshold_us = rm_trace.threshold_us;
        for (uint32_t i = 0u; i < RM_TRACE_IRQ_WORDS; ++i)
        {
            rm_trace.enabled_irqs[i] = 0u;
        }
        rm_trace_hw_irqs();
        rm_trace.armed_cycles = rm_trace_hw_cycles();
        rm_trace.state = RM_TRACE_ARMED;
        append(RM_TRACE_ARM, rm_trace.capture_id);
        append(RM_TRACE_SESSION, session);
    }
    else if (recording())
    {
        if (allow_motion || session != rm_trace.session || request == 2u)
        {
            freeze(allow_motion                  ? RM_TRACE_REASON_MOTION
                   : session != rm_trace.session ? RM_TRACE_REASON_SESSION
                                                 : RM_TRACE_REASON_MANUAL);
        }
        else if (rm_trace.state == RM_TRACE_ARMED && rm_trace.control_cycle > 1u
                 && rm_trace.last_period_us > rm_trace.captured_threshold_us)
        {
            append(RM_TRACE_TRIGGER, rm_trace.last_period_us);
            rm_trace.trigger_sequence = rm_trace.count;
            rm_trace.trigger_cycle = rm_trace.control_cycle;
            rm_trace.tail_remaining = RM_TRACE_TAIL_CYCLES;
            rm_trace.state = RM_TRACE_TRIGGERED;
        }
    }
    rm_trace_hw_unlock(saved);
}

void rm_trace_control_end(void)
{
    if (!recording())
    {
        return;
    }
    const uint32_t saved = rm_trace_hw_lock();
    append(RM_TRACE_CYCLE_END, rm_trace.control_cycle);
    if (rm_trace.state == RM_TRACE_TRIGGERED && rm_trace.control_cycle != rm_trace.trigger_cycle)
    {
        if (--rm_trace.tail_remaining == 0u)
        {
            freeze(RM_TRACE_REASON_PERIOD);
        }
    }
    rm_trace_hw_unlock(saved);
}

void rm_trace_fault(void)
{
    const uint32_t saved = rm_trace_hw_lock();
    if (recording())
    {
        freeze(RM_TRACE_REASON_FAULT);
    }
    rm_trace_hw_unlock(saved);
}
