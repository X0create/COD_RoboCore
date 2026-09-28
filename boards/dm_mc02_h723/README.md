# dm_mc02_h723：达妙 DM-MC02（STM32H723VGT6）

## 来源与许可

`dm_mc02.ioc` 以 COD-H7-Template（提交 `4622556`，CubeMX 6.12.1，FW_H7 V1.11.2）的 `COD_H7_Template.ioc` 为起点，
只改了工程名（`ProjectName`、`ProjectFileName`）并统一为 LF 换行。原工程许可如下：

```text
MIT License

Copyright (c) 2025 GrassFan_Wang
```

完整条款见 <https://github.com/GrassFanWang/COD-H7-Template/blob/main/LICENSE>。

## 相对原工程要做的修改（逐项在 CubeMX 中完成，完成后在此打勾）

- [ ] 工具链：MDK-ARM → CMake（ADR 0019）
- [ ] FreeRTOS：删掉 4 个 CubeMX 任务，不定义任何任务或队列；任务由框架静态创建（ADR 0025）
- [ ] 系统时钟：原工程 640 MHz 超出 H723 手册最高 550 MHz，改为 **550 MHz**；FDCAN 内核时钟 96 MHz、分频 6 / 11 / 4（沿用 UniC 实测，ADR 0028）
- [ ] 核对 HAL 时基为 TIM2、FDCAN1/3 为 1 Mbit/s 经典帧、FDCAN2 为 FD+BRS、SPI2 数据位 8 bit
- [ ] 按 UniC 实测补上 SPI6（WS2812 状态灯）和 TIM12（蜂鸣器）

每次 Generate Code 后按 `REGEN_CHECKLIST.md` 核对（待编写）。
