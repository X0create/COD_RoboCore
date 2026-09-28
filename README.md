# COD RoboCore

COD 战队的 RoboMaster 电控通用模板：用普通 C11 写成，分层清楚，可以在电脑上测试，并内置失效安全机制。
一套代码覆盖多个兵种，支持两种主控：达妙 DM-MC02（STM32H723）和大疆 C 板（STM32F407）。

> **当前状态：阶段 0（骨架）进行中。** 仓库里暂时只有基础配置，代码会按实施计划逐步加入。

## 设计原则

- **明了易懂**：结构体 + 函数，不用宏生成代码，不用函数指针表模拟多态，新队员能顺着目录读懂。
- **分层单向依赖**：robots → subsystems → devices → platform，算法层（algorithm）是纯计算，可以在 PC 上测试。
- **安全兜底**：安全门分三级（致命 / 降级 / 警告），每个机构有自己规定的安全动作，而不是简单地“输出 0”。
- **静态内存**：初始化之后不再分配内存。
- **单一时间基准**：所有时间戳、超时判断都读同一个时钟。

架构规范与实施计划由队内另行维护，不在本仓库中。

## 规划中的目录

```text
boards/       每块板一个目录：CubeMX 生成代码、链接脚本、board.c
platform/     外设接口；stm32h7 / stm32f4 / host 各一份实现，链接时选择
core/         话题、看门狗、日志、参数、错误处理、OS 封装
algorithm/    PID、滤波、姿态、运动学、功率模型等纯计算
devices/      电机、IMU、遥控器、裁判系统、视觉、超级电容、板间通信
msgs/         消息类型与话题函数
subsystems/   云台、底盘、发射、轮腿、姿态解算等机构
robots/       每个兵种（每块板）一个目录：参数、组装、安全表
tests/        PC 单元测试（Unity）、板上自测、硬件在环
tools/        辅助脚本
docs/         随代码演进的文档：编码规范、决策记录、实时预算
```

## 开发环境

| 工具 | 版本 |
| --- | --- |
| 系统 | Windows + WSL 2 + Ubuntu 24.04 |
| 交叉编译器 | Arm GNU Toolchain 15.2.Rel1（`arm-none-eabi-gcc` 15.2.1） |
| 构建 | CMake ≥ 3.25、Ninja |
| 代码检查 | clang-format 18、clang-tidy 18、cppcheck 2.13 |
| 测试 | gcc（主机）、Ruby 3.2（CMock） |
| 调试 | SEGGER J-Link + Ozone（Windows 端） |
| 板级配置 | STM32CubeMX |

电脑侧单元测试（WSL，仓库根目录）：

```bash
cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests
```

固件构建与烧录命令会在板级工程完成后补充。

## 仓库约定

- 所有文本文件为 **UTF-8 编码、LF 换行**，由 `.gitattributes` 和 `.editorconfig` 保证。
- 提交说明的格式是 `类型: 做了什么`，类型包括 `feat` 新功能、`fix` 修复、`docs` 文档、`build` 构建、`test` 测试、`chore` 杂项。
- 编译必须 **0 警告**。

## 参考与致谢

本模板从零编写。设计时参考了以下项目：

- [COD-H7-Template](https://github.com/GrassFanWang/COD-H7-Template)（COD，MIT）
- [COD_UniCFramework](https://github.com/fakeSkyy/COD_UniCFramework)（COD，MIT）
- [basic_framework](https://github.com/HNUYueLuRM/basic_framework)（湖南大学跃鹿战队，MIT）
- 其他开源项目的思路：taproot（GPL-3.0，只借鉴思路，未使用其代码）、StandardRobot++、RM2024-PowerModule 等

如果复制了 MIT 许可项目的代码，会在对应文件中保留原作者的版权声明。

## 许可证

待定。
