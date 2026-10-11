# 代码格式化（clang-format）

本项目的 C 代码统一用 [clang-format](https://clang.llvm.org/docs/ClangFormat.html) 自动排版：缩进、空格、换行、花括号位置都由工具决定，
人不用手动对齐，审查时也不用为格式争论。规则写在仓库根目录的 `.clang-format`，各项设置的说明见
`docs/CODING_STANDARD.md` 第 5 节和附录 B。

**提交前必须格式化**，CI（GitHub 上每次推送自动运行的检查）会检查，不合格的提交无法通过。

我们主要用 **CLion** 和 **VS Code**。下面的教程分三步，每一步都要做：

1. 安装 clang-format **18.1.3**；
2. 设置编辑器（CLion 或 VS Code，用哪个设哪个），让它**调用这个 18.1.3** 来格式化；
3. 日常：编辑器里格式化，提交前最后检查一次。

- 第 1 节：版本为什么必须是 18.1.3（先看这个）
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

所以下面的教程都装**正好 18.1.3**，并让编辑器**用它**，而不是编辑器自带的版本。

clang-format 是一个独立的程序，**不需要 Python**。项目里的检查脚本 `tools/format.py` 需要 Python 3：
它负责挑出“哪些文件归本项目格式化”（Git 跟踪的 `.c` / `.h`，**不含第三方代码**——CubeMX 生成的 `06_boards/`、
`04_core/log/segger_rtt/`、`tests/host/unity/`），再逐个调用 clang-format。没有 Python 也能检查，见各教程第 4 步。

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
clang-format-18 --version      # 应显示 clang-format version 18.1.3（后面多一个 (1ubuntu1)，不影响）
```

程序路径是 `/usr/bin/clang-format-18`，第 2、3 步要用。

**Ubuntu 22.04**：软件源里最高只有 clang-format 15，没有 18。用 pip 装 LLVM 官方编好的 18.1.3：

```bash
sudo apt install python3-pip
python3 -m pip install --user clang-format==18.1.3
~/.local/bin/clang-format --version     # 应显示 clang-format version 18.1.3
```

程序路径是 `/home/你的用户名/.local/bin/clang-format`，第 2、3 步要用。
（网络需要走代理时，pip 前面加 `HTTPS_PROXY=http://代理地址:端口`。）

### 第 2 步：设置 CLion（用 CLion 的做）

1. 打开 **File → Settings**（`Ctrl+Alt+S`），在左上角的搜索框输入 **clang-format**，找到 C/C++ 代码风格里的 ClangFormat 设置；
2. 勾选 **Read code style from .clang-format files**（从 `.clang-format` 读取代码风格）；
3. 勾选 **Use external clang-format instead of the bundled one**（用外部 clang-format 代替自带的）；
4. **Clang-format executable to use** 选“指定路径”，填第 1 步的程序路径（`/usr/bin/clang-format-18` 或 `~/.local/bin/clang-format` 的完整路径）；
5. 点 **OK**。

只勾第 2 项、不勾第 3 项时，CLion 是用它自己的格式化引擎去模仿 `.clang-format` 的规则，结果和 18.1.3 不完全一样；
三项都设好，CLion 才是直接调用 18.1.3。

日常使用：

- 格式化当前文件：`Ctrl+Alt+L`；先选中几行再按，只格式化选中部分；
- 提交时自动格式化：提交窗口（`Ctrl+K`）里勾选 **Reformat code**。

### 第 3 步：设置 VS Code（用 VS Code 的做）

1. 安装 Microsoft 的 **C/C++** 扩展（扩展商店搜 `ms-vscode.cpptools`）；
2. `Ctrl+Shift+P` → 输入 **Preferences: Open User Settings (JSON)**，在打开的 `settings.json` 里加上（路径换成第 1 步的）：

```json
{
    "C_Cpp.clang_format_path": "/usr/bin/clang-format-18",
    "C_Cpp.clang_format_style": "file",
    "[c]": { "editor.formatOnSave": true },
    "[cpp]": { "editor.formatOnSave": true }
}
```

之后保存 `.c` / `.h` 时自动格式化（VS Code 默认把 `.h` 当 C++，所以 `[c]`、`[cpp]` 都要写）；手动格式化当前文件是 `Ctrl+Shift+I`。

`C_Cpp.clang_format_path` 必须填：不填时 VS Code 先找 PATH 里名叫 `clang-format` 的程序，找不到就用扩展自带的版本，
而 Ubuntu 24.04 上装的叫 `clang-format-18`，会被跳过。

### 第 4 步：日常使用与提交前检查

平时在编辑器里写代码、保存或按快捷键格式化就行。**提交前最后检查一次**，进入仓库根目录：

```bash
cd 仓库所在目录/项目
python3 tools/format.py --check      # 显示“格式检查通过”才能提交
```

不合格时，脚本会列出文件名。修正的办法：

```bash
python3 tools/format.py --changed    # 格式化所有改过、还没提交的文件
python3 tools/format.py 文件1 文件2   # 只格式化指定的文件
```

脚本会自动找 `clang-format-18`，找不到再找 `clang-format`；要指定位置：`CLANG_FORMAT=程序路径 python3 tools/format.py --check`。

**不用脚本**也可以直接调用 clang-format，`-i` 表示直接改写文件。只对本项目的代码用，不要格式化第三方代码：

```bash
clang-format-18 -i 文件夹A/文件夹B/task.c
```

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

装好后确认（**关掉再重新打开 PowerShell**，新装的 PATH 只对新窗口生效）：

```powershell
& "C:\Program Files\LLVM\bin\clang-format.exe" --version     # 应显示 clang-format version 18.1.3
clang-format --version                                       # 能显示 18.1.3，说明已在 PATH 里
```

程序路径是 `C:\Program Files\LLVM\bin\clang-format.exe`，第 2、3 步要用。

第二条提示“无法将 clang-format 识别为 cmdlet、函数…”时，说明 LLVM 的目录不在 PATH 里（winget 静默安装时可能不会加）。
编辑器里填完整路径就不受影响；想在命令行直接用，把它加进当前用户的 PATH，再重开 PowerShell：

```powershell
$p = [Environment]::GetEnvironmentVariable("Path", "User")
[Environment]::SetEnvironmentVariable("Path", "$p;C:\Program Files\LLVM\bin", "User")
```

第二条显示的不是 18.1.3（电脑上以前装过别的 LLVM）时，用 `where.exe clang-format` 看找到的是哪一个，
把 18.1.3 的目录排到 PATH 前面；编辑器里填完整路径同样不受影响。

### 第 2 步：设置 CLion（用 CLion 的做）

1. 打开 **File → Settings**（`Ctrl+Alt+S`），在左上角的搜索框输入 **clang-format**，找到 C/C++ 代码风格里的 ClangFormat 设置；
2. 勾选 **Read code style from .clang-format files**（从 `.clang-format` 读取代码风格）；
3. 勾选 **Use external clang-format instead of the bundled one**（用外部 clang-format 代替自带的）；
4. **Clang-format executable to use** 选“指定路径”，填 `C:\Program Files\LLVM\bin\clang-format.exe`；
5. 点 **OK**。

只勾第 2 项、不勾第 3 项时，CLion 是用它自己的格式化引擎去模仿 `.clang-format` 的规则，结果和 18.1.3 不完全一样；
三项都设好，CLion 才是直接调用 18.1.3。

日常使用：

- 格式化当前文件：`Ctrl+Alt+L`；先选中几行再按，只格式化选中部分；
- 提交时自动格式化：提交窗口（`Ctrl+K`）里勾选 **Reformat code**。

### 第 3 步：设置 VS Code（用 VS Code 的做）

1. 安装 Microsoft 的 **C/C++** 扩展（扩展商店搜 `ms-vscode.cpptools`）；
2. `Ctrl+Shift+P` → 输入 **Preferences: Open User Settings (JSON)**，在打开的 `settings.json` 里加上：

```json
{
    "C_Cpp.clang_format_path": "C:\\Program Files\\LLVM\\bin\\clang-format.exe",
    "C_Cpp.clang_format_style": "file",
    "[c]": { "editor.formatOnSave": true },
    "[cpp]": { "editor.formatOnSave": true }
}
```

（JSON 里的反斜杠要写两个。）之后保存 `.c` / `.h` 时自动格式化（VS Code 默认把 `.h` 当 C++，所以 `[c]`、`[cpp]` 都要写）；手动格式化当前文件是 `Shift+Alt+F`。

`C_Cpp.clang_format_path` 建议填上：不填时 VS Code 先找 PATH 里的 `clang-format`，找不到就用扩展自带的版本，那就不是 18.1.3 了。

### 第 4 步：日常使用与提交前检查

平时在编辑器里写代码、保存或按快捷键格式化就行。**提交前最后检查一次**，进入仓库根目录（换成你自己的路径）：

```powershell
cd D:\你的路径\项目
```

**装了 Python 3 的**（项目的 `keil_sync.py`、MuJoCo 仿真也要用，大多数人迟早要装）：

```powershell
$env:CLANG_FORMAT = "C:\Program Files\LLVM\bin\clang-format.exe"   # clang-format 已在 PATH 里时可以省略
python tools\format.py --check      # 显示“格式检查通过”才能提交
python tools\format.py --changed    # 不合格时：格式化所有改过、还没提交的文件
```

`python` 不可用时换成 `py -3`。

**没装 Python 的**：直接调用 clang-format。格式化一个文件：

```powershell
clang-format -i 文件夹A\文件夹B\task.c
```

格式化所有改过、还没提交的 `.c` / `.h`（跳过第三方代码；完整的排除规则以 `tools/format.py` 为准）：

```powershell
git diff --name-only HEAD -- '*.c' '*.h' |
    Where-Object { $_ -notmatch '^06_boards/|segger_rtt/|tests/host/unity/' } |
    ForEach-Object { clang-format -i $_ }
```

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
`python3 tools/format.py --check`。结果在 GitHub 仓库页面的 **Actions** 里看；不合格时，日志里会列出每个文件名，
本地格式化后再提交即可。

## 6. 常见问题

**编辑器里格式化过了，`--check` 还说不合格？**
编辑器用的多半不是 18.1.3：CLion 没勾“用外部 clang-format”，或者 VS Code 没填 `C_Cpp.clang_format_path`。
按教程第 2、3 步检查设置。CLion 开着文件时也可能自动按它当时的设置重排文件
（2026-10-11 就发生过一次，`robot.c` 一行注释的对齐被改成 CLion 自带 22 版的样子混进了提交），所以提交前一定跑 `--check`。

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
