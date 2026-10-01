# gpio/：外设接口：GPIO

**状态：规划，尚无代码。**

- 做什么：按用途命名的引脚（如 `GPIO_LASER`），外部中断。
- 计划的文件：`gpio.h`、`gpio_stm32h7.c`
- 参考：ADR 0033
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
