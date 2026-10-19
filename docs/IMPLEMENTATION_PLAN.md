# 实施计划

从 `ARCHITECTURE.md` 拆出（2026-10-18）。**清单的勾选状态就是当前进度。**

先做什么、做到什么程度算完成。2026-09-25 按“从零新写”重写（前一版建立在“以 COD\_UniCFramework 为底座演进”上，已作废，ADR 0017）；原为单独的《实施计划》，2026-09-28 并入本文。**清单的勾选状态就是当前进度。**

### 结论

- 在 `COD_RoboCore/` 中**从零新写**。COD-H7-Template 与 COD\_UniCFramework 是同级的首要参考，其他战队的项目作为补充参考。
- 已确认的决策：ADR 0001–0025，其中 0017–0025 是 2026-09-25 确认的。
- 先打通**一条完整、安全的最小链路**，再按兵种扩展。
- 下表的周数只是粗估，每个阶段结束后按实际速度校准。

### 已经具备的条件（不用再做）

独立单电机分支 `bench/m3508-control`（2026-10-18，ADR 0085）：
- [x] MC02 单台 M3508 的轨迹 / PI / 前馈、总抗饱和、DR16 锁定、USB 自描述记录及编译 / 主机数值核对。
- [x] 页面本次参数 → 固件停止态校验 → 实际值回读 / 日志保存；温度保护与模型覆盖分开（2026-10-19）。
- [ ] 用户烧录及停止 / 失联实测；采新轨迹验证 PI / 前馈对照。操作见 `M3508_CONTROL_BENCH.md`。

| 条件 | 证据 |
| --- | --- |
| WSL 2 + Ubuntu 24.04、与 CI 版本一致的工具、arm-gcc 15.2.1 | `docs/DEV_ENVIRONMENT.md` 第 1–5 节，2026-09-24 实测 |
| Ozone V3.50b + J-Link 在 Windows 上连接 MC02、烧录、看 RTT、看 FreeRTOS 任务 | 同上，第 9 节，2026-09-25 实测 |
| 一个“已知正常”的对照固件：UniC `4220d9c` 在这块 MC02 上能跑，LED 绿色心跳 | 同上；新模板上板出问题时，先烧它来排除硬件问题 |
| 同一块板的资源分配、协议与坑 | 附录 A |

### 代码从哪里借鉴

从零新写不等于闭门造车。每个模块写之前，先看参考项目里对应的实现，**理解之后自己写**。

| 来源 | 许可证 | 可以怎么用 |
| --- | --- | --- |
| COD-H7-Template | MIT | `.ioc` 可以作为 CubeMX 配置的起点；算法和协议解析可以参考。复制代码时保留版权声明 |
| COD\_UniCFramework | MIT | 链接脚本段、构建脚本、测试与 CI 做法、实测参数可以参考。复制时保留版权声明 |
| basic\_framework | MIT | 同上 |
| taproot | GPL-3.0（`COPYING`） | 只借鉴思路，**不复制代码**：复制会让整个模板都受 GPL 约束 |
| RM2024-PowerModule、SPR 等 | 未声明许可证 | 只借鉴思路和公式，不复制代码 |
| 达妙官方例程（Gitee） | 未声明许可证 | 只读取引脚等事实 |

### 分阶段计划

