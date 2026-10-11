# 代码格式化（clang-format）

本项目的 C 代码统一用 [clang-format](https://clang.llvm.org/docs/ClangFormat.html) 自动排版：缩进、空格、换行、花括号位置都由工具决定，
人不用手动对齐，审查时也不用为格式争论。规则写在仓库根目录的 `.clang-format`，各项设置的说明见
`docs/CODING_STANDARD.md` 第 5 节和附录 B。

**提交前必须格式化**，CI 会检查，不合格的提交无法通过。

- 第 1 节：版本为什么必须是 **18.1.3**（先看这个）
- 第 2 节：Ubuntu 教程
- 第 3 节：Windows 教程
- 第 4–6 节：不想被排版的地方、CI 怎么查、常见问题

## 1. 版本必须是 18.1.3

CI 用的是 clang-format **18.1.3**。不同版本对同一段代码可能排得不一样，**连同为 18 的修补版也会**：

| 版本 | 检查本仓库的结果（2026-10-11 实测） |
| --- | --- |
| 18.1.3（Ubuntu 24.04 的 apt、LLVM 官方安装包、PyPI 都一样） | 全部合格，与 CI 一致 |
| 18.1.8 | 2 个文件不合格（带花括号的宏排法变了，如 `buzzer.c` 的 `PATTERN`） |
| 22（CLion 自带） | 22 个文件不合格 |

所以下面的教程都装**正好 18.1.3**。`tools/format.py` 发现版本不是 18.1.3 时会提示。

clang-format 是一个独立的程序，**不需要 Python**。项目里的辅助脚本 `tools/format.py` 需要 Python 3：
它负责挑出“哪些文件归本项目格式化”（Git 跟踪的 `.c` / `.h`，**不含第三方代码**——CubeMX 生成的 `06_boards/`、
`04_core/log/segger_rtt/`、`tests/host/unity/`），再逐个调用 clang-format。没有 Python 也能格式化，见各教程的“不用脚本”。

---

## 2. Ubuntu 教程

适用于 Ubuntu 24.04 / 22.04，以及 Windows 上的 WSL（Ubuntu）。

### 第 1 步：安装 clang-format 18.1.3

先看系统版本：

```bash
lsb_release -ds          # 显示 Ubuntu 24.04.x LTS 或 22.04.x LTS
```

**Ubuntu 24.04**：软件源里的 clang-format-18 就是 18.1.3，和 CI 完全相同。

```bash
sudo apt update
sudo apt install clang-format-18
```

**Ubuntu 22.04**：软件源里最高只有 clang-format 15，没有 18。用 pip 装 LLVM 官方编好的 18.1.3：

```bash
sudo apt install python3-pip
python3 -m pip install --user clang-format==18.1.3
```

装好的程序在 `~/.local/bin/clang-format`。如果下一步提示找不到命令，把这个目录加进 PATH：

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

（网络需要走代理时，pip 前面加 `HTTPS_PROXY=http://代理地址:端口`。）

### 第 2 步：确认版本

```bash
clang-format-18 --version     # 24.04 用 apt 装的
clang-format --version        # 22.04 用 pip 装的
```

应显示 `clang-format version 18.1.3`（24.04 会多一个 `(1ubuntu1)`，不影响）。

### 第 3 步：格式化

进入仓库根目录：

```bash
cd 仓库所在目录/COD_RoboCore

python3 tools/format.py --changed    # 只格式化改过、还没提交的文件（日常用这个）
python3 tools/format.py --check      # 提交前最后检查一次：显示“格式检查通过”才能提交
```

其他用法：

```bash
python3 tools/format.py              # 格式化全部本项目代码
python3 tools/format.py 文件1 文件2   # 只格式化指定的文件
```

**不用脚本**：直接调用 clang-format，`-i` 表示直接改写文件。只对本项目的代码用，不要格式化第三方代码：

```bash
clang-format-18 -i 01_applic/tasks/control_task.c 01_applic/modules/chassis/chassis.h
```

### 第 4 步（可选）：编辑器里格式化

CLion、VS Code 的设置见第 3 节第 4 步，Linux 上一样（VS Code 的 `clang_format_path` 填 `/usr/bin/clang-format-18`
或 `~/.local/bin/clang-format`）。

---

## 3. Windows 教程

适用于 Windows 10 / 11。下面的命令都在 **PowerShell** 里运行（开始菜单搜“PowerShell”）。

### 第 1 步：安装 clang-format 18.1.3（LLVM 官方安装包）

**方法 A：winget（推荐，一条命令）**。winget 是 Windows 自带的软件包管理器。

```powershell
winget install LLVM.LLVM --version 18.1.3
```

winget 下载的是 LLVM 官方的 `LLVM-18.1.3-win64.exe`（来自 LLVM 的 GitHub Releases），默认装到 `C:\Program Files\LLVM`。

**方法 B：手动下载安装包**。打开 <https://github.com/llvm/llvm-project/releases/tag/llvmorg-18.1.3>，
下载 `LLVM-18.1.3-win64.exe` 运行；安装时选择把 LLVM 加入 PATH（Add LLVM to the system PATH）。

LLVM 安装包里除了 clang-format，还有编译器等其他工具，体积较大；本项目只用其中的 clang-format。

### 第 2 步：确认能找到 clang-format

**关掉再重新打开 PowerShell**（新装的 PATH 只对新窗口生效），然后：

```powershell
clang-format --version
```

应显示 `clang-format version 18.1.3`。

如果提示“无法将 clang-format 识别为 cmdlet、函数…”，说明 LLVM 的目录不在 PATH 里（winget 静默安装时可能不会加）。
把它加进当前用户的 PATH，再重开 PowerShell：

```powershell
$p = [Environment]::GetEnvironmentVariable("Path", "User")
[Environment]::SetEnvironmentVariable("Path", "$p;C:\Program Files\LLVM\bin", "User")
```

如果显示的不是 18.1.3（比如电脑上以前装过别的版本的 LLVM），用 `where.exe clang-format` 看找到的是哪一个，
把 18.1.3 的目录排到 PATH 前面，或者用第 3 步的 `CLANG_FORMAT` 直接指定。

### 第 3 步：格式化

进入仓库根目录（换成你自己的路径）：

```powershell
cd D:\你的路径\COD_RoboCore
```

**装了 Python 3 的**（项目的 `keil_sync.py`、MuJoCo 仿真也要用，大多数人迟早要装）：

```powershell
python tools\format.py --changed    # 只格式化改过、还没提交的文件（日常用这个）
python tools\format.py --check      # 提交前最后检查一次：显示“格式检查通过”才能提交
```

`python` 不可用时换成 `py -3`。要指定 clang-format 的位置：

```powershell
$env:CLANG_FORMAT = "C:\Program Files\LLVM\bin\clang-format.exe"
python tools\format.py --changed
```

**没装 Python 的**：直接调用 clang-format。格式化一个文件：

```powershell
clang-format -i 01_applic\tasks\control_task.c
```

格式化所有改过、还没提交的 `.c` / `.h`（跳过第三方代码；完整的排除规则以 `tools/format.py` 为准）：

```powershell
git diff --name-only HEAD -- '*.c' '*.h' |
    Where-Object { $_ -notmatch '^06_boards/|segger_rtt/|tests/host/unity/' } |
    ForEach-Object { clang-format -i $_ }
```

### 第 4 步（可选）：编辑器里格式化

编辑器里格式化方便随手用，但**用的 clang-format 版本不一定是 18.1.3**，提交前仍以第 3 步的 `--check` 为准。

**CLion**：打开仓库后，CLion 识别到 `.clang-format` 会自动改用 ClangFormat（状态栏右下角显示 `ClangFormat`）。

- 格式化当前文件：`Ctrl+Alt+L`；先选中几行再按，只格式化选中部分；
- 提交时自动格式化：提交窗口（`Ctrl+K`）里勾选 **Reformat code**。

CLion 用的是它自带的 clang-format（较新的版本，实测 22），有二十多个文件的排版会和 18.1.3 不同。
用 CLion 格式化后，再运行一次第 3 步的命令，让结果回到 18.1.3 的样子。
开着文件的 CLion 也可能按它自己的版本重排文件（2026-10-11 就发生过一次，`robot.c` 一行注释的对齐被改成 22 版的样子混进了提交），
所以**提交前最后一步**跑 `--check`。

**VS Code**：安装 Microsoft 的 **C/C++** 扩展，在设置（`settings.json`）里加：

```json
{
    "C_Cpp.clang_format_style": "file",
    "C_Cpp.clang_format_path": "C:\\Program Files\\LLVM\\bin\\clang-format.exe",
    "editor.formatOnSave": true
}
```

之后保存时自动格式化；手动格式化当前文件是 `Shift+Alt+F`。这样用的就是第 1 步装的 18.1.3。

---

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

推送到 GitHub 后，CI（`.github/workflows/ci.yml`）在 Ubuntu 24.04 上用 apt 安装 clang-format-18（即 18.1.3），运行
`python3 tools/format.py --check`。不合格时，日志里会列出每个文件名，本地格式化后再提交即可。

## 6. 常见问题

**为什么不直接用 `clang-format --dry-run --Werror` 检查？**
clang-format 18.1.3 在这个模式下报出格式错误后，**退出码仍然是 0**（2026-10-11 实测），
脚本和 CI 就会误以为通过。所以 `tools/format.py --check` 改成逐个比较“格式化后的内容”和文件原文，不依赖退出码。

**格式化会改动代码的意思吗？**
不会。clang-format 只改空白、换行、花括号的位置，以及给单行的 `if` / `for` 补上花括号（`.clang-format` 里的 `InsertBraces`），
不改变程序行为。

**格式化以后行尾变了？**
项目统一用 LF 换行（`.gitattributes`、`.editorconfig` 保证）。Windows 上请让编辑器使用 LF；Git 提交时也会统一成 LF。

**我只改了一行，却整个文件都变了？**
多半是用了别的版本的 clang-format（先看 `clang-format --version` 是不是 18.1.3），或者这个文件以前没格式化过。
确实是旧代码没格式化的，把“格式化”单独提交一次，和功能修改分开，审查时才看得清。

**能不能用 pip 装？**
可以，任何系统都能用：`python -m pip install clang-format==18.1.3`（PyPI 上是 LLVM 官方编好的程序）。
2026-10-11 实测，PyPI 的 18.1.3 在 Ubuntu 22.04、Windows 11 上检查本仓库，结果与 CI 一致。
