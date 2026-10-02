# COD RoboCore 协作与上手指南

写给电控组全体队员，按新队员能看懂的程度写。本文只讲**怎么上手、怎么协作、出了问题怎么办**；
代码怎么写看 `docs/CODING_STANDARD.md`，为什么这样设计看 `docs/ARCHITECTURE.md`。本文不重复它们的条文，只给链接。

## 0. 必须做 / 禁止做

先记住这一页，其余内容用到时再查。

**必须**

1. 接电机前：电机固定在台架上、输出轴不带负载、断电开关放在手边（`README.md`“注意事项”）。
2. 上电后约 2 s 内不要动板子：这段时间在标定陀螺零偏，晃动会让标定失败、一直停在 `safe`。
3. 提交前：编译 **0 警告**、电脑侧单元测试全部通过、改过的文件用 `clang-format-18 -i` 格式化（第 4.3 节）。
4. 改 CubeMX 配置后，逐条核对 `06_boards/dm_mc02_h723/REGEN_CHECKLIST.md` 再提交。
5. 汇报结果时写清验证到了哪一级：编译、电脑测试、上板、台架、整车（第 5 节）。
6. 调 PID 先小后大；新参数先在台架上试。

**禁止**

1. 不在 `main` 上直接改代码、直接推送；一律走分支 + 合并请求（第 4 节）。
2. 不在 CubeMX 生成文件的 `USER CODE BEGIN … END` 区域以外写代码，重新生成时会被覆盖。
3. 不修改第三方代码（HAL、CMSIS、FreeRTOS、SEGGER RTT、Unity）。
4. 不在调试器里随手暂停正在控制电机的程序：暂停后 CAN 指令停发，电调怎么反应还没实测。
5. 不把“电脑测试通过”的功能直接上整车：以 `README.md`“已支持的功能”表的验证状态为准。
6. 不提交密码、令牌、Wi-Fi 密码、设备序列号。

## 1. 第一天：把环境跑通

1. 按 `docs/DEV_ENVIRONMENT.md` 第 1–5 节装好 WSL、交叉编译器和工具，每一步都有验证方法
   （第 6–8 节是用 COD_UniCFramework 做的基线验证，新队员可以跳过）。
2. 克隆仓库（地址见第 4.1 节），在 WSL 的仓库根目录依次运行：

   ```bash
   cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests   # 电脑侧单元测试
   cmake --preset h723-debug && cmake --build --preset h723-debug                                 # DM-MC02 固件
   ```

   两条都成功、固件 0 警告，环境就好了。
3. 烧录：用 Ozone（`docs/DEV_ENVIRONMENT.md` 第 9–10 节）。只接 J-Link 和板子，**不接电机**。
   RTT 里看到 `startup done`、之后每秒一行 `alive N, mode safe`，说明跑起来了。
   习惯 Keil 的也可以用 Keil 编译烧录（同一节后面有说明）。
   CLion 经 J-Link GDB 服务器烧录目前会“显示成功但没写进去”，**请用 Ozone 烧录**（`DEV_ENVIRONMENT.md` 11.2）。

卡住时先查 `docs/DEV_ENVIRONMENT.md` 末尾的“常见问题”，再在群里问，附上完整的报错文字。

## 2. 代码地图

### 2.1 先读什么

按顺序读，读完前三份就能自己找代码：

| 顺序 | 文档 | 读它是为了 |
| --- | --- | --- |
| 1 | `README.md` | 模板能做什么、各功能验证到哪一步 |
| 2 | `docs/LOGIC_PSEUDOCODE.md` | 用中文伪代码看懂从上电到发电机指令的全过程，每段标了文件和函数 |
| 3 | `docs/CALL_FLOW.md` | 每个任务的函数调用树，和老模板 COD-H7-Template 的对照 |
| 4 | `docs/CODING_STANDARD.md` | 动手写代码之前 |
| 5 | `docs/ARCHITECTURE.md` | 想知道“为什么这样设计”时查 ADR 表 |

### 2.2 六层目录

依赖只能从编号小的指向编号大的（`03_algorithm`、`04_core` 各层都能用）：

| 目录 | 放什么 | 老模板里对应 |
| --- | --- | --- |
| `01_applic/` | 这台车的业务：任务、机构（底盘、云台……）、参数、安全门 | `Application/` |
| `02_devices/` | 具体设备驱动：电机、IMU、遥控、裁判系统、视觉 | `Components/Device` |
| `03_algorithm/` | 纯计算：PID、滤波、姿态、运动学，电脑上可测 | `Components/Algorithm`、`Controller` |
| `04_core/` | 时钟、日志、看门狗等基础设施 | — |
| `05_platform/` | 唯一直接碰 HAL 和寄存器的一层：CAN、SPI、串口、PWM…… | `BSP/` |
| `06_boards/` | CubeMX 工程、启动文件、链接脚本 | `Core/` |

每个目录都有 `README.md`，进目录先看它。

