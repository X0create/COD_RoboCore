# leg/：机构：轮腿

**状态：规划，尚无代码。**

- 做什么：平衡与腿长控制（LQR，增益按腿长拟合）、离地检测、跳跃；用 `leg` 替换 `chassis`。
- 计划的文件：`leg.c/.h`
- 设计要点：平衡控制对周期很敏感，要确认 1 kHz 下计算时间够用（看 log_task 打印的 control 执行时间）。
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
