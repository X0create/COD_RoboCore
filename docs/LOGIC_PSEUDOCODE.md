# 中文逻辑伪代码（步兵固件）

更新时间：2026-10-01。用中文把整个固件“从上电到发电机指令”的逻辑写一遍，**只讲做什么，不讲 C 语法**。
每一段后面的 `→ 文件:函数` 是对应的真实代码，在 IDE 里打开后用“跳到定义 / 查找用法”继续看细节（见 `docs/CALL_FLOW.md`）。
伪代码与代码不一致时以代码为准，并更新本文。

---

## 0. 一张图

```
上电 → app_main（初始化、建任务）→ 调度器启动 → startup_task（允许解锁，删除自己）
                                                   ↓
   ┌──────────── 6 个任务同时运行，各自按周期 ────────────┐
   │ ins_task      1 ms   读 IMU → 算姿态 → 发布 imu_state   │
   │ comm_rx_task  有数据  解析遥控、电机反馈                 │
   │ control_task  1 ms   读输入 → 安全门 → 底盘 → 发电机指令 │
   │ detect_task   10 ms  打印设备上线 / 离线                │
   │ indicator_task 25 ms 灯、蜂鸣器、低电量                 │
   │ log_task      1 s    打印状态                          │
   └──────────────────────────────────────────────────────┘
中断：只把数据收下来、叫醒 comm_rx_task，不做解析
```

---

## 1. 上电

```
main()（CubeMX 生成）
    配置 MPU、开 Cache、初始化时钟和各外设
    调用 MX_FREERTOS_Init()
        调用 app_main()                                    → 01_applic/system/app_main.c:app_main
            初始化 DWT 计时器；失败就停住
            初始化 RTT 日志
            robot_init()：初始化本兵种的全部对象              → 01_applic/robots/infantry/infantry_robot.c:robot_init
                初始化 DR16 遥控（结果发布到 rc_state）
                对 4 个轮子电机：检查配置、查 ID 冲突、加入电机组
                初始化底盘（轮组类型、尺寸、PID 来自 infantry_config.h）
                初始化 ins（安装方向来自 infantry_config.h，结果发布到 imu_state）
                初始化安全门（解锁拨杆 = 右拨杆），模式 = Init
                任何一步失败 → 停住，不建任何任务（电机不会收到指令）
            按任务表 robot_tasks[] 逐个创建 6 个任务；有一个失败就停住
    启动调度器

startup_task（CubeMX 建的，优先级最高，第一个运行）     → 01_applic/system/app_main.c:startup_task
    安全门：系统就绪（之后才允许解锁）
    打印 "startup done"
    删除自己
```

---

## 2. 中断（只收数据）

```
串口空闲中断（DR16 一帧收完）                          → 05_platform/uart/uart_stm32h7.c:HAL_UARTEx_RxEventCallback
    数据已经由 DMA 写进循环缓冲，这里什么都不拷
    叫醒 comm_rx_task                                     → 01_applic/system/comm_rx_common.c:notify_from_isr

CAN 接收中断（收到一帧）                                → 05_platform/can/can_stm32h7.c:HAL_FDCAN_RxFifo0Callback
    把这一帧放进 CAN 接收环形缓冲（满了就丢弃并计数）
    叫醒 comm_rx_task
```

---

## 3. comm_rx_task：解析遥控和电机反馈（收到数据就运行）

```
任务开始：
    打开每一路 CAN 的接收
    打开 UART5（DR16）的 DMA 接收

永远循环：                                              → 01_applic/robots/infantry/infantry_comm_rx_task.c:comm_rx_task_entry
    等待中断叫醒（最多等 10 ms，防止漏掉通知）

    对每一路 CAN：
        取出缓冲里的每一帧：
            依次问 4 个轮子电机“这帧是不是你的反馈”      → 02_devices/motor/motor.c:motor_receive
                总线对、反馈 ID 对（0x201–0x204）→ 是它的：
                    长度不是 8 → 丢弃
                    否则 解码出 角度、转速、力矩、温度（换成输出轴国际单位）
                         在临界区里 整份写进这个电机的反馈
                         记下“刚收到过数据”（在线检测用）
                    不再问后面的电机
    如果某路 CAN 进入 bus-off，并且距上次重启已过 100 ms → 重启它

    从 UART5 取出新收到的字节，交给 DR16 解析            → 02_devices/remote/dr16.c:dr16_on_bytes
        距上次收到数据超过 6 ms → 当作新的一帧开头
        每凑满 18 字节：
            检查摇杆值在 364–1684、拨杆值合法
            合法 → 减去中位 1024，发布到 rc_state，记下“刚收到过数据”
            不合法 → 坏帧计数 +1
```

