# tools/

已有：

- `gen_readme_diagrams.py`：生成 README 里的三张结构图（`docs/images/*.svg`）。改图改这个脚本再运行，不要手改 SVG。

规划中的辅助脚本与配置，例如：

- `new_robot.py`：从 `robots/_template/` 创建新兵种；
- `check_deps.py`：检查 `#include` 是否符合分层依赖规则；
- `check_forbidden.py`：检查禁用的写法；
- `check_linked.py`：确认关键函数确实链接进了 ELF；
- VOFA+、Ozone 工程模板等调试配置。
