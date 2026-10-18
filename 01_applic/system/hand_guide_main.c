/** 独立台架入口；不链接底盘的 robot_init、control_task 或自动恢复解锁。 */
#include "01_applic/config/hand_guide_robot.h"
#include "01_applic/system/app_main.h"
#include "01_applic/tasks/tasks.h"
#include "04_core/log/log.h"
#include "05_platform/iwdg/iwdg.h"
#include "05_platform/time/time.h"

Motor hand_motor;
MotorGroup hand_motors;
Dr16 hand_rc;
HandGuide hand_guide;
static RmTask control_task, detect_task;
static StackType_t control_stack[1024], rx_stack[1024], detect_stack[256];

static void halt(void)
{
    RM_LOG_E_NO_TIME("hand guide init failed; no scheduler start");
    for (;;)
    {
    }
}

void app_main(void)
{
    (void)iwdg_caused_last_reset(); /* 只清复位原因；看门狗重启仍须下→中重新解锁。 */
    const bool time_ok = rm_time_init();
    rm_log_init();
    if (!time_ok)
    {
        halt();
    }
    dr16_init(&hand_rc);
    const Motor *conflict;
    if (!can_bus_is_fd(hand_motor_config.can_bus)
        || !motor_init(&hand_motor, &hand_motor_config, &hand_motors, &conflict)
        || !hand_guide_init(&hand_guide, &hand_guide_config) || !hand_guide_log_init())
    {
        halt();
    }
    if (!rm_task_create(&comm_rx_task, "hand_rx", hand_guide_rx_task_entry, NULL, 5u, rx_stack,
                        sizeof(rx_stack) / sizeof(rx_stack[0]))
        || !rm_task_create(&control_task, "hand_control", hand_guide_task_entry, NULL, 4u,
                           control_stack, sizeof(control_stack) / sizeof(control_stack[0]))
        || !rm_task_create(&detect_task, "detect", detect_task_entry, NULL, 3u, detect_stack,
                           sizeof(detect_stack) / sizeof(detect_stack[0])))
    {
        halt();
    }
    RM_LOG_I("DM4340 hand guide TEST: DOWN -> MID baseline -> UP assist; USB logs only");
}

void startup_task(void *argument)
{
    (void)argument;
    rm_task_delete_self();
}
