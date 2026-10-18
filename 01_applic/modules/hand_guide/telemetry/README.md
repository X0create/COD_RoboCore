# USB 记录协议快照

来源：https://gitee.com/Xalve/system_identification ，提交 2d99465，firmware/sysid/sysid.c 和 sysid.h。
版权：COD RoboMaster team and contributors；MIT，见 LICENSE。未复制达妙 SDK、手册或实验日志。
拷贝后只按 RoboCore 的 clang-format 排版，无行为修改；以下校验值对应来源原文：

- sysid/sysid.c: SHA-256 fd66db75d7157d102d2863452eec1b1ffd83c5df6c883fb9416d2c2b40d89f1b
- sysid/sysid.h: SHA-256 6049f57fb9a1419ae1a20f7266f4a41a43fea19c1af11b5c28327e8afea2039d

CMake 的头文件目录保留 sysid/sysid.h。日志环形队列由控制任务生产、接收任务发送；USB 忙时不阻塞控制。
MC02 接入时 axes=NULL/axis_count=0；电脑请求只用于通道表，不接受运动激励。
升级时从上述源仓库同步此对文件，并重跑固件编译、协议测试和台架接收检查。
