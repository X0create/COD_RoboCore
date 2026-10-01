# 04_core/

与具体设备无关的基础设施。

| 目录 | 内容 |
| --- | --- |
| `os/` | 任务创建（静态分配）、临界区、`rm_task_delay_until()` |
| `msg/` | （规划）事件队列、单生产者单消费者环形队列 |
| `watchdog/` | 设备在线检测、任务心跳、喂 IWDG |
| `error/` | `RM_ASSERT`、`RM_CHECK`、错误码、HardFault 记录 |
| `log/` | SEGGER RTT 日志 |
| `param/` | Flash 双区参数存储 |
| `util/` | `snapshot`：最新一份数据 + 写入时刻，临界区拷贝（dr16、ins 用它保存数据，ADR 0058）；CRC、环形缓冲、帧编解码等小工具 |

与芯片无关的部分（`util/`、`watchdog/`）编成库 `rm_core_common`，电脑测试也链接它：它们只通过 `os/critical.h` 用临界区、通过 `05_platform/time/time.h` 取时间，测试里由 `tests/host/fakes/` 提供假实现。`os/`、`log/` 只用于固件。

- **可以** include：platform 接口、FreeRTOS。
- **禁止** include：任何具体设备。