| 阶段 | 粗估 | 做什么 | 验收标准 |
| --- | --- | --- | --- |
| **0 骨架** | 2–3 周 | 仓库、目录、CMake、CubeMX 板级工程、链接脚本、`05_platform/stm32h7` 的时间、GPIO、串口、CAN，以及 RTT 日志、Unity 主机测试、`REGEN_CHECKLIST.md`，清单见下一节 | 零警告编译；LED 心跳；RTT 打印；FDCAN 收到 DJI 电机的反馈帧（电机上电但不下发指令），证明波特率为 1 Mbit/s；主机测试能运行 |
| **1 最小完整链路（H723）** | 3–4 周 | 一台电机 + 遥控 + 安全停机，清单见下文 | 下方“第一版验收”四条全部通过，台架实测数据写进 `docs/budget.md` |
| **2 F407 最小移植** | 2 周 | `05_platform/stm32f4`，跑同一条最小链路 | 同样四条在 C 板上通过 |
| **3 设备层** | 2–3 周 | DJI 电机（3508/2006/6020）、达妙电机（MIT，含 FD 总线）、BMI088（含加热恒温）、电机组打包、分类发送队列、`frame_codec` | 每种电机实测停发后多久停转；每种安全动作都做台架验证；CAN 负载实测 |
| **4 姿态 + 云台** | 2–3 周 | EKF（改为静态内存）、云台误差计算（FLU 下重新推导）、`GimbalState` | 主机测试有对照数据；步兵云台上板跟随。`Gimbal_Quat.c` 等来源暂缓，到这一阶段再决定是找回还是新写 |
| **5 步兵整车** | 3–4 周 | 底盘（含功率控制）、发射（热量、卡弹）、裁判系统 v2.0.0、图传键鼠、输入标准化、UI、自瞄通信（带时间戳）、`RobotCmd` | 步兵可上场；遥控丢失、电机离线、拔 CAN 逐项台架验证通过；不超功率、不超热量；阵亡复活不猛冲 |
| **6 参数存储** | 1–2 周 | Flash 双区标定值，处理 H7 的 ECC | 写到一半掉电的测试通过 |
| **7 轮腿** | 4 周以上 | 腿部运动学、LQR/VMC（增益离线拟合）、离地检测、蹲下安全动作 | 台架平衡；各类故障下都不猛倒 |
| **8 多板 / 哨兵** | 2–3 周 | BoardLink（步兵双板也用）、双云台、导航接口 | 任一块板掉线、重启或写 Flash 停顿时，依赖它的机构都执行机构停，不互相锁死 |
| **9 工程** | 待排期 | 机械臂运动学、重力补偿、自定义控制器 | 另定 |
| **重装机器人** | 暂缓 | 规则尚未发布 | — |

阶段之间的依赖：0 → 1 → 3 是主线；2（F407）可以在 3 之后再做；4、5 依赖 3；7、8 依赖 5 里的底盘和命令链路。

**第一版验收（阶段 1、阶段 2 必须全部通过）：**

1. **反馈读取一致**：压力测试下，快照里不出现不同帧的字段混在一起。
2. **控制不被通信拖死**：CAN 满负载时，control 的启动延迟和提交时刻仍在阈值内。
3. **故障按时停下**：遥控丢失、急停、拔 CAN、主控复位，电机在规定时间内执行安全动作。
4. **恢复不猛冲**：故障解除并重新解锁后，输出按斜坡恢复，不出现突然运动。

### 阶段 0 清单（新模板骨架）

每一项对应一次或几次小提交，按顺序做。

**仓库与规范**
- [x] WSL 中设置 git 身份（`user.name`、`user.email`、`init.defaultBranch main`）（2026-09-25）
- [x] 在 `COD_RoboCore/` 中执行 `git init`，并设置 `core.filemode false`（2026-09-25）
- [x] 第一个提交 `0af8c5f`（2026-09-25）：`.gitattributes`（`* text=auto eol=lf`，强制 LF；`.bat/.cmd/.ps1` 用 CRLF；图片、ELF 等为二进制）+ `.gitignore`；`git ls-files --eol` 确认为 `i/lf w/lf`
- [x] 补充 `.editorconfig`、`README.md`、`docs/CODING_STANDARD.md`、`.clang-format`（提交 `04bc291`，2026-09-27；clang-format 18.1.3 解析通过）
  - 在终端里用 heredoc 写文件时，复制的内容行首可能带空格，导致结束标记 `EOF` 不被识别。做法是用 `sed 's/^[[:space:]]*//' > 文件 <<'EOF'`，粘贴内容后**手动输入** `EOF`；
  - 设计文档原先放在仓库外（2026-09-25 决定）；2026-09-28 合并后移入 `docs/`（本文、`DEV_ENVIRONMENT.md`）。
- [ ] `.clang-tidy`，命名规则按编码规范（snake\_case、类型 PascalCase）。**推迟到 CMake 骨架之后**（2026-09-27）：clang-tidy 需要 `compile_commands.json` 和实际代码才能验证
- [x] 按《架构设计》建立目录骨架（空目录放 `README.md`，说明这个目录放什么）。只建 10 个顶层目录，子目录等有代码时再建（提交 `0ebb6a6`，2026-09-27）

**板级工程（06_boards/dm\_mc02\_h723）**
- [x] 以 COD-H7-Template 的 `.ioc` 为起点复制一份（MIT，保留版权说明），在 CubeMX 里：（2026-09-28 完成，`c648b77`；CMSIS_V2、启动任务、550 MHz，差异见仓库 `docs/CHANGES_FROM_COD_H7_TEMPLATE.md`）
  - 工具链改为 **CMake**；
  - 删掉默认任务，不定义任何任务或队列（ADR 0025）；
  - 确认 HAL 时基是 TIM；
  - 核对 FDCAN1、FDCAN3 的 `CalculateBaudRateNominal=1000000`，FDCAN2 为 FD；
  - 核对 SPI 的 Data Size。