### 2.3 我想改 X，去哪里

| 想做的事 | 去哪里 |
| --- | --- |
| 改电机 ID、CAN 总线、方向、停机动作 | `01_applic/config/params.h` 的电机表 |
| 调底盘 PID、改尺寸、满杆速度 | `01_applic/config/params.h` |
| 改解锁 / 急停的拨杆、IMU 安装方向 | `01_applic/config/params.h` |
| 其他参数（超时、加热、EKF 噪声……）在哪 | `01_applic/README.md`“参数在哪里”表，**每个参数只在一处定义** |
| 改每个控制周期做什么 | `01_applic/tasks/control_task.c` |
| 改收到 CAN 帧后交给哪个电机 | `01_applic/tasks/comm_rx_task.c` |
| 改 RTT 打印的内容 | `01_applic/tasks/log_task.c` |
| 加一个车上对象（电机、机构） | `01_applic/config/objects.c`（定义 + 初始化）、`objects.h`（声明） |
| 加 / 改任务的优先级、栈 | `01_applic/config/task_table.c` |
| 改引脚、外设、时钟 | CubeMX 打开 `06_boards/dm_mc02_h723/dm_mc02.ioc`，然后核对 `REGEN_CHECKLIST.md` |

## 3. 常见任务的做法

### 3.1 做一台新车（英雄、工程、哨兵……）

按 `README.md`“做一台具体的车”的 5 步做：复制仓库，改 `01_applic/config/` 和 `tasks/` 里的控制逻辑。

### 3.2 新增一个机构（云台、发射……）

1. 在 `01_applic/modules/<机构名>/` 里写一对 `<机构名>.h/.c`：一个结构体 + 一组函数，只做计算，不建任务、不直接碰外设；
   机构之间不互相 include，需要的数据由任务读出来传进去。
2. 在 `tests/host/01_applic/` 里写单元测试（照已有的 `test_chassis` 写）。
3. 在 `config/objects.c` 里创建对象，在 `tasks/control_task.c` 里每个周期调用。
4. 更新该目录的 `README.md` 和 CMake；Keil 用户运行 `python3 tools/keil_sync.py`。

### 3.3 新增一种设备（新电机、新传感器）

1. 放在 `02_devices/<类别>/`，一对 `.h/.c`；只通过 `05_platform/` 的接口访问硬件，不直接调用 `HAL_*`。
2. 协议解析写成纯函数，在 `tests/host/02_devices/` 里用真实抓到的数据帧测试。
3. 在该目录的 `README.md` 写明协议来源和版本、注意事项。
4. 在 `docs/VERIFICATION_TODO.md` 加一行上板验证项：前提、操作、期望。

### 3.4 调参

1. 先在 `params.h` 里改，参数名带单位（`_rad`、`_ms`……）。
2. 用 Ozone 的 Watched Data 或 Data Sampling 实时看变量曲线（`docs/DEV_ENVIRONMENT.md` 第 9 节）。
3. 调好的数值和条件（台架还是整车、负载、电压）写进提交说明。

### 3.5 改 CubeMX 配置

只有确实要改引脚、外设或时钟时才动 CubeMX。生成代码后逐条核对 `REGEN_CHECKLIST.md`，每一条都对应一次真实出过的问题。
`.ioc` 的改动和生成代码放在同一次提交里，提交说明写清改了什么配置。

## 4. Git 协作流程

### 4.1 仓库

