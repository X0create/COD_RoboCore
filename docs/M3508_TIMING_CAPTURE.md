# M3508 周期抖动的最小计时测量方案

状态：2026-10-22，首版诊断已取得用户 MCU RAM；取记录 / 编码 / CRC 分段测点已实现，**新增测点尚未上板**。
依据：辨识仓库 `docs/cn/bench_findings.md` #84；实现决策见 ADR 0088。不改任务优先级、运动设置或质量标准。

## 使用本版（硬件操作由用户完成）

1. 软件检查在 WSL、仓库根目录运行 `bash tools/check_timing.sh`。它构建诊断与三个普通固件，执行主机 / 解码测试，
   检查内核钩子、普通 ELF 无诊断符号、IRQ 覆盖和 USER CODE 边界；不连接硬件。
   ELF 为 `build/Linux/h723-m3508-timing/COD_RoboCore.elf`，可在 Windows Ozone 打开。
   诊断预设名 `h723-m3508-timing`；也可单独 `cmake --preset h723-m3508-timing` / `cmake --build --preset h723-m3508-timing`。
2. 用户在 Ozone 选择并烧录这个 ELF，复位后 RTT 应出现 `M3508 FW: timing-mbt1-phases-v1; RAM capture, motion defaults OFF`。
   启动标识区分普通 / 诊断版，不是唯一构建哈希；同时保存该 ELF 及生成的 `build/timing_20261021/capture_manifest.json`。
   manifest 的源码提交只描述电脑检查时的版本，不能回溯证明曾烧录的版本。
3. 用原采集流程 SET 准备无运动记录，确认实际回读 `allow_motion=false`、会话号非零；不发送 START。
   然后在 Ozone 写 `rm_trace.threshold_us=1050`、`rm_trace.request=1`。先 SET、后 arm；首次循环和 arm 前周期不触发。
   只写这两个请求字段，状态 / 环 / 计数均由记录器维护。捕获期间避免高频 Watch、单步、断点。
4. `state` 为 0 空闲、1 armed、2 已触发 / 收尾、3 frozen、4 请求被拒绝。可能第一次刷新就已是 3，
   核对 `capture_id` 增加、`session` 与 SET 相同即可；不要要求先看到 1。无尖峰时可写 `request=2` 手动冻结。
   已触发后保留尾部控制周期数见 `trace.h`；冻结的是事件写入，控制 / 心跳 / USB 仍运行。
5. frozen 后停止电脑采集并确认撤销成功。用户确认无运动授权，再暂停 MCU、保存 `rm_trace` 整块二进制；
   长度 32928 字节（160 字节头 + 32 KiB 事件环），地址以 manifest 或本次 ELF 的 `&rm_trace` 为准，不跨版本硬编码。
   停止采集后的会话 / 授权变化不改已冻结快照。导出前不要复位 / 重新下载，复位会清掉 RAM 捕获。
