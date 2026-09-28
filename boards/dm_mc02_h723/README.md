# dm_mc02_h723：达妙 DM-MC02（STM32H723VGT6）

## 来源与许可

`dm_mc02.ioc` 以 COD-H7-Template（提交 `4622556`，CubeMX 6.12.1，FW_H7 V1.11.2）的 `COD_H7_Template.ioc` 为起点，
只改了工程名（`ProjectName`、`ProjectFileName`）并统一为 LF 换行。原工程许可如下：

```text
MIT License

Copyright (c) 2025 GrassFan_Wang
```

完整条款见 <https://github.com/GrassFanWang/COD-H7-Template/blob/main/LICENSE>。

## 相对原工程要做的修改（逐项在 CubeMX 中完成，完成后在此打勾）

完成的修改同时记入 `docs/CHANGES_FROM_COD_H7_TEMPLATE.md`（新旧对照、原因和验证层级）。

- [x] 工具链：MDK-ARM → CMake（ADR 0019）。CubeMX 中已确认（`d1c2b45`）
- [x] FreeRTOS：原 4 个任务已随迁移删除；CubeMX 只保留一个启动任务 `startup`（静态、512 字、`osPriorityRealtime7`、
  入口 `startup_task` 选 As weak，由框架实现）。不定义队列（ADR 0025 修订）。CubeMX 中已确认（`d1c2b45`）
- [x] CubeMX 6.12.1 → 6.18.1 迁移（FW_H7 V1.13.0）。**迁移时 FreeRTOS 被移除**：6.18.1 不再提供 CMSIS_V1，
  重新启用时选 CMSIS_V2（`defaultTask` 删不掉，改作启动任务，见上）
- [x] 系统时钟：原工程 640 MHz 超出 H723 手册最高 550 MHz，改为 **550 MHz**（ADR 0028）：
  PLL1 与 AHB 分频照搬 UniC 实测（CubeMX 时钟树已确认无报错，`c3994d1`）；
  PLL2 保持 100 MHz，FDCAN 分频保持 5 / 14 / 5（CAN FD 数据段 5 Mbit/s 需要 100 MHz）
- [ ] 核对 HAL 时基为 TIM2、FDCAN1/3 为 1 Mbit/s 经典帧、FDCAN2 为 FD+BRS、SPI2 数据位 8 bit
- [ ] 按 UniC 实测补上 SPI6（WS2812 状态灯）和 TIM12（蜂鸣器）

每次 Generate Code 后按 `REGEN_CHECKLIST.md` 核对。

## 构建

固件由仓库根目录的 CMake 构建（WSL，仓库根目录）：

```bash
cmake --preset h723-template-debug && cmake --build --preset h723-template-debug
```

根目录的 `CMakeLists.txt` 通过 `add_subdirectory` 复用 CubeMX 生成的 `cmake/stm32cubemx/CMakeLists.txt`（源文件清单随重新生成自动更新），
芯片编译选项在 `cmake/board-dm_mc02.cmake`。本目录下 CubeMX 生成的 `CMakeLists.txt`、`CMakePresets.json`、
`cmake/gcc-arm-none-eabi.cmake` 可以单独构建裸板工程，框架构建不使用它们。

生成代码在工作区是 CRLF 换行，提交时由 `.gitattributes` 转成 LF，不影响编译。
