# 当前构建的兵种（robots/ 下的目录名），由预设选择：h723-template-debug 为样板，h723-infantry-debug 为步兵
set(RM_ROBOT "_template" CACHE STRING "兵种目录名（robots/ 下）")

# 与芯片无关的纯逻辑（安全门），电脑测试和固件都链接它
add_library(rm_robot_logic STATIC
    common/safety_gate.c
)
target_include_directories(rm_robot_logic PUBLIC common ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_robot_logic PUBLIC rm_msgs)
rm_target_warnings(rm_robot_logic)

# 以下只用于固件（依赖 FreeRTOS）
if(NOT RM_BOARD)
    return()
endif()

# 兵种目录下的全部 .c（至少有 robot.c；步兵另有 debug.c）
file(GLOB rm_robot_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${RM_ROBOT}/*.c)
add_library(rm_robot STATIC
    common/app_main.c
    common/comm_rx.c
    common/control_task.c
    common/daemon.c
    ${rm_robot_sources}
)
target_include_directories(rm_robot PUBLIC common)
target_link_libraries(rm_robot PUBLIC rm_robot_logic rm_core rm_subsystems rm_devices rm_algorithm)
rm_target_warnings(rm_robot)
