# 代码阅读指南

给第一次看这套代码的人：按什么顺序读、读到哪里可以停、怎么用一份数据把整套代码串起来。
不需要从头翻到尾，照下面四步走，大约一个半小时能看懂整体。

配套文档：`LOGIC_PSEUDOCODE.md`（中文伪代码，每段标了文件和函数）、`CALL_FLOW.md`（每个任务的函数调用树、新旧模板对照）。

## 第一步：建立整体印象（10 分钟）

整个程序就是 **6 个任务同时在跑**，每个任务都是一个死循环：

| 任务 | 多久跑一次 | 干什么 |
| --- | --- | --- |
| `ins_task` | 1 ms | 读 IMU，算姿态 |
| `comm_rx_task` | 收到数据就跑 | 收遥控器和电机反馈 |
| `control_task` | 1 ms | **核心**：读输入 → 判断能不能动 → 算底盘 → 发给电机 |
| `detect_task` | 10 ms | 报告哪个设备上线、掉线 |
| `indicator_task` | 25 ms | 状态灯、蜂鸣器、电池电压 |
| `log_task` | 1 s | 通过 RTT 打印状态 |

上电时先由 `01_applic/system/app_main.c` 初始化全部对象，再按任务表把这 6 个任务创建出来。

先看 `LOGIC_PSEUDOCODE.md` 第 0 节“一张图”：它用中文把上面这些讲了一遍，不涉及 C 语法。

## 第二步：按顺序读这几个文件（1 小时）

| 顺序 | 文件 | 读它是为了 |
| --- | --- | --- |
| 1 | `01_applic/config/robot.h` | 这台车有哪些电机、什么参数、有哪些对象 |
| 2 | `01_applic/config/robot.c` | 对象怎么初始化（`robot_init()`），最下面是任务表：6 个任务的优先级、栈大小 |
| 3 | `01_applic/tasks/control_task.c` | **最重要**，约 60 行。循环里写着 1、2、3、4 四步，从上往下读 |
| 4 | `01_applic/tasks/comm_rx_task.c` | 遥控器和电机的数据从哪来 |
| 5 | `01_applic/tasks/` 里其余的任务 | 每个都很短；入口函数都声明在 `tasks.h` |

读到这里，你已经知道这台车每 1 ms 在做什么。**下面几层先不用看。**

## 第三步：需要时再往下钻

只顺着 `control_task.c` 循环里那四步往下点：

| 步骤 | 往下看 | 讲的是什么 |
| --- | --- | --- |
| 1. 读输入 | `02_devices/remote/dr16.c` 的 `dr16_read()`、`01_applic/modules/ins/ins.c` 的 `ins_read()` | 遥控、姿态怎么读；多久没更新算掉线 |
| 2. 安全门 | `01_applic/system/safety_gate.c` | 什么情况下全车停：急停、遥控丢失、未解锁、IMU 未就绪 |
| 3. 底盘 | `01_applic/modules/chassis/chassis.c` 的 `chassis_step()` → `03_algorithm/kinematics/omni.c`、`03_algorithm/control/pid.c` | 速度怎么分到四个轮子，PID 怎么算出力矩 |
| 4. 发送 | `02_devices/motor/motor_group.c` 的 `motor_group_send()` | 每个电机最终发什么（停机 > 没写指令 > 离线 > 力矩），怎么打包成 CAN 帧 |

刚开始可以完全跳过：`05_platform/`、`06_boards/`、`04_core/log/segger_rtt/`、`tests/host/unity/`。
它们是和芯片、第三方库打交道的底层，平时不用改。

## 第四步：跟着一份数据走一遍

把“推一下遥控器摇杆，轮子为什么会转”从头走到尾。每一行都打开对应的文件找到那个函数：

