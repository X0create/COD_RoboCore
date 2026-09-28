# boards/

每块主控板一个目录，例如 `dm_mc02_h723/`、`dji_c_f407/`。**只有这里允许出现 CubeMX 生成的代码。**

每个板目录包含：

- `.ioc` 与生成代码（`Core/`、`Drivers/`、`Middlewares/` 等），不手改；必须改的只写在 `USER CODE` 区内；
- 链接脚本：DMA 段、`.noinit` 段，并用链接期 `ASSERT` 检查它们还在；
- `board.h` / `board.c`：板级资源表、中断优先级表、`board_init()`；
- `REGEN_CHECKLIST.md`：每次在 CubeMX 里 Generate Code 之后逐条核对。

CubeMX 重新生成时会悄悄删掉它认为不属于用户的代码，所以框架入口和必须存在的中断处理函数尽量放在框架侧。
