# 04_core/

与具体设备无关的基础设施。

| 目录 | 内容 |
| --- | --- |
| `os/` | 任务创建（静态分配）、任务通知、`rm_task_delay_until()`；`critical.h` 临界区、`delay.h` 忙等延时 |
| `watchdog/` | 设备在线检测（`watchdog_feed` / `watchdog_is_online`；数据由各模块自己在临界区里存取；在线状态只有这一个来源）。硬件看门狗 IWDG 不在这里：在 `05_platform/iwdg/`，由 detect_task 喂 |
| `error/` | （规划）`RM_ASSERT`、`RM_CHECK`、错误码、HardFault 记录 |
| `log/` | SEGGER RTT 日志 |
| `param/` | （规划）Flash 双区参数存储 |
| `util/` | `crc`（裁判系统 CRC8 / CRC16）；（规划）事件队列、单生产者单消费者环形队列 |

与芯片无关的部分（`util/`、`watchdog/`）编成库 `rm_core_common`，电脑测试也链接它：它们只通过 `os/critical.h` 用临界区、通过 `05_platform/time/time.h` 取时间，测试里由 `tests/host/fakes/` 提供假实现。`os/`、`log/` 只用于固件。

- **可以** include：platform 接口、FreeRTOS。
- **禁止** include：任何具体设备。
