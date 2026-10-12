# dji_c_f407/：主控板：大疆 C 板（STM32F407）

**状态：规划，尚无代码。**

- 做什么：CubeMX 工程、链接脚本、重新生成检查清单；芯片实现在 `05_platform/<外设>/<外设>_stm32f4.c`。
- 计划的文件：`dji_c.ioc`、`Core/` `Drivers/`（生成）、`dji_c.ld`、`REGEN_CHECKLIST.md`
- 做法：上层代码不改，只在每个外设目录加一个 `*_stm32f4.c`，并参照 `../dm_mc02_h723/` 建好 CubeMX 工程；见 README 路线图“C 板移植”。
