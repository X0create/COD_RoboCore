# mdk/：用 Keil 烧录和调试（ADR 0052）

Keil 在这里**只负责烧录和调试**，固件仍由 CMake（arm-none-eabi-gcc）编译，CMake 是唯一的构建（ADR 0019）。
Keil 里按 F7，实际是调用 WSL 里的 CMake 编译，再把编出的 ELF 交给 Keil 下载、调试。所以 Keil 和命令行、CLion、Ozone
用的是同一份固件。

## 文件

| 文件 | 作用 |
| --- | --- |
| `dm_mc02.uvprojx` / `dm_mc02.uvoptx` | Keil 工程。两个 Target：`bench`（台架验证固件）、`infantry`（步兵），对应预设 `h723-<Target>-debug` |
| `build_with_cmake.bat` | Keil 的用户命令：Before Build 调 CMake 编译；After Build 把 ELF 复制成 Keil 的 `.axf` |
| `keil_placeholder.c` | 占位文件，只为让 Keil 的“编译”能成功，好执行 After Build（见“原理”） |

`DebugConfig/`、`*.uvguix.*`（窗口布局）、`JLinkLog.txt` 是 Keil 自动生成的个人文件，不进 Git。

## 前提

- WSL `Ubuntu-24.04` 里能用 CMake 编译固件（`docs/DEV_ENVIRONMENT.md` 第 5–7 节）；
- 装了 MDK 5.41 和 `Keil.STM32H7xx_DFP`（已在 4.1.3 上验证；版本不同时 Keil 会提示，选“继续”即可）；
- 调试器默认 J-Link（与 Ozone 相同）。用 ST-Link、DAP 的，在 Options for Target → Debug 里换。

## 用法

1. 打开 `dm_mc02.uvprojx`，工具栏下拉框选 Target：`bench` 或 `infantry`。
2. **F7（Build）**：Build Output 里依次出现 `[cmake] building …`、CMake 的编译输出、`now holds the CMake ELF`，
   最后 `0 Error(s)` 就成功了。CMake 编译失败时 Keil 会停下，并且不会留下旧的 `.axf`。
3. **F8（Download）**：烧录。
4. **Ctrl+F5（Start/Stop Debug Session）**：调试。断点、单步、Watch、Peripherals 寄存器都能用；源文件在单步进入时自动打开，
   想提前打断点就用 File → Open 打开对应的 `.c` 文件。

## 注意

- **编译报错在 Build Output 里只是文本**，路径是 WSL 的 `/mnt/d/...`，不能双击跳转；改代码请用 CLion / VS Code，或照行号打开。
- **在 Keil 里新加的源文件不会被编译**。加文件改 CMake（各层的 `CMakeLists.txt`），Keil 工程不用动。
- **RTT 日志 Keil 看不到**：用 J-Link RTT Viewer（可以和 Keil 调试同时连）或 Ozone 的 Terminal。
- **不要同时用 Keil 和 Ozone 连同一个 J-Link。**
- **会给电机发指令的固件，调试器暂停时 CAN 指令停发**，电调怎么反应还没实测（README“注意事项”）。

## 原理

Keil 工程里没有真正的源文件时，它会判定“Target not created”并删掉输出的 `.axf`，F8、调试都没法用。所以工程里放了一个占位文件：

```
F7 → Before Build：build_with_cmake.bat <Target> build   （WSL 里 cmake --build，失败返回 1，Keil 停止）
   → Keil 编译链接 keil_placeholder.c                     （只为让 Keil 认为构建成功）
   → After Build： build_with_cmake.bat <Target> copy    （build/h723-<Target>-debug/COD_RoboCore.elf
                                                            覆盖 build/keil/<Target>/COD_RoboCore.axf）
F8 / 调试 → 用 build/keil/<Target>/COD_RoboCore.axf（.axf 就是 ELF 格式，内容与 CMake 的 ELF 逐字节相同）
```

- Keil 的用户命令要写成 `cmd.exe /c "$Pbuild_with_cmake.bat" …`：`CreateProcess` 不能直接运行 `.bat`；
  用户命令的当前目录也不是工程目录，`$P` 是工程所在目录。
- “Stop on Exit Code” 在工程文件里是 `nStopB1X` / `nStopA1X`，值 N 表示“退出码 ≥ N−1 就停止”（2026-10-01 用 `UV4 -b` 实测）：
  1 = 任何情况都停，**2 = 退出码非 0 才停**（本工程用 2）。
- 占位文件里的变量放在 `RESET` 段：Keil 自动生成的分散加载文件要求有这个段（原本是向量表）。
- 调试信息是 DWARF 4（`cmake/board-dm_mc02.cmake` 的 `-gdwarf-4`）：GCC 15 默认 DWARF 5，Keil 的调试器不一定能读。

## 验证状态

- 2026-10-01：`UV4 -b` 命令行编译两个 Target 成功，`.axf` 与 CMake 的 ELF 逐字节相同；CMake 失败时脚本返回 1、Keil 停止。
- **未验证**：Keil 里 F8 烧录、调试（断点、Watch、源码对应）——需要接板子。
