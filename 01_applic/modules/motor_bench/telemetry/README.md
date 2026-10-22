# USB 记录与应用参数协议快照

来源：[系统辨识插件 72b97c3](https://gitee.com/Xalve/system_identification/commit/72b97c37f5941d7dc644f8d2b73831d0948d0d20)，`firmware/sysid/sysid.c` 和 `sysid.h`。
版权：COD RoboMaster team and contributors；MIT，见 LICENSE。未复制达妙 SDK、手册或实验日志。
普通构建保持原插件行为；以下为上述提交的原始源文件 SHA-256（不是本地排版后文件的哈希）：

- sysid/sysid.c: cad0b8d7bbdc3fad3a52b7d90b1243e6f8023c3fcd98a93d39b48ec661105a0c
- sysid/sysid.h: 6ded2d1044eddfd374d3aac97741afd27aeca0d3035d27c368786af2010270c9

2026-10-22 本地差异：仅 `RM_MOTOR_BENCH_TIMING` 诊断构建通过条件宏记录取数据 / 封帧 / CRC 的 BEGIN/END。
普通构建宏为空且参数不求值；未改协议、CRC 算法、激励或队列逻辑。诊断使用平台 RAM 接口，不引入通用回调。
诊断 / 普通实际插件的通道表和记录帧逐字节比较由 `tools/check_timing.py` 执行；新增测点未上板，耗时及扰动未测。

CMake 保留 `sysid/sysid.h` 头文件路径；控制任务生产日志，通信任务是唯一发送方，USB 忙时不阻塞控制。
单电机 axes=NULL / axis_count=0，不接受通用激励；可选 command 回调交接 MB01 参数请求，`sysid_reply` 排队实际值回执。
回执与记录共用发送缓冲生命周期和重试出口。应用命令的停止条件、限值与撤销优先级由 `motor_bench_link` 执行。
升级时同步这对源文件、保留诊断条件宏，并重跑协议、主机、固件构建和台架接收检查。
