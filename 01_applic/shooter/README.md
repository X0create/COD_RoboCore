# shooter/：机构：发射

**状态：规划，尚无代码。**

- 做什么：摩擦轮转速环、拨弹盘位置 / 速度环、卡弹检测、按裁判系统热量限制射频。
- 计划的文件：`shooter.c/.h`
- 参考：《架构设计》ShootCmd 一节
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
