# 交叉编译工具链：arm-none-eabi-gcc。Windows 原生和 Linux / WSL 通用。
# 查找顺序（先找到的优先）：
#   1. 环境变量 ARM_TOOLCHAIN_BIN（编译器所在的 bin 目录）；
#   2. ~/tools/arm-gnu-toolchain-*/bin（Linux / WSL，docs/DEV_ENVIRONMENT.md 规定的安装位置；
#      CLion 等 IDE 通过 WSL 调用 CMake 时不一定读 ~/.bashrc，靠这一条找到编译器）；
#   3. PATH；
#   4. Windows 默认安装位置：STM32CubeCLT（C:/ST/STM32CubeCLT_*）、Arm GNU Toolchain 安装包。
# 芯片相关的编译选项（-mcpu 等）写在 cmake/board-<板子>.cmake 里，这里只选编译器。
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

file(GLOB rm_home_toolchains "$ENV{HOME}/tools/arm-gnu-toolchain-*/bin")
list(SORT rm_home_toolchains ORDER DESCENDING) # 装了多个版本时用版本号最大的
file(GLOB rm_windows_toolchains
    "C:/ST/STM32CubeCLT_*/GNU-tools-for-STM32/bin"
    "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin"
    "C:/Program Files (x86)/Arm/GNU Toolchain mingw-w64-x86_64-arm-none-eabi/bin")
list(SORT rm_windows_toolchains ORDER DESCENDING)

find_program(RM_ARM_GCC arm-none-eabi-gcc
    HINTS "$ENV{ARM_TOOLCHAIN_BIN}" ${rm_home_toolchains}
    PATHS ${rm_windows_toolchains})
if(NOT RM_ARM_GCC)
    message(FATAL_ERROR "找不到 arm-none-eabi-gcc：安装 Arm GNU Toolchain 或 STM32CubeCLT，"
                        "并把它的 bin 目录加入 PATH 或设为环境变量 ARM_TOOLCHAIN_BIN（见 docs/DEV_ENVIRONMENT.md）")
endif()

get_filename_component(rm_toolchain_bin "${RM_ARM_GCC}" DIRECTORY)
if(CMAKE_HOST_WIN32)
    set(rm_exe ".exe")
endif()
set(CMAKE_C_COMPILER "${rm_toolchain_bin}/arm-none-eabi-gcc${rm_exe}")
set(CMAKE_ASM_COMPILER "${rm_toolchain_bin}/arm-none-eabi-gcc${rm_exe}")
set(CMAKE_CXX_COMPILER "${rm_toolchain_bin}/arm-none-eabi-g++${rm_exe}")

set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

# 没有启动文件和链接脚本时测试程序链接不了，所以 CMake 检查编译器时只编译成静态库
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
