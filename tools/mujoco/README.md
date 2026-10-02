# MuJoCo 单电机闭环

用 Windows Conda MuJoCo 调用本仓库原有 C PID，验证电脑侧控制闭环。
DLL 直接编译 `03_algorithm/control/pid.c` 和它依赖的 `filter/lpf.c`，没有 Python 版 PID。
这是独立的仿真工具，不参与固件构建，不需要连接主控板。

## 运行

在仓库根目录的 PowerShell 执行：

```powershell
# 自动构建 Windows DLL，并打开仿真窗口；演示结束后自动关闭。
.\tools\mujoco\run.ps1

# 不打开窗口，执行闭环并检查跟踪误差、力矩限幅、关闭后的零力矩。
.\tools\mujoco\run.ps1 -Check
```

脚本默认使用 `C:\Develop\Anaconda\envs\mujoco\python.exe` 和
`%LOCALAPPDATA%\Programs\CLion` 自带的 64 位 GCC、CMake、Ninja。
安装位置不同时，传 `-PythonExe` 和 `-ClionDir`；不用激活 Conda，也不用修改系统 PATH。
Windows DLL 单独输出到 `build/mujoco-windows/`，原来的 WSL 构建目录继续用于固件和主机测试。
这里使用的是 **CLion 的 Windows CMake**，不是 STM32CubeCLT 的 CMake。

已经构建 DLL 后，也可以只运行 Python：

```powershell
& 'C:\Develop\Anaconda\envs\mujoco\python.exe' -B .\tools\mujoco\single_motor.py
```

## 看什么、改哪里

默认过程：关闭 → 正转 → 反转 → 关闭并滑行。红色指针显示转轴的方向。
控制周期、每段速度和演示 PID 参数只在 `single_motor.py` 中定义。
每周期读取 `qvel`（输出轴 rad/s），调用 DLL 算出力矩，再写入 `ctrl`，推进一次物理仿真。
`single_motor.xml` 的转轴和执行器均按输出轴建模，`gear=1`，`ctrl` 的单位为 N·m。
图形刷新低于控制频率；仿真控制每步固定推进，不以 Windows 的休眠精度计算 dt。

完整过程写入 `build/mujoco-windows/single_motor.csv`，列为：
时间、目标速度、施加该周期力矩前的实测速度、该周期力矩、使能状态。
`-Check` 在正转和反转阶段最后一段检查最大误差，并检查全部周期的有限数值与限幅。
窗口提前关闭时保留已运行的 CSV；此时不代表完整演示通过。

## 范围与下一步

模型的惯量、阻尼和 PID 参数仅供接口验证，未用实车数据辨识；不是 M3508 的真实模型。
只接了 PID，尚未接 `chassis_step()`、电机协议、看门狗、安全门、遥控或 IMU。
这里的使能开关只是演示控制；关闭后是零力矩滑行，不保证某个时间内转轴停下。
不能据此推断电调、CAN、DMA 或实车停机行为。

闭环通过后，下一步才是补底盘模型和仿真电机 / 时间接口；该阶段要沿用现有电机发送出口与安全门。

参考：[MuJoCo Python 接口](https://mujoco.readthedocs.io/en/latest/python.html)、
[模型与执行器](https://mujoco.readthedocs.io/en/latest/XMLreference.html)。
当前使用电脑已有 MuJoCo 包，不复制或安装第三方源码。
