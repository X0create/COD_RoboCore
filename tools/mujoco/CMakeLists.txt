cmake_minimum_required(VERSION 3.25)
project(mujoco_control_demo LANGUAGES C)

# 单独构建 Windows DLL，不改变固件和 Linux 主机测试的目标。
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This demo requires a Windows 64-bit C compiler")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
get_filename_component(RM_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${RM_SOURCE_DIR}/cmake/warnings.cmake")

# 直接编译原有算法文件，桥接层没有第二份 PID 实现。
add_library(pid_bridge SHARED
    pid_bridge.c
    "${RM_SOURCE_DIR}/03_algorithm/control/pid.c"
    "${RM_SOURCE_DIR}/03_algorithm/filter/lpf.c"
)
target_include_directories(pid_bridge PRIVATE "${RM_SOURCE_DIR}")
set_target_properties(pid_bridge PROPERTIES PREFIX "")
rm_target_warnings(pid_bridge)

# 整个底盘控制链仍用仓库原文件，平台同名函数只在这个 DLL 中由仿真实现替代。
add_library(chassis_bridge SHARED
    chassis_bridge.c
    sim_platform.c
    "${RM_SOURCE_DIR}/01_applic/modules/chassis/chassis.c"
    "${RM_SOURCE_DIR}/01_applic/system/safety_gate.c"
    "${RM_SOURCE_DIR}/02_devices/motor/motor.c"
    "${RM_SOURCE_DIR}/02_devices/motor/motor_group.c"
    "${RM_SOURCE_DIR}/02_devices/motor/dji_motor.c"
    "${RM_SOURCE_DIR}/02_devices/motor/dm_motor.c"
    "${RM_SOURCE_DIR}/04_core/watchdog/watchdog.c"
    "${RM_SOURCE_DIR}/03_algorithm/control/pid.c"
    "${RM_SOURCE_DIR}/03_algorithm/control/ramp.c"
    "${RM_SOURCE_DIR}/03_algorithm/filter/lpf.c"
    "${RM_SOURCE_DIR}/03_algorithm/kinematics/half_steer.c"
    "${RM_SOURCE_DIR}/03_algorithm/kinematics/steer.c"
    "${RM_SOURCE_DIR}/03_algorithm/kinematics/omni.c"
    "${RM_SOURCE_DIR}/03_algorithm/kinematics/mecanum.c"
)
target_include_directories(chassis_bridge PRIVATE "${RM_SOURCE_DIR}")
set_target_properties(chassis_bridge PROPERTIES PREFIX "")
rm_target_warnings(chassis_bridge)
