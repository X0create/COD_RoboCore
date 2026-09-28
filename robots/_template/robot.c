/**
 * @file    robot.c
 * @brief   新兵种的样板：阶段 0 只有一个心跳任务，驱动状态灯并每秒通过 RTT 打印一次
 */
#include "robot.h"

#include "platform/status_led.h"

#include "core/log/log.h"
#include "core/os/os.h"

/* FreeRTOS 优先级，数字越大越高；本兵种全部任务的优先级只写在这里 */
enum
{
    PRIORITY_HEARTBEAT = 1,
};

#define HEARTBEAT_STEP_MS     100u
#define HEARTBEAT_STEPS       10u /* 10 × 100 ms = 1 s 一拍 */
#define HEARTBEAT_STACK_WORDS 256u
#define LED_GREEN_LEVEL       0x20u /* WS2812 满亮度很刺眼，1/8 亮度足够看清 */

static RmTask heartbeat_task;
static StackType_t heartbeat_stack[HEARTBEAT_STACK_WORDS];

/* 每拍的第 0、2 步亮：绿色每拍闪两下表示正常（与 UniC 的指示约定一致） */
static bool led_on_at(uint32_t step)
{
    return step == 0u || step == 2u;
}

static void heartbeat_entry(void *arg)
{
    (void)arg;
    uint32_t beat = 0u;
    uint32_t step = 0u;
    RmTaskPeriod last_wake = rm_task_period_start();

    for (;;)
    {
        rm_status_led_set(0u, led_on_at(step) ? LED_GREEN_LEVEL : 0u, 0u);

        if (step == 0u)
        {
            /* 每次打印都会读一次时间，同时保证 DWT 扩展计数至少每 7.8 s 更新一次（time.h 的 @pre） */
            RM_LOG_I("alive %u", (unsigned)beat);
            beat++;
        }
        step = (step + 1u) % HEARTBEAT_STEPS;
        rm_task_delay_until(&last_wake, HEARTBEAT_STEP_MS);
    }
}

bool robot_init(void)
{
    return true;
}

void robot_create_tasks(void)
{
    if (!rm_task_create(&heartbeat_task, "heartbeat", heartbeat_entry, NULL, PRIORITY_HEARTBEAT,
                        heartbeat_stack, HEARTBEAT_STACK_WORDS))
    {
        RM_LOG_E("create heartbeat task failed");
    }
}