6. 可用 Ozone 内存导出；也已生成 `build/timing_20261021/export_trace.jlink`，内容只有 halt、SaveBin、exit，**检查脚本不会执行它**。
   若用户选 J-Link Commander，先断开 Ozone 且保持目标暂停，从仓库根目录手动运行：
   `JLink.exe -device STM32H723VG -if SWD -speed 4000 -AutoConnect 1 -CommanderScript build/timing_20261021/export_trace.jlink`。
   它不复位、不下载、不恢复运行；保存为 `build/timing_20261021/timing_capture.bin`。实际连接 / 导出仍待用户验证。
   SaveBin 语法依据 [SEGGER 官方 Commander 文档](https://kb.segger.com/J-Link_Commander#SaveBin)。
7. 离线运行 `python tools/decode_timing_trace.py build/timing_20261021/timing_capture.bin --csv build/timing_20261021/events.csv --json build/timing_20261021/report.json`。
   输出带原二进制 SHA-256，不修改原件；拒绝非 frozen、故障 / 会话变化 / 运动捕获、不完整槽和触发点已覆盖的捕获。
   下一次捕获先在 frozen 状态改 `threshold_us=1200`，再 `request=1`；分别保留两次二进制 / manifest / 全部采集记录，避免覆盖。

`reason`：1 周期触发收尾，2 手动，3 会话变化，4 出现运动授权，5 arm 配置不合法，6 致命异常。
捕获只接受当前非零会话且无运动授权；出现运动授权只结束诊断，不改运动状态，不能把它当作运动保护。
arm 时逻辑清空槽数而不擦 32 KiB 内存；有效槽由本次 count / sequence 确定，旧槽不属于新捕获。
软件验证见文末；新增测点的插桩开销、最长关中断段、环覆盖长度和真实停止行为尚未实测。

## 2026-10-22 分段测量的依据与操作

用户第二份 RAM 为 state=3 / reason=1，744 有效事件、13.089 ms；13 次 0x11 普通记录帧发送接口均接受。
触发周期 1111.115 µs，控制就绪到入口 122.207 µs；其中主要 POLL 区间 103.758 µs，扣除 IRQ 并集后 90.940 µs。
POLL_BEGIN 到 FRAME_BEGIN 扣除 IRQ 后 86.869 µs，不能全部称为 CRC。此窗口没有通道表帧。
验证层级为用户 RAM / Python 解码 / 独立原始字节与 DWT、IRQ 复核；未关联对应完整 30 s 原流，不代表全程周期验收。
详细原始快照、哈希及分析保存在本工作树忽略目录 `build/timing_20261021/auto_capture_20261022*`。

新增分段沿用上面的 30 s 无运动记录流程，threshold_us=1050；需要用户下载新 ELF 并确认新 RTT 标识后才有新事件。
Ozone 仍使用 `build/timing_20261021/MC02_H723_3508_timing.jdebug`。导出前从当前 ELF / manifest 核对地址，
不要沿用此前 RAM 地址；对象仍为 32928 字节，Ozone Save Memory 的 Size 填 `0x80A0`（框内数字按十六进制）。
2026-10-22 本次已验证构建的地址为 `0x2000C578`；后续重建仍以对应 manifest / ELF 为准。
导出另存新文件，例如 `timing_capture_phases_01.bin`，不要覆盖旧捕获；读取后先检查新事件是否实际存在。
本轮软件准备不操作硬件，烧录 / 采集仍由用户完成；未下载的新 ELF 不能用于解释之前的 RAM。

解码 JSON 的 `phases.LOG_PEEK` / `phases.ENCODE` / `phases.CRC` 按独立 BEGIN/END 序号配对：

| 字段 / 阶段 | 含义 |
| --- | --- |
| LOG_PEEK | 取得普通记录指针，或准备一个通道表分页；END 为 0 表示队列空，否则高 16 位类型、低 16 位数据长度 |
| ENCODE | `sysid_frame_encode()` 全部，BEGIN 是类型 / 数据长度，END 是完整帧长度 |
| CRC | 编码内部 `sysid_crc16()`，BEGIN 是数据长度，END 是 CRC 值；不测接收解析或 schema_id 的 CRC |
| wall_us / irq_overlap_us | 墙钟区间 / 同一区间 IRQ 并集；嵌套 IRQ 不重复累计 |
| non_irq_us | 覆盖完整时的墙钟减 IRQ，仍包含测点 / 调度 / 其他任务时间；覆盖不全则为 null |
| partial_phase_prefix / partial_phase_at_end | 环开头缺 BEGIN / 冻结时缺 END 的阶段名；不补零、不当作完整耗时 |

CRC 是 ENCODE 的子区间，不能把两者相加；ENCODE 减 CRC 也不能全称为复制耗时。
普通记录的数值打包已经在控制任务完成，LOG_PEEK 不包含该过程；通道表则会在 peek 时打包分页。
普通版不求值诊断宏参数，不链接记录器；不改 CRC 算法、协议、优先级、队列、运动授权或质量阈值。
新事件在原 27 个事件之后追加，格式版本仍为 MBT1 v1，当前解码器可读旧捕获；旧解码器会拒绝新事件。
每次非空新帧额外记录六个事件，每次空 peek 两个；环覆盖长度会缩短，测点本身扰动尚须实测。

## 要回答的问题

把长周期分成控制任务变为就绪之前、就绪后等待、任务执行三个部分，检查高优先级 RX 的哪一段与等待重叠。
必须同时看到任务就绪、上下文切换和中断；只测 `sysid_poll()` 前后不能证明它占用了多少 CPU。
`control.period` 是相邻任务入口的间隔，不是本周期启动延迟；两者不能互相替代。

现有数据的限制：`control.exec` 从任务入口到记录入队前，缺少入口前的等待及 `sysid_record()` 耗时。
通道表与尖峰的发送顺序关联也不能代替任务时间线。表外背景抖动仍须一起解释。

源码与预算还有一处差异：`ARCHITECTURE.md` 实时预算写 comm_rx 每轮最多 32 帧或 100 µs，而本专用 RX 当前用 `while` 排空，
没有该限额。此处以实际任务源码描述现状；预算是设计要求，既不是已实现的限额，也不能当成本次抖动的实测原因。
本轮不据此直接加限额，后续修订须同时核对反馈年龄、命令响应和队列积压。

## 源码测量点

以下路径均相对本仓库；钩子核对的是本仓库 H723 随附的 FreeRTOS，而非其他版本手册。

| 事件 | 放置位置 / 现有调用 | 目的 |
| --- | --- | --- |
| CONTROL_READY | `tasks.c` 的 `prvAddTaskToReadyList()` 中 `tracePOST_MOVED_TASK_TO_READY_STATE(pxTCB)` | 入就绪表之后，记录控制任务本次真正变为就绪的时刻 |
| TASK_IN / TASK_OUT | `tasks.c` 的 `traceTASK_SWITCHED_IN()` / `traceTASK_SWITCHED_OUT()` | 记录所有任务的切换，区分 RX、control、detect、idle 及其他任务 |
| CONTROL_ENTER | `01_applic/tasks/motor_bench_task.c` 循环中的 `start_us` | 对齐现有 `control.period`，记录本地控制迭代号 |
| CONTROL_SAMPLE | 同文件 `sample_us` 读取处 | 保存原有微秒时间戳，关联 USB 记录；包含本次 SET 会话号的关联记录另在设置应用后写入 |
| CONTROL_SUBMIT | 同文件 `motor_group_send()` 前 / 后 | 看到控制提交是否也延迟；仅指软件提交，不是 CAN 线上发送完成 |
| RECORD_BEGIN / END | 同文件 `sysid_record()` 前 / 后 | 补齐现有 `control.exec` 未覆盖的入队耗时 |
| RX_BEGIN / END | `01_applic/tasks/motor_bench_rx_task.c` 等待返回之后 / 下次等待之前 | 包住一次 RX 唤醒的全部工作；保留通知返回值，不能据它识别是哪一种中断 |
| DISPATCH_BEGIN / END | 同文件 CAN / DBUS / USB 收取和回执交接的前 / 后 | 区分反馈分发与遥测发送；记各类处理数量，不在热路径格式化 |
| POLL_BEGIN / END | 同文件 `sysid_poll()` 前 / 后 | 记录命令解析、帧准备及发送尝试的整个调用区间 |
| FRAME_BEGIN / END | `01_applic/tasks/motor_bench_task.c` 的 `send_record()` 包住 `usb_cdc_write()` | 记录帧类型 `frame[3]`、长度、接受 / 忙；该区间不包含此前的编码或后续 USB 传输 |
| LOG_PEEK_BEGIN / END | `sysid_poll()` 调用 `sysid_log_peek()` 前 / 后 | 普通记录取指针、通道表分页打包，空队列也有完整配对 |
| ENCODE_BEGIN / END | `sysid_poll()` 调用 `sysid_frame_encode()` 前 / 后 | 一次完整封帧；忙重试复用旧帧，不重新编码 |
| CRC_BEGIN / END | `sysid_frame_encode()` 内的 `sysid_crc16()` 前 / 后 | 仅本次发帧 CRC，与 ENCODE 嵌套，不归入 USB 在途时间 |
| IRQ_IN / IRQ_OUT | 本预设实际启用的各中断处理入口 / 出口 | 从任务区间中分离中断占用，记录 IRQ 编号并正确处理嵌套 |

`tracePOST_MOVED_TASK_TO_READY_STATE` 在 `tasks.c:226`；切换钩子在 `tasks.c:3081/3124`，首次切入另见 2062。
该文件位于 `06_boards/dm_mc02_h723/Middlewares/Third_Party/FreeRTOS/Source/`。
这三个宏在随附 `include/FreeRTOS.h` 中有默认空定义，可由诊断构建配置提供，不修改内核源码。
诊断配置应通过不依赖 FreeRTOS 类型的桥接声明传递任务句柄；不读取私有 TCB 字段，不在钩子内调用任务 / 分配 / 日志接口。
只过滤 READY 的控制任务句柄；切换事件须覆盖所有任务。任务句柄在创建后登记，登记前事件不能误归为控制任务。

不能用普通 tick 钩子替代 READY：本版本 `vApplicationTickHook()` 在本轮解除延时阻塞之后才执行，且还会在调度器挂起时调用。
控制任务若已超时、`rm_task_delay_until()` 未实际阻塞，下一迭代没有新的 READY，标为“未阻塞 / 追赶”，不能沿用上周期 READY。
任务切入发生在内核选定任务处，仍不等于应用循环入口；两种事件都要保留。

IRQ 清单从该诊断预设实际启用的 NVIC / 外设配置确认，至少检查 SysTick、HAL 时基 TIM2、USB OTG HS、FDCAN1、
DBUS 的 UART / DMA 及实际启动的其他 DMA。不能只包 USB 和 CAN 就宣称已扣除全部中断。
NMI / HardFault 不作为正常采集事件；发生致命异常、复位或覆盖不完整时，本次捕获不能用于完整时间归因。

## 保存与读取

采用**独立诊断预设、RAM 环形事件记录、冻结后读取**。不加入现有 USB 通道表，不通过 RTT 逐条输出，不新增发送任务。
普通 `h723-m3508` 不包含诊断记录器；诊断预设保留相同优化等级、任务优先级、队列、通信节奏与控制设置。
此实现只为当前单电机排查，不引入通用追踪任务或新的通信协议。

- 所有事件使用同一 DWT 时基；原始周期计数器访问放在 `05_platform/`，应用不直接读寄存器。
  记录时钟频率、计数宽度、固件提交、格式版本、任务句柄映射、IRQ 覆盖清单和冻结原因。
- 每项至少保存提交序号、原始时间、事件 / 上下文和参数；参数关联控制迭代号、会话号或帧类型 / 长度。
  关联多项参数的事件必须有明确配对标识；不能用“最靠近的 USB 帧”猜对应周期。
- 任务与中断共用记录器时，在平台层用保存 / 恢复 PRIMASK 的短临界区完成时间戳、槽写入和提交。
  里面只读时钟、写固定字段，不调用 RTOS、USB、日志或内存分配；不用任务版临界区接口包 ISR。
  提交序号最后写，读取者能区分完整与未完成槽，不能只有 `volatile` 而无提交约定。
- 缓冲容量与每项布局见 `trace.h`，已由静态断言和链接检查核对；本版诊断 DTCM 总用量 108056 / 131072 字节。
  槽数决定的是事件容量，不保证时间覆盖；记录绕回 / 覆盖计数，不静默丢失。
- 在调试器中先请求 arm，由任务确认清空 / 建立时基后进入 armed；确认前不分析捕获。
  arm 只接受已准备的当前 SET 会话，旧 FIFO 和首次循环不触发“本会话长周期”。不新增 USB arm 命令。
- 诊断触发使用现有 1050 µs 草案界线，另一次用 1200 µs 捕捉较大尖峰；这是两类捕获选择，
  **不是修改质量阈值**。触发后再保留尾部控制迭代（数量见 `trace.h`），观察追赶，再冻结事件写入，不冻结控制任务。
  arm 时锁存触发条件；不能把不同条件的捕获拼成连续时间线。
- 冻结标志只在最后一项提交后发布；停止后才能读取完整缓冲和元信息。先确认无运动授权，再由用户暂停 / 读取 Ozone 内存。
  捕获期间关闭高频变量刷新，不下断点；暂停造成的时间跳跃不能放进运行统计。
- 导出原始 RAM 二进制和元信息到仓库外，记录地址、长度及 SHA-256；具体读取操作在诊断 ELF 的符号 / 地址固定后编写。
  不要求用户手抄数千条事件；当前 ELF / 地址 / 导出脚本见本版使用步骤。

DWT 原始 32 位计数回绕需按已记录频率和事件相邻差处理；相邻事件跨过整圈、时钟改变或重启时不能唯一展开，应判捕获无效。
时基关联用 CONTROL_SAMPLE 的既有微秒戳；不能拿电脑到包时间对齐内核切换。

## 如何判读

记某次周期的 READY 为 r、首次切入为 s、应用入口为 a，均来自同一 MCU 时基。
报告 `s-r`（就绪到切入）、`a-s`（切入到入口）、相邻 a 的间隔及软件提交时刻；无对应 READY 的追赶周期单独列出。
READY 只给出解除阻塞时刻；若要区分 tick 本身晚到与就绪后的等待，还须用 SysTick 事件核对节拍相位。
不能把首次任意 tick 的 DWT 时间当成经校准的物理周期起点，或把 READY 延迟等同于相邻周期偏差。

按 TASK_IN / OUT 划分当前任务，再按带编号的 IRQ_IN / OUT 栈分割区间；嵌套中断时间按区间并集扣除，不能重复相加。
调度钩子与真实指令恢复之间仍有切换开销，作为剩余量报告。事件不配对、缺失、被覆盖、没有触发前 READY 的窗口不能给出完整分解。

| 观察 | 可支持的结论 | 仍不能证明 |
| --- | --- | --- |
| 控制 READY 后，RX 在 POLL 区间运行，随后 control 才切入 | 遥测路径占据了本次就绪等待的一部分 | CRC 是唯一原因，或降低整个 RX 优先级合理 |
| READY 等待主要落在 DISPATCH，CAN / UART 处理计数同时增加 | 反馈分发是本窗口干扰候选，可进一步定位 | 允许反馈过期或直接限制帧数就能达标 |
| 等待区间主要由某 IRQ 覆盖 | 该 IRQ 占用了本窗口时间 | IRQ 内部哪一个函数耗时，或其他窗口原因相同 |
| FRAME 为通道表，POLL 比普通记录明显长 | 分页准备 / 发送尝试路径值得细分 | FRAME 的 `usb_cdc_write()` 耗时包含 CRC 或 USB 在途时间 |
| 长周期后出现短周期且没有新阻塞 / READY | 该迭代在追赶绝对节拍 | 控制已经满足当前周期质量草案 |

每段 POLL 与它内部多个 FRAME 都保留；只能把编码时间列为 POLL 内尚未归因的部分，不能把整段 POLL 减 FRAME 后全称为 CRC。
本轮已经为编码 / 通道表取数增加第二轮测点；在取得分段实测前，不修改协议或 CRC 算法。

## 验证层级与后续测量

1. **主机：** `test_trace` 六例覆盖 arm 边界、阈值锁存、环形覆盖、尾部、冻结后不变、计数回绕、IRQ 屏蔽恢复及结束原因；
   Python 七例覆盖嵌套并集、READY 不沿用、DWT 回绕、损坏拒绝、IRQ 覆盖缺口，以及分段嵌套 / 前后截断 / 非法配对。
   实际 C 生成 MBT1 二进制，Python 独立核对字段、等待 / 嵌套时间和新增事件编号 / 阶段耗时。
   `test_sysid_plain` / `test_sysid_timing` 各三例验证真实插件的通道表、记录、空队列及忙重试，另比较两种构建的输出帧逐字节一致。
   这些是合成事件，不是 MCU 性能测试。
2. **编译：** WSL host-tests 41/41；诊断 / 普通单电机 / 默认 H723 / C 板构建，零警告。
   检查三种普通 ELF 无 `rm_trace`、诊断内核确有就绪 / 切换钩子、31 个外设入口及 SysTick 插桩、USER CODE 边界、Keil 清单。
   2026-10-22 分段版另通过 189 文件格式检查、普通 / 诊断实际输出字节相同及旧真实 RAM 捕获字段回归；旧捕获不伪造新增阶段数据。
   PendSV 的切换前后开销没有完整 IRQ 包围，解码不把剩余量宣称为纯任务 CPU 时间；NVIC 位图有未覆盖项时不能完整归因。
3. 校准空事件写入的 MCU 开销和最长关中断段，并记录捕获事件率 / 覆盖长度；常数开销不能简单逐项相减来消除缓存和抢占扰动。
   用户用相同无运动工况比较未插桩基线与诊断版完整记录分布；插桩版变好或变差都必须报告。
4. 用户烧录 / 操作硬件，先确认新诊断启动标识、运动授权关闭，再分别捕获背景抖动和较大尖峰。
   返回冻结的事件二进制、元信息和对应完整采集记录；AI 只读分析，验证序号、嵌套、时基及完整性后再归因。
5. 有效时间线确认主导路径后，才提出最小修订；原有 1.05 ms 草案和全部样本统计保持不变。

温度保持不可用，不阻塞这项离线准备；本方案没有授权 AI 烧录、接电机或启动运动。
第 1–2 步软件实现与离线验证已完成；下一项为用户按第 3–4 步进行无运动捕获与插桩扰动核对。
