# 代码格式化（clang-format）

本项目的 C 代码用 clang-format 自动排版，规则在仓库根目录的 `.clang-format`（各项说明见 `docs/CODING_STANDARD.md`
第 5 节和附录 B）。CI 用 **clang-format 18.1.3** 检查，不合格的提交无法通过。版本必须一致：2026-10-11 实测，18.1.8 有 2 个文件、
CLion 自带的 22 有 22 个文件排得与 18.1.3 不同。所以要装 18.1.3，并让编辑器**用它**，而不是编辑器自带的版本。

## 1. 安装

### Ubuntu 24.04
```bash
sudo apt update
sudo apt install clang-format-18
clang-format-18 --version
```
程序路径：`/usr/bin/clang-format-18`

### Ubuntu 22.04
软件源里没有 18，用 pip 装 LLVM 官方编好的 18.1.3：
```bash
sudo apt install python3-pip
python3 -m pip install --user clang-format==18.1.3
~/.local/bin/clang-format --version
```
程序路径：`/home/你的用户名/.local/bin/clang-format`

### Windows 10 / 11
在 PowerShell 中运行：
```powershell
winget install LLVM.LLVM --version 18.1.3
```
安装后重开 PowerShell，确认：
```powershell
& "C:\Program Files\LLVM\bin\clang-format.exe" --version
```
程序路径：`C:\Program Files\LLVM\bin\clang-format.exe`

如果 winget 失败，就手动下载安装 `LLVM-18.1.3-win64.exe`。

### 通用备选
任何系统都可以用 pip 安装：
```bash
python -m pip install clang-format==18.1.3
```

以上安装完成后，版本应显示为：

```text
clang-format version 18.1.3
```

后续在 CLion 或 VS Code 中配置时，填对应的完整程序路径即可。

## 2. 编辑器配置

### CLion

1. 打开 **File → Settings**（`Ctrl+Alt+S`），搜索 **clang-format**，找到 C/C++ 代码风格里的 ClangFormat。
2. 勾选：
   - **Read code style from .clang-format files**
   - **Use external clang-format instead of the bundled one**
3. **Clang-format executable to use** 选“指定路径”，填上面的路径：
   - Ubuntu 24.04：`/usr/bin/clang-format-18`
   - Ubuntu 22.04：`/home/你的用户名/.local/bin/clang-format`
   - Windows：`C:\Program Files\LLVM\bin\clang-format.exe`
4. 点 **OK**。

日常使用：
- 格式化当前文件：`Ctrl+Alt+L`；选中几行再按，只格式化选中部分。
- 提交时自动格式化：提交窗口 `Ctrl+K` 里勾选 **Reformat code**。

> 只勾第 2 项、不勾第 3 项时，CLion 只是用自己的引擎模仿 `.clang-format`，结果可能和 18.1.3 不一致。

---

### VS Code

1. 安装 Microsoft 的 **C/C++** 扩展：`ms-vscode.cpptools`。
2. `Ctrl+Shift+P` → 输入 **Preferences: Open User Settings (JSON)**，在 `settings.json` 中加入：

**Ubuntu 24.04**

```json
{
    "C_Cpp.clang_format_path": "/usr/bin/clang-format-18",
    "C_Cpp.clang_format_style": "file",
    "[c]": { "editor.formatOnSave": true },
    "[cpp]": { "editor.formatOnSave": true }
}
```

**Ubuntu 22.04**

```json
{
    "C_Cpp.clang_format_path": "/home/你的用户名/.local/bin/clang-format",
    "C_Cpp.clang_format_style": "file",
    "[c]": { "editor.formatOnSave": true },
    "[cpp]": { "editor.formatOnSave": true }
}
```

**Windows**

```json
{
    "C_Cpp.clang_format_path": "C:\\Program Files\\LLVM\\bin\\clang-format.exe",
    "C_Cpp.clang_format_style": "file",
    "[c]": { "editor.formatOnSave": true },
    "[cpp]": { "editor.formatOnSave": true }
}
```

手动格式化：
- Ubuntu：`Ctrl+Shift+I`
- Windows：`Shift+Alt+F`

> `C_Cpp.clang_format_path` 必须填。不填时 VS Code 会找 PATH 里名为 `clang-format` 的程序，找不到就用扩展自带版本，可能不是 18.1.3。

## 3. 提交前检查（tools/format.py）

编辑器里格式化过也要在提交前查一次。在仓库根目录运行（Windows 上把 `python3` 换成 `python`、`/` 换成 `\`）：

```bash
python3 tools/format.py --check      # 显示“格式检查通过”才能提交
python3 tools/format.py --changed    # 不合格时：格式化所有改过、还没提交的文件
python3 tools/format.py 文件1 文件2   # 只格式化指定的文件
```

脚本只处理 Git 跟踪的本项目 `.c` / `.h`，跳过第三方代码（CubeMX 生成的 `06_boards/`、`04_core/log/segger_rtt/`、
`tests/host/unity/`）。它先找 `clang-format-18`，再找 `clang-format`；都不在 PATH 里（如 Windows、Ubuntu 22.04 的 pip 安装）时，
用环境变量指定第 1 节的程序路径：

```bash
CLANG_FORMAT=程序路径 python3 tools/format.py --check                 # Ubuntu
$env:CLANG_FORMAT="程序路径"; python tools\format.py --check          # Windows PowerShell
```

CI（`.github/workflows/ci.yml`）在 Ubuntu 24.04 上装 clang-format-18，运行同一条 `--check`；结果在 GitHub 仓库的 **Actions** 页面看。

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

## 5. 常见问题

**编辑器里格式化过了，`--check` 还说不合格？**
编辑器用的多半不是 18.1.3：CLion 没勾“用外部 clang-format”，或者 VS Code 没填 `C_Cpp.clang_format_path`。
按第 2 节检查设置。CLion 开着文件时也可能自动按它当时的设置重排文件
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
