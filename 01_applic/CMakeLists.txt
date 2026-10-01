# 当前构建的兵种（01_applic/ 下的目录名），由预设选择：h723-infantry-debug 为步兵
set(RM_ROBOT "infantry" CACHE STRING "兵种目录名（01_applic/ 下）")

# 与芯片无关的部分（安全门、底盘、ins），电脑测试和固件都链接它
add_library(rm_app_logic STATIC
    system/safety_gate.c
    chassis/chassis.c
    ins/ins.c
)
# 以 "01_applic/chassis/chassis.h" 的形式 include
target_include_directories(rm_app_logic PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_app_logic PUBLIC rm_devices rm_algorithm rm_core_common)
rm_target_warnings(rm_app_logic)

# 以下只用于固件（依赖 FreeRTOS）
if(NOT RM_BOARD)
    return()
endif()

# 各兵种共用的任务和上电顺序，加上当前兵种目录下的全部 .c（<兵种>_robot.c 和本兵种的任务文件）
file(GLOB rm_robot_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${RM_ROBOT}/*.c)
add_library(rm_app STATIC
    system/app_main.c
    system/comm_rx_common.c
    system/detect_task.c
    system/indicator_task.c
    ins/ins_task.c
    ${rm_robot_sources}
)
target_link_libraries(rm_app PUBLIC rm_app_logic rm_core rm_devices rm_algorithm)
rm_target_warnings(rm_app)
