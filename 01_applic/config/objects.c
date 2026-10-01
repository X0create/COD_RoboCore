/**
 * @file    objects.c
 * @brief   这台车的全部对象和它们的初始化（目前：四轮全向轮底盘，遥控直接给底盘速度）
 * @note    1. 对象定义（相当于老模板的全局变量），声明在 objects.h；参数都在 params.h
 *          2. objects_init()：设备 → 机构 → 安全门
 *          上电顺序（通用）在 01_applic/system/app_main.c：app_main() 调用 objects_init()，再按 task_table.c 的
 *          task_table[] 创建任务。6 个任务都在 01_applic/tasks/，调用关系总图见 docs/CALL_FLOW.md。
 */
#include "objects.h"

#include "01_applic/system/app_main.h"
#include "04_core/log/log.h"
#include "params.h"

/* ================================================================== */
/* 1. 对象                                                             */
/* ================================================================== */

/* 设备 */
Dr16 dr16;

/* 电机、底盘、IMU 的参数都在 params.h 的配置表里（wheel_config、chassis_config …） */
Motor wheel_motor[CHASSIS_WHEELS];
MotorGroup motors;

/* 机构（安全门是全车唯一的，在 01_applic/system/safety_gate.c） */
Chassis chassis;
Ins ins;

/* ================================================================== */
/* 2. objects_init()：初始化对象（调度器启动前，由 app_main 调用）        */
/* ================================================================== */

bool objects_init(void)
{
    dr16_init(&dr16);

    Motor *drive[CHASSIS_WHEELS];
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        const Motor *conflict;
        if (!motor_init(&wheel_motor[i], &wheel_config[i], &motors, &conflict))
        {
            RM_LOG_E("motor %s init failed%s%s", wheel_config[i].name,
                     conflict != NULL ? ": id conflict with " : "",
                     conflict != NULL ? conflict->cfg->name : "");
            return false;
        }
        drive[i] = &wheel_motor[i];
    }
    if (!chassis_init(&chassis, &chassis_config, drive, NULL))
    {
        RM_LOG_E("chassis init failed: motor without torque command");
        return false;
    }

    ins_init(&ins, &ins_config);
    safety_gate_init(&safety_gate, arm_switch);
    return true;
}
