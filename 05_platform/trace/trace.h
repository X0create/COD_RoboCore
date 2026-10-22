/** @file trace.h
 * @brief H723 单电机诊断专用的 RAM 事件记录；普通固件的宏为空且不求值。
 * @note 格式 MBT1：小端、全部字段 uint32_t；读冻结快照，不在线消费。
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define RM_TRACE_CAPACITY            2048u
#define RM_TRACE_IRQ_WORDS           8u
#define RM_TRACE_TAIL_CYCLES         8u
#define RM_TRACE_MAGIC               0x3154424du
#define RM_TRACE_THRESHOLD_US        1050u
#define RM_TRACE_SCHEMA_THRESHOLD_US 1200u

typedef enum
{
    RM_TRACE_IDLE,
    RM_TRACE_ARMED,
    RM_TRACE_TRIGGERED,
    RM_TRACE_FROZEN,
    RM_TRACE_REJECTED
} RmTraceState;

typedef enum
{
    RM_TRACE_REASON_NONE,
    RM_TRACE_REASON_PERIOD,
    RM_TRACE_REASON_MANUAL,
    RM_TRACE_REASON_SESSION,
    RM_TRACE_REASON_MOTION,
    RM_TRACE_REASON_CONFIG,
    RM_TRACE_REASON_FAULT
} RmTraceReason;

typedef enum
{
    RM_TRACE_ARM = 1,
    RM_TRACE_READY,
    RM_TRACE_TASK_IN,
    RM_TRACE_TASK_OUT,
    RM_TRACE_CONTROL_ENTER,
    RM_TRACE_SAMPLE,
    RM_TRACE_SESSION,
    RM_TRACE_SUBMIT_BEGIN,
    RM_TRACE_SUBMIT_END,
    RM_TRACE_RECORD_BEGIN,
    RM_TRACE_RECORD_END,
    RM_TRACE_RX_BEGIN,
    RM_TRACE_RX_END,
    RM_TRACE_DISPATCH_BEGIN,
    RM_TRACE_DISPATCH_END,
    RM_TRACE_CAN_COUNT,
    RM_TRACE_UART_COUNT,
    RM_TRACE_USB_COUNT,
    RM_TRACE_POLL_BEGIN,
    RM_TRACE_POLL_END,
    RM_TRACE_FRAME_BEGIN,
    RM_TRACE_FRAME_END,
    RM_TRACE_IRQ_IN,
    RM_TRACE_IRQ_OUT,
    RM_TRACE_TRIGGER,
    RM_TRACE_FREEZE,
    RM_TRACE_CYCLE_END,
    /* 追加编号，保持已有 MBT1 捕获可读；BEGIN/END 分别携带输入/结果。 */
    RM_TRACE_LOG_PEEK_BEGIN,
    RM_TRACE_LOG_PEEK_END,
    RM_TRACE_ENCODE_BEGIN,
    RM_TRACE_ENCODE_END,
    RM_TRACE_CRC_BEGIN,
    RM_TRACE_CRC_END
} RmTraceEventType;

typedef struct
{
    uint32_t sequence; /* 最后提交；从 1 开始。有效范围由 header.count 决定。 */
    uint32_t cycles;
    uint32_t event; /* 高 16 位 IPSR，低 16 位 RmTraceEventType。 */
    uint32_t data;
} RmTraceEvent;

/** Ozone 只写 request（1 arm / 2 freeze）与 threshold_us（1050 / 1200）。 */
typedef struct
{
    uint32_t magic, version, capacity, clock_hz;
    uint32_t control_task, rx_task, detect_task;
    uint32_t request, threshold_us, state, reason;
    uint32_t session, capture_id, count, overwritten;
    uint32_t trigger_sequence, trigger_cycle, tail_remaining;
    uint32_t control_cycle, last_start_us, last_period_us;
    uint32_t armed_cycles, frozen_cycles, captured_threshold_us;
    uint32_t enabled_irqs[RM_TRACE_IRQ_WORDS], covered_irqs[RM_TRACE_IRQ_WORDS];
    RmTraceEvent events[RM_TRACE_CAPACITY];
} RmTrace;

extern volatile RmTrace rm_trace;

/** 调度器启动前调用；clock_hz 来自实际内核时钟。 */
void rm_trace_reset(uint32_t clock_hz);
void rm_trace_bind_tasks(uintptr_t control, uintptr_t rx, uintptr_t detect);
void rm_trace_emit(RmTraceEventType event, uint32_t data);
void rm_trace_ready(uintptr_t task);
void rm_trace_control_enter(uint32_t start_us);
/** 控制任务应用设置后调用。只接受非零会话且无运动授权的捕获，不修改运动状态。 */
void rm_trace_control_state(uint32_t session, bool allow_motion);
void rm_trace_control_end(void);
void rm_trace_fault(void);

/* 平台原语。调用者在锁内写槽，不使用 FreeRTOS 的任务版临界区。 */
void rm_trace_platform_init(void);
uint32_t rm_trace_hw_lock(void);
void rm_trace_hw_unlock(uint32_t saved);
uint32_t rm_trace_hw_cycles(void);
uint32_t rm_trace_hw_context(void);
void rm_trace_hw_barrier(void);
void rm_trace_hw_irqs(void);

#if defined(RM_MOTOR_BENCH_TIMING)
#define RM_TRACE_EVENT(event, data)     rm_trace_emit((event), (uint32_t)(data))
#define RM_TRACE_ENTER(start)           rm_trace_control_enter((uint32_t)(start))
#define RM_TRACE_STATE(session, motion) rm_trace_control_state((session), (motion))
#define RM_TRACE_END()                  rm_trace_control_end()
#define RM_TRACE_FAULT()                rm_trace_fault()
#else
#define RM_TRACE_EVENT(event, data)     ((void)0)
#define RM_TRACE_ENTER(start)           ((void)0)
#define RM_TRACE_STATE(session, motion) ((void)0)
#define RM_TRACE_END()                  ((void)0)
#define RM_TRACE_FAULT()                ((void)0)
#endif
