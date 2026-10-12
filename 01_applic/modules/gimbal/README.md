# gimbal/：机构：云台

**状态：规划，尚无代码。**

- 做什么：yaw / pitch 两轴串级控制（角度环 → 速度环），上电先用电机编码器回到正中，再切换到用 IMU 角度闭环；自瞄目标来自视觉。
- 计划的文件：`gimbal.c/.h`；需要独立任务时加 `gimbal_task.c`
- 设计要点：模式切换（如编码器 → IMU）要等云台真的回中了再切，不能只看时间；pitch 要有机械限位保护（软件限角）；
  注意 IMU 的 pitch 是“低头为正”（`modules/ins/ins.h`），云台习惯“抬头为正”，接入时取反。
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
