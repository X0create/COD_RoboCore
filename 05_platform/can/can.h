/**
 * @file    can.h
 * @brief   CAN / CAN FD 收发（《架构设计》核心机制第 1 节、运行时契约第 6 节）
 * @note    - 只支持 11 位标准 ID（DJI、达妙都用标准 ID）；接收全部标准数据帧，扩展帧和远程帧拒收。
 *            每条总线上只挂本车的设备，不用硬件滤波；帧交给谁由robot_comm_rx_task.c 显式写出（ADR 0049）；
 *          - 中断里只把帧放进环形缓冲并调用通知回调，任务里用 can_read() 逐帧取出；
 *          - 使用顺序：调度器启动后 can_start() → 任务里 can_read()。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    CAN_BUS_1,
    CAN_BUS_2,
    CAN_BUS_3,
    CAN_BUS_COUNT,
} CanBusId;

#define CAN_STD_ID_MAX   0x7FFu
#define CAN_MAX_DATA_LEN 64u

typedef struct
{
    uint32_t id; /* 11 位标准 ID */
    uint8_t len; /* 数据字节数：经典帧 0–8，FD 帧 0–8、12、16、20、24、32、48、64 */
    bool is_fd; /* true：CAN FD 帧（只能发往全 FD 的总线，ADR 0023） */
    uint8_t data[CAN_MAX_DATA_LEN];
    uint64_t stamp_us; /* 接收时刻（rm_time_now_us），发送时忽略 */
} CanFrame;

/** 收到新帧时的通知，在**中断**里调用；只能做“通知任务”这类事 */
typedef void (*CanRxNotify)(void *ctx);

/**
 * @brief   设置为接收全部标准数据帧，打开接收中断并启动总线
 * @param   notify  收到新帧时在中断里调用，可以为 NULL（只靠任务轮询 can_read()）
 * @return  false：这块板没有这路总线，或 HAL 配置失败
 * @pre     调度器已经启动；每路总线只调用一次
 */
RM_NODISCARD bool can_start(CanBusId bus, CanRxNotify notify, void *ctx);

/**
 * @brief   取出一帧中断收下的帧
 * @return  false：没有新帧（或这路总线没有启动）
 * @pre     只由一个任务调用（comm_rx）
 */
bool can_read(CanBusId bus, CanFrame *out);

/**
 * @brief   放进硬件发送队列，不等待发送完成
 * @return  false：发送队列满、帧不合法（如 FD 帧发往经典总线），这一帧被丢弃
 * @note    可以在多个任务里调用（内部短暂关中断）；不能在中断里调用
 */
RM_NODISCARD bool can_send(CanBusId bus, const CanFrame *frame);

/**
 * @brief   这路总线是否配置成 CAN FD（CubeMX 的 FrameFormat）
 * @note    FD 帧只能发往全 FD 的总线（ADR 0023）；设备驱动据此决定发 FD 帧还是经典帧
 */
bool can_bus_is_fd(CanBusId bus);

/**
 * @brief   这路总线是否处于 bus-off
 * @note    发送错误太多时控制器会自动脱离总线。STM32 FDCAN 进入 bus-off 后停在初始化状态，
 *          不会自己回来，要调用 can_recover()。未启动的总线返回 false
 */
bool can_is_bus_off(CanBusId bus);

/**
 * @brief   从 bus-off 恢复：重新启动控制器，滤波器和接收中断的配置保留
 * @pre     can_start() 成功过；由 comm_rx_task 调用（01_applic/system/comm_rx_common.c 的 comm_rx_recover_bus_off），同一路两次调用至少间隔 100 ms（《架构设计》“发送队列满了怎么办”）
 * @note    恢复期间 can_send() 返回 false（丢帧），和发送队列满的处理相同
 */
void can_recover(CanBusId bus);

/** 接收环形缓冲满、被丢弃的帧数（调试用） */
uint32_t can_rx_dropped(CanBusId bus);

#ifdef __cplusplus
}
#endif
