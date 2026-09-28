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
