# 代码格式化（clang-format）

本项目的 C 代码统一用 [clang-format](https://clang.llvm.org/docs/ClangFormat.html) 自动排版：缩进、空格、换行、花括号位置都由工具决定，
人不用手动对齐，审查时也不用为格式争论。规则写在仓库根目录的 `.clang-format`，各项设置的说明见
`docs/CODING_STANDARD.md` 第 5 节和附录 B。

**提交前必须格式化**，CI 会检查，不合格的提交无法通过。

## 1. 一条命令

在仓库根目录运行（Windows、Linux、macOS 都一样，需要 Python 3 和 clang-format 18）：

```bash
python3 tools/format.py              # 格式化全部本项目代码（直接改写文件）
python3 tools/format.py --changed    # 只格式化改过、还没提交的文件（日常用这个）
python3 tools/format.py --check      # 只检查不改：不合格的列出文件名，返回 1（CI 用的就是它）
python3 tools/format.py 文件1 文件2   # 只格式化指定的文件
```

Windows 上如果 `python3` 不可用，换成 `python` 或 `py -3`。

`tools/format.py` 是“哪些文件归本项目格式化”的唯一定义：Git 跟踪的 `.c` / `.h`，
**不含第三方代码**——CubeMX 生成的 `06_boards/`、`04_core/log/segger_rtt/`（SEGGER RTT）、`tests/host/unity/`（Unity）。
第三方代码保持原样，方便以后升级时和原版对比。

## 2. 安装 clang-format 18

**版本要和 CI 一致（18.1.3）**。不同大版本对个别写法的排版不一样（例如中文注释的对齐、长参数换行），
用别的版本格式化后，CI 可能仍判为不合格。脚本发现版本不是 18 时会提示。

### 推荐：用 pip 装（Windows、Linux、macOS 都一样）

PyPI 上的 `clang-format` 包就是 LLVM 官方编好的 clang-format 程序，版本号与 LLVM 一致，各系统都有：

```bash
pip install clang-format==18.1.3          # 或 python3 -m pip install …；Windows 也可以 py -3 -m pip install …
clang-format --version                    # 应显示 clang-format version 18.1.3
```

装好后 `clang-format` 在 Python 的脚本目录里（Linux / macOS 通常是 `~/.local/bin`，Windows 是 Python 安装目录下的 `Scripts`），
这个目录在 PATH 里就能直接用；不在的话用环境变量 `CLANG_FORMAT` 指向它。
不想装进系统的 Python，可以用 `pipx install clang-format==18.1.3`，或者先建一个虚拟环境（`python3 -m venv`）再装。

2026-10-11 实测：PyPI 的 18.1.3 在 Ubuntu 22.04 和 Windows 11 上检查本仓库，结果与 CI（Ubuntu 24.04 的 18.1.3）完全一致。

### 其他装法

| 系统 | 安装 | 说明 |
| --- | --- | --- |
| Ubuntu 24.04 / WSL 24.04 | `sudo apt install clang-format-18` | 与 CI 相同（CI 就是这么装的） |
| Ubuntu 22.04 | 用上面的 pip | 22.04 自带的软件源里最高只有 clang-format 15，没有 18。22.04 没装 pip 时先 `sudo apt install python3-pip`（或 `python3-venv`） |
| Windows | 用上面的 pip；或从 [LLVM 的 GitHub Releases](https://github.com/llvm/llvm-project/releases) 下载 18.1.x 的安装包 | 安装包要选“加入 PATH”，或用 `CLANG_FORMAT` 指定 |
| macOS | 用上面的 pip；或 `brew install llvm@18` | brew 装的在 `$(brew --prefix llvm@18)/bin/clang-format` |

脚本查找 clang-format 的顺序：环境变量 `CLANG_FORMAT` → `clang-format-18` → `clang-format`。例如：

```bash
CLANG_FORMAT=/path/to/clang-format python3 tools/format.py --changed      # Linux / macOS / Git Bash
```

```powershell
$env:CLANG_FORMAT = "C:\Tools\clang-format.exe"; python tools/format.py --changed   # PowerShell
```

## 3. 在编辑器里格式化

编辑器里格式化方便随手用，但**用的 clang-format 版本不一定是 18**，提交前仍以 `python3 tools/format.py --check` 为准。

### CLion

打开仓库后，CLion 识别到 `.clang-format` 会自动改用 ClangFormat（状态栏右下角显示 `ClangFormat`）。

- 格式化当前文件：`Ctrl+Alt+L`（macOS：`⌥⌘L`）；先选中几行再按，只格式化选中部分；
- 提交时自动格式化：提交窗口（`Ctrl+K`）里勾选 **Reformat code**。

CLion 自带的 clang-format 是较新的版本（例如 22），有二十多个文件的排版会和 18 版不同（2026-10-11 用 CLion 自带的 22 版实测）。
用 CLion 格式化后，再运行一次 `python3 tools/format.py --changed` 让结果回到 18 版的样子。
开着文件的 CLion 也可能按它自己的版本重排文件（2026-10-11 就发生过一次，`robot.c` 一行注释的对齐被改成 22 版的样子混进了提交），
所以**提交前最后一步**跑 `python3 tools/format.py --check`。

### VS Code

安装 Microsoft 的 **C/C++** 扩展，在设置（`settings.json`）里加：

```json
{
    "C_Cpp.clang_format_style": "file",
    "C_Cpp.clang_format_path": "填 clang-format 18 的路径",
    "editor.formatOnSave": true
}
```

之后保存时自动格式化；手动格式化当前文件是 `Shift+Alt+F`。

## 4. 不想被自动排版的地方

像寄存器表、参数表、任务表这种手工对齐更好读的表格，可以包起来让 clang-format 跳过：

```c
/* clang-format off */
const TaskTableEntry task_table[] = {
    /* 任务         名字           入口              优先级 */
    { &ins_task,    "ins_task",    ins_task_entry,   6u },
    { &log_task,    "log_task",    log_task_entry,   1u },
};
/* clang-format on */
```

按编码规范，用了就要在旁边写明原因（`docs/CODING_STANDARD.md` 第 6.1 节）；只用于表格，不要用来躲开格式化。

## 5. CI 怎么检查

推送到 GitHub 后，CI（`.github/workflows/ci.yml`）在 Ubuntu 24.04 上安装 clang-format 18，运行
`python3 tools/format.py --check`。不合格时，日志里会列出每个文件名，本地运行 `python3 tools/format.py` 修正后再提交即可。

## 6. 常见问题

**为什么不直接用 `clang-format --dry-run --Werror` 检查？**
clang-format 18.1.3（Ubuntu 24.04 自带的版本）在这个模式下报出格式错误后，**退出码仍然是 0**（2026-10-11 实测），
脚本和 CI 就会误以为通过。所以 `tools/format.py --check` 改成逐个比较“格式化后的内容”和文件原文，不依赖退出码。

**格式化会改动代码的意思吗？**
不会。clang-format 只改空白、换行、花括号的位置，以及给单行的 `if` / `for` 补上花括号（`.clang-format` 里的 `InsertBraces`），
不改变程序行为。

**格式化以后行尾变了？**
项目统一用 LF 换行（`.gitattributes`、`.editorconfig` 保证）。Windows 上请让编辑器使用 LF；Git 提交时也会统一成 LF。

**我只改了一行，却整个文件都变了？**
多半是这个文件以前没格式化过，或者用了别的版本的 clang-format。先确认 `clang-format --version` 是 18，
再运行 `python3 tools/format.py 这个文件`。如果确实是旧代码没格式化，把“格式化”单独提交一次，和功能修改分开，审查时才看得清。
