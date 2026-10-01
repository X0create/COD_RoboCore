# SEGGER RTT（第三方代码，不修改）

- 版本：V8.58.0（见 `VERSION`）
- 来源：COD_UniCFramework `05_vender/segger_rtt`（提交 `4220d9c`），该副本已在同一块 DM-MC02 上实测可用
- 许可证：SEGGER 的 BSD 风格许可，见 `LICENSE.md`（允许再分发，需保留版权声明）
- 引入日期：2026-09-28

本项目的配置在上一级目录的 `SEGGER_RTT_Conf.h`，不改这里的文件。升级时整体替换并更新下面的 SHA-256：

```text
caf3d20bc2def30e176f937a56c878a363dec3cd805334ea8173f22e097f1106  SEGGER_RTT.c
b8b6c29abd72c42082502306fe7cdaa0ca90ca2a9b7821dceaa3b023b75f4d95  SEGGER_RTT.h
d2af82ee107fba25157e850109ed9da38243d234f5b70a1039a5c37a0579a054  SEGGER_RTT_ConfDefaults.h
0816e1dd2a03cdfcb186c82195da0994f9afea0139c20789d7c0a8fa3fb70dce  SEGGER_RTT_printf.c
044cecdb620ec4ae626440a0f55aed659e27f629db04e1eb51636d848f5852c6  SEGGER_RTT_ASM_ARMv7M.S
e033779c697246a7a89eb411eca41cabb791dbf0b23619cfbd6d42429191baa8  LICENSE.md
```
