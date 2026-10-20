/** @file motor_bench_link.h
 * M3508 MB02 参数和启停。通信任务交接请求，控制任务校验、应用和检查 USB 心跳。
 */
#pragma once
#include "01_applic/modules/motor_bench/motor_bench.h"
#include <stdatomic.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MOTOR_BENCH_COMMAND = 0x20,
    MOTOR_BENCH_REPLY = 0x21,
    MOTOR_BENCH_MESSAGE_SIZE = 40,
    MOTOR_BENCH_USB_TIMEOUT_US = 500000
};

typedef struct
{
    _Atomic uint32_t state;
    _Atomic bool disarm_requested;
    _Atomic uint32_t session, heartbeat_us;
    uint16_t length;
    uint8_t request[MOTOR_BENCH_MESSAGE_SIZE], reply[MOTOR_BENCH_MESSAGE_SIZE];
} MotorBenchLink;

/** 调度器启动前初始化。 */
void motor_bench_link_init(MotorBenchLink *self);
/** 通信任务提交已通过帧 CRC 的载荷；上个请求尚未消费时忽略，电脑超时失败。 */
void motor_bench_link_receive(MotorBenchLink *self, const uint8_t *data, uint16_t length);
/** 控制任务中，在 motor_bench_update 之前处理请求并生成实际值回执。 */
void motor_bench_link_control(MotorBenchLink *self, MotorBench *bench, MotorBenchInput *input);
/** 通信任务取回执并交给 sysid_reply；返回 true 后本请求已消费。 */
bool motor_bench_link_reply(MotorBenchLink *self, uint8_t *reply);

#ifdef __cplusplus
}
#endif
