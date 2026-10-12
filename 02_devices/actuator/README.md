# actuator/：设备：PWM 执行器

**状态：规划，尚无代码。**

- 做什么：舵机（弹舱盖、工程机构）、气泵、电磁阀。
- 计划的文件：`actuator.c/.h`
- 设计要点：底层 PWM 用 `05_platform/pwm`；舵机角度要限位，气泵、电磁阀在全车停时要回到安全状态。
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
