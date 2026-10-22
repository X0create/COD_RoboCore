option(RM_MOTOR_BENCH "MC02 M3508 single-motor control experiment" OFF)

# 与芯片无关的部分（安全门、各机构），电脑测试和固件都链接它
add_library(rm_app_logic STATIC
    system/safety_gate.c
    modules/chassis/chassis.c
    modules/ins/ins.c
)
if(RM_HOST_TESTS OR RM_MOTOR_BENCH)
    target_sources(rm_app_logic PRIVATE modules/motor_bench/motor_bench.c
        modules/motor_bench/motor_bench_link.c)
endif()
# 以 "01_applic/modules/chassis/chassis.h" 的形式 include
target_include_directories(rm_app_logic PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_app_logic PUBLIC rm_devices rm_algorithm rm_core_common)
rm_target_warnings(rm_app_logic)

# 以下只用于固件（依赖 FreeRTOS）
if(NOT RM_BOARD)
    return()
endif()

if(RM_MOTOR_BENCH)
    if(NOT RM_BOARD STREQUAL "dm_mc02")
        message(FATAL_ERROR "Motor bench requires MC02/H723; use h723-m3508")
    endif()
    add_library(rm_bench_telemetry STATIC modules/motor_bench/telemetry/sysid/sysid.c)
    target_include_directories(rm_bench_telemetry PUBLIC modules/motor_bench/telemetry)
    if(RM_MOTOR_BENCH_TIMING)
        target_link_libraries(rm_bench_telemetry PRIVATE rm_platform_common)
    endif()
    rm_target_warnings(rm_bench_telemetry)
    add_library(rm_app STATIC
        system/motor_bench_main.c
        tasks/motor_bench_task.c
        tasks/motor_bench_rx_task.c
        tasks/detect_task.c
    )
    target_link_libraries(rm_app PUBLIC rm_app_logic rm_core rm_devices rm_algorithm rm_bench_telemetry)
    target_include_directories(rm_app PRIVATE ${RM_BOARD_DIR})
    rm_target_warnings(rm_app)
    return()
endif()

add_library(rm_app STATIC
    # 上电顺序
    system/app_main.c
    # 全部任务（每个任务一个文件）
    tasks/ins_task.c
    tasks/control_task.c
    tasks/comm_rx_task.c
    tasks/detect_task.c
    tasks/indicator_task.c
    tasks/log_task.c
    # 这台车的对象、初始化和任务表
    config/robot.c
)
target_link_libraries(rm_app PUBLIC rm_app_logic rm_core rm_devices rm_algorithm)
# 所选主控板的目录：<board.h>（DBUS 串口、电池分压比这类板级接线）
target_include_directories(rm_app PRIVATE ${RM_BOARD_DIR})
rm_target_warnings(rm_app)
