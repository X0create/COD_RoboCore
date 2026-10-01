# gimbal/：机构：云台

**状态：规划，尚无代码。**

- 做什么：yaw / pitch 两轴串级控制（角度环 → 速度环），上电先编码器回中再切 IMU（ADR 0041）；自瞄目标来自视觉。
- 计划的文件：`gimbal.c/.h`；需要独立任务时加 `gimbal_task.c`
- 参考：ADR 0041；《架构设计》“云台”
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