主仓库在 Gitee：<https://gitee.com/Xalve/COD_RoboCore>。问题（Issue）和合并请求（Pull Request，下文简称 PR）都在这里提。
没用过 Git 的先看[廖雪峰 Git 教程](https://liaoxuefeng.com/books/git/)的“分支管理”一章。

### 4.2 分支

- `main`：始终能编译、测试通过，受保护，**只能通过 PR 合并**，不能直接推送。
- 功能分支：每做一件事开一个，从最新的 `main` 拉出来，做完合并后删掉。名字用 `类型/简短说明`：
  `feat/gimbal-pid`、`fix/dr16-timeout`、`docs/contributing`。

不按人名建长期分支：长期分支和 `main` 差得越来越多，最后合并时冲突一大片。

### 4.3 一次修改的完整流程

```bash
git switch main && git pull                 # 1. 拿到最新的 main
git switch -c feat/gimbal-pid               # 2. 开分支
# 3. 改代码 ……
clang-format-18 -i 改过的.c 改过的.h          # 4. 格式化
cmake --build --preset host-tests && ctest --preset host-tests   # 5. 电脑测试
cmake --build --preset h723-debug           # 6. 固件 0 警告
git add -p && git commit                    # 7. 提交（逐段确认要提交的内容）
git push -u origin feat/gimbal-pid          # 8. 推到 Gitee（remote 名按你克隆时的为准）
```

然后在 Gitee 网页上从这个分支向 `main` 开 PR。VSCode、CLion 的 Git 面板也能做同样的事，步骤一一对应。

### 4.4 提交说明

格式 `类型: 做了什么`（`docs/CODING_STANDARD.md` 第 18 节），例如 `fix: DR16 丢帧后超时判断用接收时刻`。
一次提交只做一件事；“搬代码”和“改行为”分两次提交，审查的人才看得清。

### 4.5 PR 写什么

PR 描述里写：

1. **做了什么、为什么**：一两句话，关联的 Issue 编号。
2. **改了哪些文件**：一个文件一行。
3. **验证到哪一级**：本地编译 0 警告、电脑测试通过数；上过板、台架的写日期、接线和看到的现象（第 5 节）。
4. **没验证的部分**：直接写出来，不要省略。

### 4.6 审查与合并

- 至少一位其他队员审查后才合并；审查按 `docs/CODING_STANDARD.md` 第 18 节的清单逐条看。
- 涉及电机、CAN、遥控、安全门的改动，审查人要确认有台架记录，或者明确写了“未上台架”。
- 发现冲突：在自己的分支上 `git pull origin main` 合并、解决冲突、重新测试，再推送。不要用强制推送覆盖别人的提交。

## 5. 验证层级与上车流程

每个结论都要说清验证到了哪一级，**低一级的通过不代表高一级通过**：

| 层级 | 含义 | 证据 |
| --- | --- | --- |
| 编译 | 固件编译 0 警告 | 构建输出 |
| 电脑测试 | 单元测试通过 | `ctest` 结果 |
| 上板 | 烧进 DM-MC02，不接电机或手扶电机，看 RTT / Ozone | RTT 日志、截图 |
| 台架 | 电机固定、不带负载，实际转动 | 日志、曲线、停机时间等实测值 |
| 整车 | 装在车上运行 | 场地记录 |

- 需要硬件确认的项目先记进 `docs/VERIFICATION_TODO.md`，攒起来集中上板；通过后把状态改为“通过（日期）”并附现象。
- 一台车第一次上电：先不接电机看 RTT 正常 → 接电机只看反馈 → 架空车轮试转 → 落地。每一步正常了再进行下一步。
- “急停多少毫秒停下”这类结论只能来自台架实测，不能靠读代码得出。

## 6. 遇到 bug

### 6.1 报告

在 Gitee 上开 Issue，标题用一句话说清现象，正文按下面的格式写：

```markdown
**现象**：看到了什么（贴 RTT 日志、截图；写清期望是什么）
**环境**：提交号（git log -1 --oneline）、板子、接了哪些设备、怎么接线
**复现步骤**：1. … 2. … 能否稳定复现？
**已经试过**：做过哪些尝试、猜测的原因
**影响**：会不会伤人、坏硬件？不修会影响什么？
```

修好后在 Issue 里写清原因和修复的提交号；能在电脑上复现的 bug，同时加一个单元测试防止它再出现（`CODING_STANDARD.md` 第 15 节）。

### 6.2 定位的基本步骤

1. **先缩小范围**：能不能在电脑测试里复现？能就在电脑上调，比上板快得多。
2. **排除硬件问题**：怀疑板子坏了，先烧 COD_UniCFramework 的对照固件比较（`docs/DEV_ENVIRONMENT.md` 开头的说明）。
3. **程序卡死或复位**：在 Ozone 里 Halt，看 Call Stack 停在哪。停在 `HardFault_Handler` 时，看寄存器和故障前的调用栈；
   常见原因是空指针、数组越界、栈溢出。停在初始化失败的死循环时，代码注释写明了原因（如 `01_applic/system/app_main.c`）。
4. **数据不对**：在 Ozone 的 Watched Data 里看原始值和换算后的值，先确认单位、方向，再怀疑算法。
5. **偶发问题**：记下出现前后的 RTT 日志和时间；看是不是多个任务同时读写一份数据，或者中断里做了耗时操作（`CODING_STANDARD.md` 第 11 节）。

## 7. 版本

- 每个阶段稳定后在 `main` 上打标签，格式 `v<赛季年份>.<序号>`，如 `v2027.1`，标签说明写清这个版本验证过什么。
- 赛前冻结：比赛前约定一个提交作为参赛版本，之后只合并修 bug 的 PR，每个都要整车验证。
- 第一次用本框架的车，从最近的标签开始，不要直接用 `main` 的最新提交。

## 8. 文档维护

- 改了代码的行为，同一个 PR 里更新对应的 `README.md` 和文档；文档里引用文件、函数，不抄具体数值（`CODING_STANDARD.md` 1.1a）。
- 和老模板 COD-H7-Template 做法不同的地方，记进 `docs/CHANGES_FROM_COD_H7_TEMPLATE.md`。
- 硬件实测的结果、踩过的坑，写进对应目录的 `README.md` 或 `docs/` 下的文档，写明日期和验证层级。
- 本文有过时或不清楚的地方，直接提 PR 改。
