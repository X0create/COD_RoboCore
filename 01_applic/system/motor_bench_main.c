/** 独立单电机入口；默认只记录，运动准备在 motor_bench.settings 中，复位后清除。 */
#include "01_applic/config/motor_bench_robot.h"
#include "01_applic/system/app_main.h"
#include "01_applic/tasks/tasks.h"
#include "04_core/log/log.h"
#include "05_platform/iwdg/iwdg.h"
#include "05_platform/time/time.h"

Motor bench_motor;
MotorGroup bench_motors;
Dr16 bench_rc;
MotorBench motor_bench;
MotorBenchLink bench_link;
static RmTask control_task, detect_task;
static StackType_t control_stack[1024], rx_stack[1024], detect_stack[256];

static void halt(void)
{
    RM_LOG_E_NO_TIME("motor bench init failed; no scheduler start");
    for (;;)
    {
    }
}

void app_main(void)
{
    (void)iwdg_caused_last_reset();
    const bool time_ok = rm_time_init();
    rm_log_init();
    if (!time_ok)
    {
        halt();
    }
    dr16_init(&bench_rc);
    motor_bench_link_init(&bench_link);
    const Motor *conflict;
    if (can_bus_is_fd(bench_motor_config.can_bus)
        || !motor_init(&bench_motor, &bench_motor_config, &bench_motors, &conflict)
        || !motor_bench_init(&motor_bench, &motor_bench_config) || !motor_bench_log_init())
    {
        halt();
    }
    if (!rm_task_create(&comm_rx_task, "bench_rx_task", motor_bench_rx_task_entry, NULL, 5u,
                        rx_stack, sizeof(rx_stack) / sizeof(rx_stack[0]))
        || !rm_task_create(&control_task, "motor_bench_task", motor_bench_task_entry, NULL, 4u,
                           control_stack, sizeof(control_stack) / sizeof(control_stack[0]))
        || !rm_task_create(&detect_task, "detect_task", detect_task_entry, NULL, 3u, detect_stack,
                           sizeof(detect_stack) / sizeof(detect_stack[0])))
    {
        halt();
    }
    RM_LOG_I("M3508 bench TEST: motion defaults OFF; USB settings+records; DOWN/MID/UP one run");
}

void startup_task(void *argument)
{
    (void)argument;
    rm_task_delete_self();
}
