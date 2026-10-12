# supercap/：设备：超级电容

**状态：规划，尚无代码。**

- 做什么：超级电容控制板的 CAN 协议：读电容电压、设功率上限。
- 计划的文件：`supercap.c/.h`
- 设计要点：底盘功率控制在 `03_algorithm/power/`；电容板的功率上限要跟随裁判系统给的底盘功率上限。
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
