#!/usr/bin/env python3
"""
用仓库根目录的 .clang-format 格式化本项目的 C 代码（说明见 docs/FORMATTING.md）。

    python3 tools/format.py              # 格式化全部（直接改写文件）
    python3 tools/format.py --changed    # 只格式化相对 HEAD 改过、还没提交的文件
    python3 tools/format.py --check      # 只检查不改，有不合格的返回 1（CI 用这个）
    python3 tools/format.py 文件 ...      # 只格式化指定文件

“哪些文件归本项目格式化”只在本文件定义（CI 也调用它）：Git 跟踪的 .c / .h，
排除第三方代码（CubeMX 生成的 06_boards/、SEGGER RTT、Unity）。
clang-format 的位置：环境变量 CLANG_FORMAT，否则依次找 clang-format-18、clang-format。

检查不靠 clang-format 的退出码：clang-format 18.1.3 的 --dry-run --Werror 报出格式错误后仍返回 0
（2026-10-11 在 Ubuntu 24.04 上实测），所以逐个比较“格式化后的内容”和文件原文。
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXPECTED_MAJOR = "18"  # 与 CI 一致；其他版本个别写法的排版可能不同
EXCLUDE = ("06_boards/", "04_core/log/segger_rtt/", "tests/host/unity/")


def find_clang_format():
    exe = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format-18") or shutil.which("clang-format")
    if not exe:
        sys.exit("找不到 clang-format：安装 clang-format 18，或用环境变量 CLANG_FORMAT 指定路径（docs/FORMATTING.md）")
    version = subprocess.run([exe, "--version"], capture_output=True, text=True).stdout.strip()
    if f"version {EXPECTED_MAJOR}." not in version:
        print(f"注意：{version}，与 CI 的 {EXPECTED_MAJOR} 版不同，结果以 CI 为准", file=sys.stderr)
    return exe


def check(exe, files):
    """返回不合格的文件列表：clang-format 输出与文件原文不同即不合格"""
    bad = []
    for f in files:
        path = os.path.join(ROOT, f)
        formatted = subprocess.run([exe, "--style=file", path], cwd=ROOT, capture_output=True, check=True).stdout
        with open(path, "rb") as fh:
            if fh.read() != formatted:
                bad.append(f)
    return bad


def git_files(*args):
    out = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    return [f for f in out.split("\n") if f.endswith((".c", ".h")) and not f.startswith(EXCLUDE)
            and os.path.exists(os.path.join(ROOT, f))]


def main():
    if os.environ.get("MSYSTEM"):  # Windows 的 Git Bash 终端是 UTF-8，Python 默认按本地代码页输出会乱码
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description="用 .clang-format 格式化本项目的 C 代码")
    ap.add_argument("files", nargs="*", help="只处理这些文件（默认全部）")
    ap.add_argument("--check", action="store_true", help="只检查不改，有不合格的返回 1")
    ap.add_argument("--changed", action="store_true", help="只处理相对 HEAD 改过、还没提交的文件")
    args = ap.parse_args()

    if args.files:
        files = [os.path.relpath(os.path.abspath(f), ROOT).replace(os.sep, "/") for f in args.files]
    elif args.changed:
        files = sorted(set(git_files("diff", "--name-only", "HEAD"))
                       | set(git_files("ls-files", "--others", "--exclude-standard")))
    else:
        files = git_files("ls-files", "--cached", "--others", "--exclude-standard")
    if not files:
        print("没有要处理的文件")
        return 0

    exe = find_clang_format()
    if args.check:
        bad = check(exe, files)
        for f in bad:
            print(f"格式不合格：{f}")
        print(f"有 {len(bad)} 个文件不合格：运行 python3 tools/format.py 修正" if bad else f"格式检查通过（{len(files)} 个文件）")
        return 1 if bad else 0
    subprocess.run([exe, "-i", *files], cwd=ROOT, check=True)
    print(f"已格式化 {len(files)} 个文件")
    return 0


if __name__ == "__main__":
    sys.exit(main())
