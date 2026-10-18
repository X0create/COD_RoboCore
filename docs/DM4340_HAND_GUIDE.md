# DM4340 单电机手拖测试（MC02/H723）

2026-10-18。状态：软件与离线验证已完成，尚未烧录、上板或接电机。验证记录见本文末尾。

## 这次能验证什么

目标是比较**上电、MIT Kp=Kd=0 的零力矩请求**与**低比例运动摩擦助力**，判断转动过程中人需要的外力是否下降。
控制在 MC02 上以 1 kHz 运行；电脑走 MC02 USB CDC，只记录。既有 USB2CANFD 采集链有约 110 ms 的批量反馈延迟，不能用于这次实时方向判断。

本次助力请求为：

    tau = fraction * Fc * sign(w) * clamp((abs(w)-deadband)/(full-deadband), 0, 1)

其中 w 是 MCU 收到的实测速度，没有位置目标、速度目标、积分、起步助推、惯量补偿或重力补偿；停止/反向不保留上一方向助力。
具体数值只在 config/hand_guide_robot.h 定义。Fc 来自系统辨识仓库 docs/en/results/dm4340_model_card.json（2026-10-04），是**驱动反馈等效力矩**，尚未经外部力矩传感器标定。
模型是裸电机、24 V、24–42 °C、运动工况的数据；原辨识使用驱动输出等级 2，之后另有输出等级 4 的配置，不能将后者当成已验证工况。首测应核对并使用模型对应配置；更换配置要重新验证。
静摩擦、过零、换向及手与电机耦合尚未验证；低速的平滑衰减是本次待测试的控制选择。
首版只给 Fc 的 25%，请求峰值约 0.093 N·m；这不保证外力减少 25%，也不保证松手后一定停下。

**它主要改善已经转起来后的阻力。零速度时没有方向信息，不会主动帮你破静摩擦；起步手感不一定明显变轻。**

沿用电机层 MIT 截尾编码：T_MAX=40 时步长约 0.0195 N·m，零请求也会解码为约 -0.00977 N·m；指令不是精确的实际力矩。正反方向的小助力可能不对称，日志 assist.command 是编码前请求。
本次没有修改已验证的通用电机编解码。

## 准备与接线（用户操作）

- 固定裸电机本体，输出端用短、轻、牢固的测试柄；先脱离云台、重力负载和其他机构。不要在带负载 pitch 上套用本测试。
- MC02 的 **FDCAN3/CAN3** 接这一台 DM4340；只挂 CAN FD 设备。现有配置为仲裁 1 Mbit/s、数据 5 Mbit/s、FD+BRS。
- 用达妙上位机核对实物 CAN ID=1、Master ID=0x11、P_MAX=pi、V_MAX=8、T_MAX=40；以配置文件为最终核对清单。任一不符，先改到一致再试，不能靠手感判断。
- USB 连 **MC02 的 USB 口**，电脑识别为 COM 口；不走 USB2CANFD 的反馈闭环。
- 此实现需要 **DR16 接收机和遥控器**，接 MC02 的 DBUS/UART5。先不使能，在调试器看 hand_rc，确认 sw[1] 对应哪个实体拨杆；默认通常是右拨杆，必须现场核对。
- 必须先由用户用达妙上位机启用驱动器通信超时保护，并在手离开输出轴时验证停发会撤力。既有记录中 TIMEOUT=0；本固件不会替你更改或确认这个驱动参数。
- 电机电源应能立即切断。失能是撤去驱动力，不是机械刹车；外部负载仍可能使轴转动。

## 构建与烧录（烧录由用户操作）

独立分支 bench/dm4340-handguide，独立工作目录 COD_RoboCore_dm4340，原工作目录的用户修改不参与本构建。
在 WSL、该工作目录运行：

    cmake --preset h723-handguide
    cmake --build --preset h723-handguide

产物：build/Linux/h723-handguide/COD_RoboCore.elf。使用 Ozone **明确选择此 ELF**；旧 Ozone 工程仍可能指向底盘 ELF，不能直接按旧路径烧录。
本台架暂只提供这个 CMake 预设；Keil 目标仍是底盘，不用它构建/烧录手拖测试。
启动只创建一个达妙电机、DR16、控制/接收/检测任务；无底盘电机、超级电容输出或比赛固件的看门狗重启自动解锁。

## 最小试验顺序

1. 手离开输出轴，上电；拨杆一直在中/上也应保持失能。若不符合，切电排查。
2. 实测电机温度进入配置允许范围后，在轴静止时把拨杆拨下，再拨中：中档是零力矩基线。先确认没有自行持续转动。
3. 手离开输出轴，先完成中→下撤力、遥控丢失、驱动反馈丢失、驱动通信超时四项台架检查。断线/停机时间必须现场测，不能拿软件阈值当实测值。
4. 保持中档，轻推正、反方向，记录基线手感和外力；先用已测运动速度范围内的缓慢转动。
5. 让轴停下，确认驱动已使能，再中→上：上档为小助力。先手离开检查不自行启动，再轻推；松手若持续加速/持续旋转、来回抖动或突然助推，立刻下档并切电，不能继续加大补偿。
6. 上→中可立即撤去助力、保持使能零力矩基线；下档请求失能。每次切上档前先停住，否则锁定。
7. 分别做正转、反转、慢速过零、停手，重复基线→助力→基线，尽量保持同一温度、速度和测试柄。

