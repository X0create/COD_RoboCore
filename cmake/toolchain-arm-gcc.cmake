# 交叉编译工具链：arm-none-eabi-gcc（Arm GNU Toolchain 15.2.Rel1）。
# 优先用环境变量 ARM_TOOLCHAIN_BIN 指定的目录，否则从 PATH 里找。
# 芯片相关的编译选项（-mcpu 等）写在 cmake/board-<板子>.cmake 里，这里只选编译器。
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(DEFINED ENV{ARM_TOOLCHAIN_BIN})
    set(RM_TOOLCHAIN_PREFIX "$ENV{ARM_TOOLCHAIN_BIN}/arm-none-eabi-")
else()
    set(RM_TOOLCHAIN_PREFIX "arm-none-eabi-")
endif()

set(CMAKE_C_COMPILER ${RM_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${RM_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${RM_TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY ${RM_TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE ${RM_TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

# 没有启动文件和链接脚本时测试程序链接不了，所以 CMake 检查编译器时只编译成静态库
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