- [x] 链接脚本：`.bss` 放在 DTCM；增加 `.dma_buf`（AXI SRAM）；MPU 把 AXI SRAM 设为不可缓存（ADR 0021）。2026-09-28 完成：用自己的 `dm_mc02.ld`（CubeMX 改不到），不再需要 ASSERT；`.bdma_buf`（需另加 MPU 区域）等到有 BDMA 设备时加，`.noinit` 随阶段 1 故障记录一起加并上板验证 ECC
- [x] 写 `REGEN_CHECKLIST.md`，然后故意重新生成一次代码，按清单核对一遍（2026-09-28：实际重新生成两次，第一次就抓到 18 个空中断函数）

**构建**
- [x] 顶层 `CMakeLists.txt`、`cmake/toolchain-arm-gcc.cmake`、`cmake/board-dm_mc02.cmake`、`cmake/warnings.cmake`（2026-09-28 全部完成，`9d7d27e`）
- [x] `CMakePresets.json`：`h723-bench-debug` 和 `host-tests`（2026-09-28 完成，`9d7d27e`）
- [ ] 构建脚本：打印实际的构建类型；警告数必须为 0；没有重新编译任何文件时报告 “UP TO DATE”

**最小平台与核心**
- [x] `05_platform/time/time_stm32h7.c`：DWT 扩展成 64 位的 `rm_time_now_us()`，启动后自检计数器确实在增长（ADR 0020）（2026-09-28 `ebb48ed`，上板：跨过第一次回绕连续）
- [x] `04_core/log`：SEGGER RTT，E/W/I/D 四级（2026-09-28，上板 RTT 输出正常）
- [x] 状态灯（WS2812 接 SPI6，发送方式按附录 A.1）（2026-09-28 上板：绿灯每拍闪两下）。`gpio.c` 等到有 GPIO 设备时再加
- [x] `04_core/os`：用原生 FreeRTOS API 静态创建任务，并实现 `rm_task_delay_until()`（2026-09-28，上板心跳周期 500 ms 无漂移）
- [x] `01_applic/bench/`：`app_main()` 创建一个心跳任务，由它驱动状态灯并通过 RTT 打印（2026-09-28 上板通过）
- [x] `05_platform/can/can_stm32h7.c` 最小版本：可以发送；可以按精确 ID 和精确范围订阅（2026-09-28 编译 + 主机测试：DLC 换算、接收环形缓冲；comm_rx_task 分发；心跳每秒打印 CAN1 反馈帧数。**上板待 V30**）
- [x] `05_platform/uart/uart_stm32h7.c` 最小版本：DMA 循环接收 + 空闲中断，缓冲区放在 `.dma_buf` 段（2026-09-28 编译 + 主机测试：取数逻辑 `05_platform/uart/dma_ring` 5 项；出错自动重启接收；心跳每秒打印 UART5 收到的字节数。**上板待 DR16**）

**测试与 CI**
- [x] `tests/host`：接入 Unity，第一个测试用 DWT 64 位扩展的回绕逻辑（`9369599`，2026-09-27；纯函数放在 `05_platform/time/cycle_extend`，time.c 上板时调用它）
- [ ] `tools/check_linked.py`：确认清单里的函数确实在 ELF 中
- [ ] CI（等仓库托管到 GitHub 或 Gitee 之后）：格式检查、两套构建、主机测试、第三方代码校验和

**阶段 0 完成的判据**：以上勾选完；上板看到 LED 心跳和 RTT 日志；接上一台 DJI 电机（只上电、不下发指令），能收到它的反馈帧；结果记入本章清单和 `docs/DEV_ENVIRONMENT.md`。

### 阶段 1 清单（一台电机 + 遥控 + 安全停机）

接线沿用 COD-H7-Template：M3508 接 FDCAN1，ID 1；DR16 接 UART5。

