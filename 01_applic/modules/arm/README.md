# arm/：机构：机械臂（工程）

**状态：规划，尚无代码。**

- 做什么：多关节位置控制、正逆运动学（计算放 `03_algorithm/kinematics/`）、重力补偿、自定义控制器映射。
- 计划的文件：`arm.c/.h`
- 参考：《架构设计》“工程”
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
