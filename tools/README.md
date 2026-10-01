# tools/

已有：

- `gen_readme_diagrams.py`：生成 README 里的结构图（`docs/images/*.svg`）。改图改这个脚本再运行，不要手改 SVG。
- `heater_model.py`：用上板数据（`docs/data/heater_2026-09-30.csv`）拟合 IMU 加热的热模型，并在模型上比较加热参数（ADR 0042）。

规划中的辅助脚本与配置，例如：

- `new_robot.py`：从 `app/bench/` 创建新兵种；
- `check_deps.py`：检查 `#include` 是否符合分层依赖规则；
- `check_forbidden.py`：检查禁用的写法；
- `check_linked.py`：确认关键函数确实链接进了 ELF；
- VOFA+、Ozone 工程模板等调试配置。