- [ ] 共享数据：`watchdog_feed_data/read_data`（临界区拷贝 + 时间戳，ADR 0059）、`RcState`（dr16）完成；`RobotCmd` 未做（2026-10-01 ADR 0058 去掉话题层）
- [x] `04_core/watchdog`：在线状态在读取时计算；清单打印（2026-09-28，detect_task 打印；上板待 V4）
- [x] `02_devices/remote/dr16`：检查帧长和取值范围，输出 `RcState`；用录制帧和错误帧做单元测试（模糊测试推迟到阶段 5 以后）（2026-09-28，主机测试 9 项，帧由独立的组帧函数生成、并用 Python 算的字节核对；上板待 V10–V12）
- [x] `02_devices/motor`：`motor.h` 统一接口 + `dji_motor.c`（先只做 M3508）、ID 冲突检查、反馈快照（2026-09-28：M3508 / M2006 / GM6020 反馈，M3508 / M2006 力矩指令，主机测试；上板待 V30–V32）
- [x] `motor_group`：按周期打包；没有写入的槽位填零力矩（2026-09-28，主机测试；固件里第 6 步 control_task 才调用）
- [x] `01_applic/system/safety_gate`：遥控丢失、急停、未解锁 → `stop_all`；全车停时发送出口把每个电机改写为 `stop_action`（ADR 0026）（2026-09-28，ADR 0032；IMU 未就绪 2026-09-28 加入，ADR 0034；上板待 V33–V38）
- [x] 模式状态机 Init → Safe → Manual（表驱动）；进入 Manual 时输出斜坡 300 ms；离线后不自动恢复，需要重新解锁（2026-09-28：在 `safety_gate` 里用 `switch` 实现，主机测试 7 项）
- [x] `control` 任务（1 kHz）：周期开头读快照，末尾统一提交；记录执行时间、周期间隔的最大值和超期次数（只做调试统计，不联动安全门）（2026-10-10 ADR 0074，log_task 每秒打印；上板待 V54）
- [ ] `command` 任务：`OperatorInput`（先只接 DR16）→ `RobotCmd`
- [x] IWDG：detect 看到 control_task 心跳在涨才喂狗，50 ms；调试暂停时 IWDG 也暂停；致命故障经 IWDG 立即复位（2026-10-10 ADR 0074；上板待 V51–V53）
- [ ] 错误处理：`RM_ASSERT`（Release 下记录后直接复位）、`RM_CHECK`、错误表，以及 `.noinit` 故障记录（magic + CRC）
- [ ] 主机测试：全车停的每个触发条件、状态机全表遍历、话题重复认领、在线判断超时边界
- [ ] **台架实测**（电机架空并固定，断电开关放在手边）：
  - 关遥控、拨急停、拔 CAN、复位这四种情况下，M3508 分别多久停转，写进 `docs/budget.md`；
  - 恢复解锁后是否不猛冲。

**完成标准**：遥控能控制电机转速；四种故障下电机都在 100 ms 内停下（台架实测；其中“关遥控”一项从关遥控算起为 200 ms 丢失判定 + 停机时间，ADR 0030）；恢复解锁后不猛冲。

### COD-H7-Template 全量移植（2026-09-28 起）

用户 2026-09-28 决定：把 COD-H7-Template 的全部功能按本文的分层移植过来，**先写代码并做主机测试，硬件验证统一记入
`docs/VERIFICATION_TODO.md`，攒起来上板**。最终行为与旧工程一致（遥控器控制一台 M3508 转速、IMU 姿态解算与加热恒温、
达妙电机、裁判系统、图传、上位机通信），并加上安全门。算法按“先原样搬、再改行为”（见“从现有代码迁移”），
复制的代码保留原 MIT 版权声明；与旧工程的每处差异记入 `docs/CHANGES_FROM_COD_H7_TEMPLATE.md`。

按依赖顺序，一个模块一次提交：

