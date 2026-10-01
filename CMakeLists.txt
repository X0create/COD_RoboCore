cmake_minimum_required(VERSION 3.25)
project(COD_RoboCore LANGUAGES C)

# C11 + GNU 扩展：CubeMX 生成的工程也用这个设置，避免两边不一致
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# 生成 compile_commands.json，供 clangd、clang-tidy 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(warnings)

# 主控板由预设指定（如 dm_mc02）；留空时只构建电脑侧的库和测试
set(RM_BOARD "" CACHE STRING "主控板：dm_mc02；留空表示只构建电脑侧")
if(RM_BOARD)
    enable_language(ASM)           # 启动文件 startup_*.s
    include(board-${RM_BOARD})     # 芯片编译选项，必须在创建任何目标之前

    # 在 WSL 里构建、在 Windows 上调试（CLion 用 Windows 版 GDB、Ozone）时，把调试信息里的
    # /mnt/<盘符>/ 改写成 <盘符>:/，调试器才找得到源码、打得上断点。只改调试信息，不影响 __FILE__
    if(CMAKE_SOURCE_DIR MATCHES "^/mnt/([a-z])/")
        string(TOUPPER "${CMAKE_MATCH_1}" rm_drive)
        add_compile_options(-fdebug-prefix-map=/mnt/${CMAKE_MATCH_1}/=${rm_drive}:/)
    endif()
endif()

add_subdirectory(05_platform)
add_subdirectory(04_core)           # 电脑侧只有 rm_core_common，其余只用于固件（见 04_core/CMakeLists.txt）
add_subdirectory(03_algorithm)
add_subdirectory(02_devices)
add_subdirectory(01_applic)           # 电脑侧只有 rm_app_logic（安全门、底盘、ins），其余只用于固件

if(RM_BOARD)
    # 固件目标。CubeMX 生成的 CMakeLists 把 main.c 等源文件加到名为 ${CMAKE_PROJECT_NAME} 的目标上，
    # 所以目标名必须等于项目名；生成代码不套用本仓库的严格警告
    add_executable(${CMAKE_PROJECT_NAME})
    add_subdirectory(${RM_BOARD_DIR}/cmake/stm32cubemx ${CMAKE_BINARY_DIR}/stm32cubemx)
    target_link_libraries(${CMAKE_PROJECT_NAME} rm_app)
endif()

option(RM_HOST_TESTS "编译电脑侧单元测试（只在 host-tests 预设中打开）" OFF)
if(RM_HOST_TESTS)
    enable_testing()
    add_subdirectory(tests/host)
endif()
