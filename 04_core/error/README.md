# error/：基础设施：错误与断言

**状态：规划，尚无代码。**

- 做什么：`RM_ASSERT`、`RM_CHECK`、错误码、HardFault 记录（写进 `.noinit`，下次上电打印）。
- 计划的文件：`error.c/.h`
- 参考：ADR 0026；《架构设计》“错误处理”
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
