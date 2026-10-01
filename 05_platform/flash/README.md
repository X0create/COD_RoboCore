# flash/：外设接口：片内 Flash

**状态：规划，尚无代码。**

- 做什么：擦写参数扇区，供 `04_core/param/` 使用。
- 计划的文件：`flash.h`、`flash_stm32h7.c`
- 参考：《架构设计》“参数存储”
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