| # | 旧工程 | 新位置 | 状态 |
| --- | --- | --- | --- |
| 1 | `BSP/bsp_can.c` | `05_platform/can/can.h`、`05_platform/can/can_stm32h7.c`（DLC 换算、接收环形缓冲放 `05_platform/can/`） | 代码完成（2026-09-28），待上板 V30 |
| 2 | `Algorithm/CRC.c` | `04_core/util/crc.{h,c}` | 代码完成（2026-09-28），主机测试 6 项；纯计算，无需上板 |
| 3 | `Controller/PID.c`、`Algorithm/Ramp.c`、`LPF.c`、`Kalman_Filter.c` | `03_algorithm/control/pid`、`03_algorithm/control/ramp`、`03_algorithm/filter/lpf`、`03_algorithm/filter/kalman` || 代码完成（2026-09-28）：PID、斜坡、低通、卡尔曼 + `03_algorithm/math/matrix`，主机测试 29 项；纯计算，无需上板 |
| 4 | `Device/Remote_Control.c`、`Task/Detect_Task.c` | `02_devices/remote/dr16`、`04_core/watchdog`、`04_core/msg/rc_state` | 代码完成（2026-09-28），另补 `04_core/msg/topic`、detect_task；主机测试 19 项；上板待 V4、V10–V12 |
| 5 | `Device/Motor.c`（DJI 部分）、`Task/CAN_Task.c` | `02_devices/motor/motor.h`、`motor.c`、`dji_motor`、`motor_group` | 代码完成（2026-09-28），主机测试 21 项；bench 只收反馈、不发指令；上板待 V30–V32 |
| 6 | `Task/Control_Task.c` | `01_applic/system/safety_gate`、`01_applic/system/control_task`、`01_applic/bench/`（遥控 → 底盘电机转速） | 代码完成（2026-09-28），主机测试 9 项（含新旧 PID 闭环等价）；**固件开始发电机指令**，上板待 V33–V38（台架） |
| 7 | `BSP/bsp_spi.c`、`bsp_gpio.c`、`bsp_pwm.c`、`Device/Bmi088.c` | `05_platform/…/spi`、`gpio`、`pwm`，`02_devices/imu/bmi088`（含加热恒温） | 代码完成（2026-09-28）：spi、pwm、bmi088、加热（UniC 参数）、`gyro_bias`；gpio 推迟到第 8 步（ADR 0033）；主机测试 15 项；上板待 V5、V6 |
| 8 | `Algorithm/Quaternion.c`、`Task/INS_Task.c` | `03_algorithm/attitude/quat_ekf`、`01_applic/modules/ins`、`04_core/msg/imu_state` | 代码完成（2026-09-28）：EKF（卡方公式改正、实测 dt）、上电零偏标定、安装旋转、IMU 未就绪全车停；轮询驱动（ADR 0034）；主机测试 16 项；上板待 V7–V9 |
| 9 | `Device/Motor.c`（达妙部分） | `02_devices/motor/dm_motor`（含 FDCAN2 的 FD 总线） | 代码完成（2026-09-28）：MIT、使能按期望状态对齐、FD 帧、跨品牌 ID 查重（ADR 0035）；样板接一台 DM8009；主机测试 18 项；上板待 V39–V42 |
| 10 | `Device/Referee_System.c`、`Image_Transmission.c` | `02_devices/referee/referee`（协议 v2.0.0）、`02_devices/remote/vt_link` | 图传完成（2026-09-28）：`referee_frame`（0xA5 帧检查）、`vt_link`（VT13、0x0304），主机测试 11 项，上板待 V13、V14；**裁判系统等官方 V2.0.0 文档**（ADR 0036） |
| 11 | `Device/MiniPC.c`、USB CDC | `05_platform/…/usb_cdc`、`02_devices/vision/vision_link` | 通道和帧层完成（2026-09-28，ADR 0037）：usb_cdc、byte_ring、vision_frame、vision_link；主机测试 8 项；上板待 V15；**消息字段等视觉组协议** |
| 12 | `BSP/bsp_adc.c`、`Algorithm/RLS.c`、蜂鸣器 | `05_platform/…/adc`、`02_devices/battery`、`03_algorithm/power/rls`、`02_devices/buzzer` | 代码完成（2026-09-28，ADR 0038）：adc、battery（6S / 21.0 V）、buzzer（启动 / 解锁 / 上锁 / 低电量）、rls（标准公式重写，暂未接入）；主机测试 12 项；上板待 V16–V18 |
| 13 | （旧工程只有一台电机的速度环） | `03_algorithm/kinematics/{omni,mecanum,steer}`、`01_applic/modules/chassis`、`01_applic/config` | 代码完成（2026-09-30，ADR 0043）：三种运动学 + 底盘子系统 + 固件（四轮全向轮）；主机测试 26 项；上板待 V45–V49（车架空） |

### 风险

- **从零新写的工作量大于在 UniC 上改。** 对策：严格按“最小链路优先”推进，暂时用不到的通用机制推迟到后面阶段（参数 Flash、板间通信、Keil）。每个阶段结束后重新估算。
- **边学边写容易在一个细节上卡住很久。** 对策：每一步都有“对照物”：UniC 固件用来区分硬件问题和软件问题；两个参考工程可以对照读；主机测试先于上板。
- **CubeMX 重新生成会悄悄删代码。** 对策：`REGEN_CHECKLIST.md` + 自己的链接脚本 `dm_mc02.ld` + 框架入口只有一行调用（附录 A 与 ADR 0025）。
- **安全相关的结论只能来自台架实测。** 主机测试全绿，不代表上板安全；涉及电机的每条验收都要写明实测条件。
- **裁判系统协议每个赛季都会变。** 当前是 v2.0.0；新赛季规则发布后，先更新长度表和测试，再上场。