---

## 4. ins_task：姿态解算（每 1 ms）

```
任务开始：                                              → 01_applic/tasks/ins_task.c:ins_task_entry
    初始化 BMI088；失败就每 1 s 重试一次
    打印“开始标定，保持静止”

永远循环（每 1 ms）：
    ins_step()：                                         → 01_applic/modules/ins/ins.c:ins_step
        1. 读 BMI088（SPI2）
           读失败，或加速度几乎为 0（坏帧）→ 关加热，失败计数 +1，这一周期不发布
        2. 加热：每 1280 ms 用芯片温度算一次 PID，目标 40 °C
        3. 如果还在上电标定阶段：
               攒满 2000 个陀螺样本（2 s）
               抖动小、均值也小 → 把均值当零偏，进入运行阶段
               否则 → 报告原因（在动 / 零偏太大），重新攒
               标定完成前不发布 imu_state（安全门因此一直全车停）
        4. 运行阶段：
               陀螺、加速度从芯片坐标转到机体坐标（安装方向）
               静止时每 1 s 修正一点航向零偏
               加速度二阶低通
               四元数 EKF（用实测的时间间隔）
               算出 yaw / pitch / roll、多圈 yaw
        5. 发布到 imu_state
    把返回的事件（读失败、标定完成、标定被拒）写进日志
```

---

## 5. control_task：控制周期（每 1 ms）

```
永远循环（每 1 ms）：                                   → 01_applic/robots/infantry/infantry_control_task.c:control_task_entry
    now = 当前时刻

    【第 1 步 读输入】
    rc  = 从 rc_state 整份拷贝；超过 200 ms 没更新 → 遥控丢失
    imu = 从 imu_state 整份拷贝；超过 20 ms 没更新 → IMU 未就绪

    【第 2 步 安全门】                                   → 01_applic/system/safety_gate.c:safety_gate_update
    （见第 6 节）得到 stop_all（这一周期是否全车停）

    【第 3 步 底盘】
    如果 stop_all：目标速度 = 0
    否则：目标速度 = 摇杆换算（ch[3] 前后、ch[2] 左右、ch[0] 旋转，满杆速度见 infantry_config.h）
    底盘计算（见第 7 节），解锁后 300 ms 内输出限幅从 0 逐渐升到 1 倍

    【第 4 步 发送】
    如果 stop_all：把每个电机改写成它的停机动作（轮子 = 零力矩）
    电机组发送（见第 8 节）

    睡到下一个 1 ms
```

---

## 6. 安全门：能不能动

```
safety_gate_update(遥控, IMU 是否就绪, now)：           → 01_applic/system/safety_gate.c
    输入可用 = 遥控在线 并且 IMU 就绪
    拨杆在下 = 输入可用 并且 右拨杆在“下”

    按当前模式：
        Init（启动未完成）：
            系统就绪了 → 进入 Safe，清除“看到过拨杆在下”
        Safe（等待解锁）：
            输入不可用 → 清除“看到过拨杆在下”（掉线期间的拨杆位置不算）
            拨杆在下   → 记下“看到过拨杆在下”
            看到过拨杆在下，现在又拨到了中或上 → 解锁：进入 Manual，记下解锁时刻
        Manual（允许动作）：
            输入不可用 或 拨杆在下 → 回到 Safe（之后必须重新拨一次才能解锁）

    stop_all = 模式不是 Manual
```

结论：急停（拨杆在下）、遥控丢失、IMU 未就绪、还没解锁，任何一种都会全车停；恢复以后也要重新拨一次才能动。

