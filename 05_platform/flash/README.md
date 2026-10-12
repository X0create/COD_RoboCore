# flash/：外设接口：片内 Flash

**状态：规划，尚无代码。**

- 做什么：擦写参数扇区，供 `04_core/param/` 使用。
- 计划的文件：`flash.h`、`flash_stm32h7.c`
- 设计要点：H723 的 Flash 按扇区（128 KB）擦除，擦写期间 CPU 从 Flash 取指会被卡住，所以只在车不动（Safe）时写；
  参数扇区要和程序分开，链接脚本里留出来。
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
