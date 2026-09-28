# core/

与具体设备无关的基础设施。

| 目录 | 内容 |
| --- | --- |
| `os/` | 任务创建（静态分配）、临界区、`rm_task_delay_until()` |
| `msg/` | 话题通用实现、事件队列、单生产者单消费者环形队列 |
| `watchdog/` | 设备在线检测、任务心跳、喂 IWDG |
| `error/` | `RM_ASSERT`、`RM_CHECK`、错误码、HardFault 记录 |
| `log/` | SEGGER RTT 日志 |
| `param/` | Flash 双区参数存储 |
| `util/` | CRC、环形缓冲、帧编解码等小工具（纯计算，库 `rm_core_util`，电脑测试也链接） |

- **可以** include：platform 接口、FreeRTOS。
- **禁止** include：任何具体设备。
