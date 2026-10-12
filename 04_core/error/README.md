# error/：基础设施：错误与断言

**状态：规划，尚无代码。**

- 做什么：`RM_ASSERT`、`RM_CHECK`、错误码、HardFault 记录（写进 `.noinit`，下次上电打印）。
- 计划的文件：`error.c/.h`
- 设计要点：只给真正会发生、且有明确处理办法的错误加检查；断言失败时在调试器下停住，正常运行时复位。
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
