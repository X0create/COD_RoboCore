# board_link/：设备：板间通信

**状态：规划，尚无代码。**

- 做什么：按编码表把指定话题映射到 CAN 帧（话题、帧 ID、周期），另一块板收到后原样发布。
- 计划的文件：`board_link.c/.h`
- 参考：ADR 0024；《架构设计》“跨板消息”
- 加代码时：在本层 `CMakeLists.txt` 里加源文件，并把文件加进 Keil 工程（或运行 `tools/keil_sync.py`）。
