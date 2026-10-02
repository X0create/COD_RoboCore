# MuJoCo：27 哨兵半舵半全向底盘

默认入口为用户确认的**对角线半舵半全向**：两个舵轮、两个全向轮，四个驱动电机加两个转向电机。
默认左前 / 右后是橙色舵轮，左后 / 右前是蓝色全向轮。车架上的红箭头指向车头。
模型是简化几何；尺寸、质量、滚子与转向 PID 都是仿真占位值，不是 27 哨兵实车 CAD 或辨识结果。

## 启动与操纵

**在资源管理器双击本目录的 `run.cmd`。** 默认先自动演示前进、横移、旋转、组合运动，再关闭出力。
演示结束后窗口继续保留；关闭 MuJoCo 窗口才会结束脚本，终端按任意键关闭。
`.ps1` 需要在 PowerShell 执行，双击可能打开编辑器。启动器只给当前进程设置执行策略，不改系统配置。

窗口中的键盘控制（按一次改变指令，不需要一直按住；第一次使用键盘后切换为手动）：

| 按键 | 动作 |
| --- | --- |
| Enter | 解锁；自动给原有安全门提供“下 → 中”的虚拟拨杆边沿 |
| W / S | 前进 / 后退 |
| A / D | 左移 / 右移 |
| Q / E | 逆时针 / 顺时针旋转 |
| X | 清零目标，仍由闭环减速 |
| 空格 | 全车停，发送出口给全部虚拟电机零力矩；之后需 Enter 重新解锁 |

WASD 与 QE 可以组合；切换手动后先按 Enter 再操作。零力矩后车体可以继续滑行。

在仓库根目录也可以执行：

```powershell
.\tools\mujoco\run.ps1
# 无窗口检查：正常运动、电机离线、遥控丢失、恢复后不能自行解锁、显式重新解锁。
.\tools\mujoco\run.ps1 -Check
# 切换为左后 / 右前舵轮。
.\tools\mujoco\run.ps1 -Diagonal lb_rf
# 无窗口渲染一张模型预览，不打开 GUI。
.\tools\mujoco\run.ps1 -Check -Snapshot build/mujoco-windows/sentry_preview.png
# 保留原来的单电机 PID 示例，作为单独的诊断入口。
.\tools\mujoco\run.ps1 -Demo SingleMotor -Check
```

`run.cmd` 支持相同参数，例如 `run.cmd -Check`。脚本默认使用
`C:\Develop\Anaconda\envs\mujoco\python.exe` 与 `%LOCALAPPDATA%\Programs\CLion` 的
Windows 64 位 GCC / CMake / Ninja；可用 `-PythonExe`、`-ClionDir` 改路径。
不需要安装 WSL MuJoCo，Windows DLL 不能换成 WSL `.so`。

## 实际调用的代码

```text
MuJoCo 关节角度 / 速度
  → chassis_bridge.c 编码虚拟 DJI 反馈
  → 原 motor_receive / watchdog / motor_read_feedback
  → 原 safety_gate_update + chassis_step（斜坡、half_steer、PID）
  → 原 motor_group_apply_stop_all / motor_group_send
  → sim_platform.c 接收最终 CAN 电流帧、还原力矩
  → MuJoCo 电机执行器（gear=1），推进物理仿真
```

Python 不重写底盘运动学或 PID；控制周期只在 C 桥接层定义，Python 用同一周期推进物理仿真。
整个 C 控制循环只在 Python 主线程执行；键盘回调只改虚拟操纵输入。
不会启动 FreeRTOS 或连接实际 CAN，也不会更改当前固件仍为全向轮的配置。

四轮顺序为左前、左后、右后、右前，与原 `half_steer.h` 一致。
全向轮沿中心切向安装，每个具有自由滚子关节，横移来自接触动力学；脚本不写车体位姿或速度来模拟运动。
所有执行器输入均为输出轴 N·m。虚拟电调统一用 M3508 协议、固定方向和虚拟 ID；
转向关节初始朝前，仅在模拟中定义零位，不决定实车转向电机型号或零点实现。

## 参数与输出

- `sentry_model.py` 的 `SentrySpec`：模型尺寸、车架质量、滚子数量；尺寸直接传给 C，不再抄一份。
- `chassis_bridge.c`：仿真专用转向 PID；驱动 PID 和加速度直接取现有 `01_applic/config/params.h`。
- `sentry_chassis.py`：自动运动序列、虚拟故障检查、键盘目标速度。
- `build/mujoco-windows/sentry_chassis.xml`：每次启动从配置生成的 MJCF。
- `build/mujoco-windows/sentry_chassis.csv`：车体实际速度、斜坡目标、最终力矩、轮速 / 朝向目标、模式和在线状态。
  所有反馈与本周期输出按同一时刻记录；交互时保留最近一段演示时长，退出窗口后保存。

图形刷新频率低于控制频率；固定的是仿真步长，不保证 Windows 的实时调度精度。
车体速度用 MuJoCo `mjOBJ_XBODY` 读取车架坐标；`mjOBJ_BODY` 的局部惯性主轴可能与车架轴不同。

## 验证范围

`-Check` 用物理仿真的车体速度检查前进 / 横移 / 旋转 / 组合运动，并注入反馈中断与虚拟遥控丢失。
安全门、看门狗和电机最终发送出口是真实 C 实现；虚拟遥控在线 / IMU 就绪标志由测试提供，
没有模拟 DR16 串口超时、BMI088 / INS、DMA、CAN 实际时序、驱动器或电机电气动态。
主机仿真通过不代表实车停机或调参通过。

2026-10-01：两种舵轮对角线的完整无窗口检查通过，单电机回归通过，离屏预览已渲染检查。
原驱动轮参数在占位模型中存在转弯误差和接触滑移；换实车尺寸 / 质量后需要重新评估。
交互键盘和真实硬件仍待验证。下一步用实车尺寸 / CAD、质量、轮半径和转向信息替换占位值。

参考：[MuJoCo Python 接口](https://mujoco.readthedocs.io/en/latest/python.html)、
[模型与执行器](https://mujoco.readthedocs.io/en/latest/XMLreference.html)。
使用电脑已有 MuJoCo 包，不复制第三方源码。
