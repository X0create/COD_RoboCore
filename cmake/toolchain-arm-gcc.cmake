# 交叉编译工具链：arm-none-eabi-gcc（Arm GNU Toolchain 15.2.Rel1）。
# 查找顺序：
#   1. 环境变量 ARM_TOOLCHAIN_BIN（终端里由 ~/.bashrc 设置）；
#   2. ~/tools/arm-gnu-toolchain-*/bin（docs/DEV_ENVIRONMENT.md 规定的安装位置）。
#      CLion 等 IDE 通过 WSL 调用 CMake 时不一定读 ~/.bashrc，靠这一条找到编译器；
#   3. PATH。
# 芯片相关的编译选项（-mcpu 等）写在 cmake/board-<板子>.cmake 里，这里只选编译器。
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(DEFINED ENV{ARM_TOOLCHAIN_BIN})
    set(RM_TOOLCHAIN_BIN "$ENV{ARM_TOOLCHAIN_BIN}")
else()
    file(GLOB RM_TOOLCHAIN_CANDIDATES "$ENV{HOME}/tools/arm-gnu-toolchain-*/bin")
    if(RM_TOOLCHAIN_CANDIDATES)
        list(SORT RM_TOOLCHAIN_CANDIDATES)
        list(GET RM_TOOLCHAIN_CANDIDATES -1 RM_TOOLCHAIN_BIN) # 装了多个版本时用版本号最大的
    endif()
endif()

if(RM_TOOLCHAIN_BIN)
    set(RM_TOOLCHAIN_PREFIX "${RM_TOOLCHAIN_BIN}/arm-none-eabi-")
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
