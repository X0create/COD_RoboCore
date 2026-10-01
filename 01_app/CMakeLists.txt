# 当前构建的兵种（01_app/ 下的目录名），由预设选择：h723-bench-debug 为台架验证固件，h723-infantry-debug 为步兵
set(RM_ROBOT "bench" CACHE STRING "兵种目录名（01_app/ 下）")

# 与芯片无关的部分（安全门、底盘、ins），电脑测试和固件都链接它
add_library(rm_app_logic STATIC
    common/safety_gate.c
    chassis/chassis.c
    ins/ins.c
)
# 以 "01_app/chassis/chassis.h" 的形式 include
target_include_directories(rm_app_logic PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_app_logic PUBLIC rm_devices rm_algorithm rm_core_common)
rm_target_warnings(rm_app_logic)

# 以下只用于固件（依赖 FreeRTOS）
if(NOT RM_BOARD)
    return()
endif()

# 兵种目录下的全部 .c（robot.c 和每个任务一个文件：control_task.c、comm_rx_task.c、ins_task.c、heartbeat_task.c）
file(GLOB rm_robot_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${RM_ROBOT}/*.c)
add_library(rm_app STATIC
    common/comm_rx.c
    common/daemon_task.c
    ${rm_robot_sources}
)
target_link_libraries(rm_app PUBLIC rm_app_logic rm_core rm_devices rm_algorithm)
rm_target_warnings(rm_app)
