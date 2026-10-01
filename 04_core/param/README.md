# param/：基础设施：参数存储

**状态：规划，尚无代码。**

- 做什么：Flash 双区参数（标定值），只在 Safe 状态写。
- 计划的文件：`param.c/.h`
- 参考：《架构设计》“参数存储”
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
