# 电脑侧单元测试：用本机 gcc 编译，每个测试是一个独立的可执行文件，由 ctest 统一运行

# Unity v2.7.0（MIT，第三方代码）：不修改，也不套用本仓库的严格警告。
# SYSTEM 让它头文件里的警告不算在测试代码头上。
add_library(unity STATIC unity/unity.c)
target_include_directories(unity SYSTEM PUBLIC unity)

# rm_add_host_test(<测试名> <源文件> [被测库...])
function(rm_add_host_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE unity ${ARGN})
    rm_target_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

rm_add_host_test(test_cycle_extend platform/test_cycle_extend.c rm_platform_common)
rm_add_host_test(test_ws2812 platform/test_ws2812.c rm_platform_common)
rm_add_host_test(test_dma_ring platform/test_dma_ring.c rm_platform_common)
rm_add_host_test(test_can_dlc platform/test_can_dlc.c rm_platform_common)
rm_add_host_test(test_can_rx_ring platform/test_can_rx_ring.c rm_platform_common)
rm_add_host_test(test_crc core/test_crc.c rm_core_util)
rm_add_host_test(test_pid algorithm/test_pid.c rm_algorithm)
rm_add_host_test(test_lpf algorithm/test_lpf.c rm_algorithm)
rm_add_host_test(test_ramp algorithm/test_ramp.c rm_algorithm)
