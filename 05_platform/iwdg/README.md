# iwdg/：外设接口：硬件看门狗 IWDG

**状态：规划，尚无代码。**

- 做什么：启动、喂狗；调试暂停时 IWDG 也暂停。
- 计划的文件：`iwdg.h`、`iwdg_stm32h7.c`
- 参考：《架构设计》阶段 1“IWDG”
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
