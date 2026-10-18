# 手拖测试模块

仅由独立的 h723-handguide 预设选用。hand_guide.c/h 保存模式、故障锁定、运动摩擦助力及唯一电机输出出口；不生成轨迹。
配置在 ../../config/hand_guide_robot.h，试验步骤与修改清单见 ../../../docs/DM4340_HAND_GUIDE.md（从仓库根目录打开）。
telemetry/ 是本队系统辨识插件的 MIT 源码快照，只启用记录，轴表为空；来源和校验见其 README。