**Ctrl+C、关记录窗口或拔掉电脑 USB 只停止记录，不是电机急停。用已核对的遥控停机档，必要时切电。**
任何遥控失联、电机离线、驱动报错、超速、温度越界、控制超期或 CAN 发送失败都会将助力请求清零并锁定，输出出口请求失能；信号恢复不会自动重启。既有设备层在反馈离线时发送零请求 MIT 轮询，收到在线反馈后才发失能命令；不能把模式 OFF 当成已确认物理失能。总线完全断开时仍必须依赖已经实测的驱动器通信超时。先排除故障、停轴，再下→中重新准备。
温度未达到模型范围时保持失能；不在温度保护未通过时放宽限值来强行试验。

## 记录与评价

从 System_Identification/System_Identification 目录，用已有 Python 环境运行（COM5 改为 MC02 实际端口）：

    python tools/sysid_log/sysid_log.py --port COM5 --out data/dm4340_handguide/runs --label ab_test --subject "MC02 DM4340 hand guide" --seconds 60 --vbus 24 --firmware-repo ../../COD_RoboCore_dm4340

不加 --axis。记录器会输出本次实际运行目录；固件运行日志默认 200 Hz，包含角度、速度、反馈等效力矩、助力请求、温度、周期/执行时间、模式、故障、遥控与电机在线状态、发送失败计数。
模式 hand.mode：0 停机、1 零力矩基线、2 助力。hand.fault 枚举在 hand_guide.h，hand_guide 对象可直接在 Ozone 查看。
记录器结束时会做解码摘要；如需 CSV，用 tools/analysis/decode_sysid_stream.py 本次目录/stream.bin --csv 本次目录/decoded.csv。

定量测试用拉力计在固定半径 r、切向方向拉动，tau_hand=F*r。正反分别记录起动峰值、相同速度下的稳态外力，并观察放手后的速度；不要把驱动反馈力矩当成人施加的外力。
建议表格列：模式、方向、温度、速度范围、力臂、起动峰值力、稳态力、重复次数、松手现象。
稳态外力的下降应超过重复测量波动/拉力计分辨率，且不能以自转、振荡、反向卡顿或停机失效换取“更轻”。没有拉力计时只能做定性试验。

本测试只能评价低速手拖中的运动摩擦助力，不足以验收重力、惯量前馈、轨迹跟踪或 PID 整定。确认首版稳定后再根据外力结果逐步调整比例；此版没有运行中增益调节功能。

## 本批文件清单

| 文件 | 新建/修改与职责 |
| --- | --- |
| 01_applic/config/hand_guide_robot.h | 新建；唯一的电机/补偿参数、对象声明 |
| 01_applic/modules/hand_guide/hand_guide.h | 新建；模式、故障与接口 |
| 01_applic/modules/hand_guide/hand_guide.c | 新建；hand_guide_update / hand_guide_apply |
| 01_applic/modules/hand_guide/README.md | 新建；模块索引 |
| 01_applic/modules/hand_guide/telemetry/sysid/sysid.c | 新建；本队记录协议源码快照 |
| 01_applic/modules/hand_guide/telemetry/sysid/sysid.h | 新建；记录协议接口快照 |
| 01_applic/modules/hand_guide/telemetry/LICENSE | 新建；源项目 MIT 许可 |
| 01_applic/modules/hand_guide/telemetry/README.md | 新建；来源提交与原文 SHA-256 |
| 01_applic/system/hand_guide_main.c | 新建；独立 app_main / startup_task |
| 01_applic/tasks/hand_guide_task.c | 新建；1 kHz 控制、200 Hz 记录 |
| 01_applic/tasks/hand_guide_rx_task.c | 新建；仅 CAN3、DR16、USB 接收 |
| 01_applic/tasks/tasks.h | 修改；手拖任务入口声明 |
| 01_applic/CMakeLists.txt | 修改；RM_HAND_GUIDE 选择独立入口 |
| 01_applic/README.md | 修改；独立应用索引 |
| CMakePresets.json | 修改；h723-handguide 预设 |
| tests/host/01_applic/test_hand_guide.c | 新建；状态/补偿/故障/真实 MIT 编码测试 |
| tests/host/CMakeLists.txt | 修改；注册 test_hand_guide |
| docs/DM4340_HAND_GUIDE.md | 新建；本文 |
| docs/CHANGES_FROM_COD_H7_TEMPLATE.md | 修改；记录专用试验与旧模板差异 |

根工作区 STATE.md 保存阶段状态；不在 Git 仓库。build/ 下离线检查脚本与产物忽略，不推送。

## 验证记录

- WSL GCC 主机测试 37/37 程序通过；其中手拖 7 个测试覆盖启动锁定、基线、换向/零速、8 类故障及重新准备、使能等待、配置拒绝、实际 MIT 编码、实际反馈超时及恢复不重启。
- h723-handguide、原 h723-debug、c-debug 均编译成功，构建日志无警告；专用 ELF 的 DTCMRAM 74224 B / 128 KiB，FLASH 120636 B / 1 MiB。
- clang-format 18.1.3 检查 178 文件通过；原 Keil 清单检查通过，未修改生成代码或 Keil 目标。
- 使用复制进来的 sysid 源码执行原项目的 4 个 C 协议测试程序，均通过；仅调整源码排版，没有修改协议行为。
- 实际编译的 C 补偿输出与独立 Python / MATLAB 公式对照 2999 个速度点，最大差约 7.18e-9 N·m（数值验证，不是物理力矩标定）。
- 原工作目录的 motor.c、Keil 文件和未跟踪数据未改；本批无达妙 SDK、手册、原始日志、大文件或个人绝对路径。

未执行烧录、上板、接电机、驱动参数修改或手拖测试。实际控制周期、反馈时延、撤力时间、方向、温度/速度边界以及人体接触行为仍待用户台架验证。
