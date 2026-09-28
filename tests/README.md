# tests/

| 目录 | 内容 |
| --- | --- |
| `host/` | PC 单元测试（Unity，C 语言）。子目录按被测的层命名（如 `host/platform/`）；Unity 源码在 `host/unity/`；`host/fakes/` 是临界区和时钟的假实现（`fake_time_set_us()` 设定“现在”），测话题、看门狗、设备时链接 `rm_host_fakes` |
| `target/` | 板上自测固件（话题并发、看门狗等） |
| `hil/` | 硬件在环：USB-CAN 回放与故障注入脚本 |
| `data/` | 录制的 IMU、CAN、裁判系统数据 |

运行电脑侧测试（WSL，仓库根目录）：

```bash
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
```

新增测试：在 `host/<层>/` 下写 `test_xxx.c`，再在 `host/CMakeLists.txt` 里加一行
`rm_add_host_test(test_xxx <层>/test_xxx.c <被测库>)`。测试函数写成 `static`（编译选项要求非 static 函数必须有声明）。

主机测试通过**不等于**硬件验证通过。涉及 DMA、缓存、时序、中断、电机安全的结论，必须写明验证层级。