---

## 7. 底盘：一次计算

```
chassis_step(目标速度, stop_all, 输出比例)：            → 01_applic/modules/chassis/chassis.c:chassis_step
    【读实测】
    对 4 个轮子：拷贝电机反馈；20 ms 内收到过反馈才算在线
    全部在线 → 用运动学正解算出底盘当前速度；否则当前速度记为 0

    如果 stop_all：
        目标速度 = 当前实测速度（解锁时不会突然跳变）
        清掉所有 PID 积分
        不写任何电机指令
        结束

    【算目标】
    有电机离线 → 目标改为 0（机构停：车受控减速）
    斜坡：平移加速度不超过 max_accel、旋转加速度不超过 max_alpha
    运动学逆解：底盘速度 → 每个轮子的目标转速（全向轮 / 麦轮 / 舵轮）

    【算输出】
    对每个轮子：
        在线 → 速度环 PID（目标转速 vs 实测转速）→ 力矩，按“PID 限幅 × 输出比例”限幅 → 记下这个电机本周期的力矩指令
        离线 → 清积分，不写指令（发送时会发零力矩）
```

---

## 8. 电机组发送：最终发什么

```
motor_group_send()：                                     → 02_devices/motor/motor_group.c:motor_group_send
    1. 确定每个电机最终发什么（final_output），按优先级：
           有停机动作 → 执行停机动作（零力矩 / 阻尼 / 失能）
           本周期没写力矩指令 → 零力矩
           反馈离线（20 ms 没收到） → 零力矩
           否则 → 发写入的力矩
    2. 编码 + 发送：
           DJI 电调：同一总线、同一控制帧的 4 个电调拼成一帧（0x200），每个占 2 字节电流值 → 放进 CAN 发送队列
                     （全部失能时只发一次 0，之后停发）
           达妙电机：每台一帧；需要使能 / 失能 / 清错时先发命令，否则发 MIT 帧
    3. 清空本周期的指令（下个周期不写就发零力矩）
```

---

## 9. 其他任务

```
detect_task（每 10 ms）：                                 → 01_applic/tasks/detect_task.c
    开始时打印一次“本固件有哪些设备”
    之后：设备在线 / 离线状态变了 → 打印一行
    （只报告，不决定停车；停不停由读数据的一方按时间戳自己判断）

indicator_task（每 25 ms）：                              → 01_applic/tasks/indicator_task.c
    开始时：打开 ADC、蜂鸣器，放启动音
    每次：
        状态灯：每秒绿灯闪两下（一长一短）
        电池：读电压；低于 21.0 V 持续 1 s → 低电量（回到 21.5 V 以上解除）；低电量期间每 2 s 响一次
        读安全门模式：刚进入 Manual → 解锁音；刚离开 Manual → 上锁音
        推进蜂鸣器的音符

log_task（每 1 s）：                                      → 01_applic/robots/infantry/infantry_log_task.c
    打印：心跳计数和模式、遥控、4 个轮子（转速 / 目标 / 力矩 / 温度）、底盘目标、IMU 姿态和温度、电池电压
```

---

## 10. 共享数据：谁写、谁读

| 数据 | 谁写 | 谁读 | 怎么保证安全 |
| --- | --- | --- | --- |
| `rc_state`（遥控） | comm_rx_task（DR16 解析） | control_task、log_task | 临界区里整份拷贝，带写入时刻；读的一方判断 200 ms 内才算在线 |
| `imu_state`（姿态） | ins_task | control_task、log_task | 同上，20 ms 内才算就绪 |
| 电机反馈 | comm_rx_task（`motor_receive`） | control_task（底盘）、log_task | 临界区里整份拷贝；20 ms 内收到过才算在线 |
| 电机指令 | control_task（底盘） | control_task（`motor_group_send`） | 写和发在同一个任务里 |
| 安全门模式 | control_task | indicator_task、log_task | 单个值，读写是原子的；只用来提示和打印 |
| 电池电压 | indicator_task | log_task | 通过 `indicator_battery_v()` 读，只用来打印 |