```
遥控器摇杆
 → 串口 UART5 收到字节，中断唤醒 comm_rx_task   05_platform/uart/uart_stm32h7.c：HAL_UARTEx_RxEventCallback()
 → comm_rx_task 取出字节                        01_applic/tasks/comm_rx_task.c：uart_read()
 → 拼成 18 字节一帧，校验后存进 dr16             02_devices/remote/dr16.c：dr16_on_bytes() → dr16_decode()
 → control_task 读出最新一帧                    01_applic/tasks/control_task.c：dr16_read()
 → 安全门判断能不能动                           01_applic/system/safety_gate.c：safety_gate_update()
 → 摇杆换算成底盘速度                           01_applic/tasks/control_task.c：chassis_cmd_from_rc()
 → 速度分到四个轮子，PID 算出力矩，写进电机      01_applic/modules/chassis/chassis.c：chassis_step()
                                                  → omni_inverse()、pid_step()、motor_set_torque()
 → 全部电机的指令打包成 CAN 帧发出去             02_devices/motor/motor_group.c：motor_group_send() → can_send()
```

再走一遍反方向的数据，就把接收这一侧也看懂了：

```
电机反馈帧
 → FDCAN 中断把帧放进环形缓冲                   05_platform/can/can_stm32h7.c
 → comm_rx_task 逐帧取出，交给电机组             01_applic/tasks/comm_rx_task.c：can_read() → motor_group_receive()
 → 总线和 ID 对上的电机解码、保存、喂看门狗      02_devices/motor/motor.c：motor_receive()
 → 底盘读实测转速                               01_applic/modules/chassis/chassis.c：motor_read_feedback()
```

`CALL_FLOW.md` 第 5 节把每个任务的调用链都按这种格式画好了。

## 读代码的技巧

- **先看 `.h` 文件开头的注释**：每个模块的头文件都写了“这个模块干什么、怎么用、谁来调用”，比直接读 `.c` 快。
- **数据由谁写、谁读**：`robot.h` 开头的注释列出了每个共享对象由哪个任务写、哪个任务读；完整的表在 `LOGIC_PSEUDOCODE.md` 第 10 节。
- **CLion 快捷键**：Ctrl+B 跳到定义，Alt+F7 查谁调用了它，Ctrl+Alt+H 看完整的调用层级。
- **看真实数值**：烧录后在 Ozone 的 Watched Data 里加 `dr16`、`chassis.target`，推摇杆时看数值变化，比干读代码直观
  （`DEV_ENVIRONMENT.md` 第 9 节）。
- **单位**：代码里一律是国际单位，角度 rad、速度 rad/s、力矩 N·m；变量名带单位后缀（`_rad_s`、`_nm`、`_ms`）。
- **为什么这样设计**：查 `ARCHITECTURE.md` 的 ADR 表，每个决定都写了原因；和老模板不同的地方在 `CHANGES_FROM_COD_H7_TEMPLATE.md`。

## 熟悉老模板 COD-H7-Template 的话

| 老模板 | 新模板 |
| --- | --- |
| `Task/Control_Task.c` + `CAN_Task.c` | `01_applic/tasks/control_task.c` |
| `INS_Task.c` | `01_applic/tasks/ins_task.c` + `01_applic/modules/ins/ins.c` |
| `bsp_can.c` / `bsp_uart.c` 里的接收回调 | `01_applic/tasks/comm_rx_task.c`（中断只收数据，解析在任务里） |
| `Config.h` | `01_applic/config/robot.h` |
| 各任务直接用的全局变量（`remote_ctrl`、`Chassis_Motor[]` …） | `robot.h` 里声明的对象（`dr16`、`wheel_motor[]` …） |
| `freertos.c` 里的任务列表 | `01_applic/config/robot.c` 的 `task_table[]` |
| `BSP/` | `05_platform/` |
| `Components/Device`、`Algorithm` | `02_devices/`、`03_algorithm/` |

完整对照见 `CALL_FLOW.md` 第 1、2 节。

## 读完之后

想动手改代码，接着看 `CONTRIBUTING.md` 第 2.3 节“我想改 X，去哪里”和第 3 节“常见任务的做法”。
