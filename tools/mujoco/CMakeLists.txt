cmake_minimum_required(VERSION 3.25)
project(mujoco_pid_demo LANGUAGES C)

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
