# 与芯片无关的部分（安全门、各机构），电脑测试和固件都链接它
add_library(rm_app_logic STATIC
    system/safety_gate.c
    modules/chassis/chassis.c
    modules/ins/ins.c
)
# 以 "01_applic/modules/chassis/chassis.h" 的形式 include
target_include_directories(rm_app_logic PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_app_logic PUBLIC rm_devices rm_algorithm rm_core_common)
rm_target_warnings(rm_app_logic)

# 以下只用于固件（依赖 FreeRTOS）
if(NOT RM_BOARD)
    return()
endif()

add_library(rm_app STATIC
    # 上电顺序、接收公共部分
    system/app_main.c
    system/comm_rx_common.c
    # 通用任务
    tasks/ins_task.c
    tasks/detect_task.c
    tasks/indicator_task.c
    # 这台车：对象和任务表、控制 / 接收 / 打印三个任务
    robot/robot.c
    robot/robot_control_task.c
    robot/robot_comm_rx_task.c
    robot/robot_log_task.c
)
target_link_libraries(rm_app PUBLIC rm_app_logic rm_core rm_devices rm_algorithm)
rm_target_warnings(rm_app)
