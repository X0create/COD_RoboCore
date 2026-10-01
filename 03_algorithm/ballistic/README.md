# ballistic/：算法：弹道解算

**状态：规划，尚无代码。**

- 做什么：按弹速、距离、俯仰角计算补偿角（纯计算，电脑可测）。
- 计划的文件：`ballistic.c/.h`
- 参考：《架构设计》目录结构
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
