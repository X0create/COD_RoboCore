# tools/

已有：

- `mujoco/`：Windows Conda MuJoCo 27 哨兵半舵半全向底盘，复用现有 C 底盘 / 电机 / 安全门；双击 `mujoco/run.cmd`，检查用 `run.cmd -Check`，单电机入口用 `-Demo SingleMotor`，见其 README。
- `gen_readme_diagrams.py`：生成 README 里的结构图（`docs/images/*.svg`）。改图改这个脚本再运行，不要手改 SVG。
- `heater_model.py`：用上板数据（`docs/data/heater_2026-09-30.csv`）拟合 IMU 加热的热模型，并在模型上比较加热参数（ADR 0042）。

规划中的辅助脚本与配置，例如：

- `keil_sync.py`：CubeMX 重新生成 Keil 工程、或增删源文件后，把 `06_boards/dm_mc02_h723/MDK-ARM/dm_mc02.uvprojx` 整理回可编译本框架的样子；`--check` 只检查（`build/check.sh`、CI 会跑）；
- `check_deps.py`：检查 `#include` 是否符合分层依赖规则；
- `check_forbidden.py`：检查禁用的写法；
- `check_linked.py`：确认关键函数确实链接进了 ELF；
- VOFA+、Ozone 工程模板等调试配置。
