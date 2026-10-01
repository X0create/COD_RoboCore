#!/usr/bin/env python3
"""把 CubeMX 生成的 Keil 工程整理成可以编译本框架的工程（ADR 0053）。

用法（WSL，仓库根目录；先让 CMake 生成两个兵种的编译清单）：
    cmake --preset h723-infantry-debug
    python3 tools/keil_sync.py            # 改写 06_boards/dm_mc02_h723/MDK-ARM/dm_mc02.uvprojx / .uvoptx
    python3 tools/keil_sync.py --check    # 只检查工程是否最新（不改文件），不是最新时返回 1

CubeMX 每次按 MDK-ARM 重新生成都会把工程改回它自己的样子，重跑本脚本即可。脚本做的事：
  1. 以 CubeMX 生成的 Target（dm_mc02）为模板，每个兵种建一个 Target（目前只有 infantry），CubeMX 的源文件分组原样保留；
  2. 加入本框架 01–05 层的源文件：文件列表、宏定义、头文件路径都取自 CMake 的 compile_commands.json，两边编的是同一组文件；
  3. FreeRTOS 移植层由 RVDS 改为 GCC（AC6 应使用 GCC 移植层；RVDS 是给 AC5 的，CubeMX 按 CMake 生成时还会删掉它）；
  4. 编译器 AC6、C 语言 gnu11、-O0（与 CMake 的 Debug 一致）、每个函数单独一段；
  5. 链接用 ../dm_mc02.sct（与 dm_mc02.ld 同样的内存布局，.dma_buf 在 0x24000000），不用 Keil 自动生成的布局；
  6. 调试器 J-Link（与 Ozone 相同），Flash 算法 STM32H72x-73x_1024；
  7. 输出放到 build/keil/<兵种>/（不进 Git）。
"""
import json
import os
import re
import shlex
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOARD = os.path.join(REPO, "06_boards", "dm_mc02_h723")
MDK = os.path.join(BOARD, "MDK-ARM")
PROJX = os.path.join(MDK, "dm_mc02.uvprojx")
OPTX = os.path.join(MDK, "dm_mc02.uvoptx")
ROBOTS = ["infantry"]
TEMPLATE_TARGET = "dm_mc02"  # CubeMX 生成的 Target 名

# CMake 编、Keil 不编的文件：GCC 专用（newlib 桩函数、GNU 语法汇编）；Keil 用 MDK-ARM/startup_stm32h723xx.s
GCC_ONLY = {"syscalls.c", "sysmem.c"}
# Keil 额外的宏：RTT 不用 GNU 语法的汇编版（SEGGER_RTT_ASM_ARMv7M.S），改用等价的 C 版
KEIL_DEFINES = ["RTT_USE_ASM=0"]

AC6 = "6220000::V6.22::ARMCLANG"
JLINK_ARGS = ('-U-O78 -O78 -S2 -ZTIFSpeedSel5000 -A0 -C0 -JU1 -JI127.0.0.1 -JP0 -RST0 -N00("ARM CoreSight SW-DP") '
              '-D00(6BA02477) -L00(0) -TO18 -TC10000000 -TP21 -TDS8007 -TDT0 -TDC1F -TIEFFFFFFFF -TIP8 -TB1 -TFE0 '
              '-FO15 -FD20000000 -FC8000 -FN1 -FF0STM32H72x-73x_1024.FLM -FS08000000 -FL0100000 '
              '-FP0($$Device:STM32H723VGTx$CMSIS\\Flash\\STM32H72x-73x_1024.FLM)')


def keil_path(abs_path):
    return os.path.relpath(abs_path, MDK).replace("/", "\\")


def load_robot(robot):
    """返回 (本框架的源文件列表, 宏定义列表, 头文件路径列表)，都来自 CMake 的编译清单"""
    path = os.path.join(REPO, "build", f"h723-{robot}-debug", "compile_commands.json")
    if not os.path.exists(path):
        sys.exit(f"缺少 {path}：先运行 cmake --preset h723-{robot}-debug")
    files, defines, includes = [], [], []
    for entry in json.load(open(path, encoding="utf-8")):
        src = os.path.normpath(entry["file"])
        rel = os.path.relpath(src, REPO).replace("\\", "/")
        args = shlex.split(entry["command"])
        for i, a in enumerate(args):
            if a.startswith("-D") and a[2:] not in defines:
                defines.append(a[2:])
            elif a in ("-I", "-isystem") or a.startswith("-I"):
                d = args[i + 1] if a in ("-I", "-isystem") else a[2:]
                d = os.path.normpath(d)
                if d not in includes:
                    includes.append(d)
        if rel.startswith("06_boards/"):
            continue  # CubeMX 的文件由 CubeMX 自己列在工程里
        if os.path.basename(rel) in GCC_ONLY or rel.endswith((".s", ".S")):
            continue
        files.append(rel)
    return sorted(files), defines, includes


def group_xml(name, files, included):
    file_xml = "".join(
        "            <File>\n"
        f"              <FileName>{os.path.basename(f)}</FileName>\n"
        "              <FileType>1</FileType>\n"
        f"              <FilePath>{keil_path(os.path.join(REPO, f))}</FilePath>\n"
        "            </File>\n" for f in files)
    option = "" if included else (
        "          <GroupOption>\n            <CommonProperty>\n"
        "              <UseCPPCompiler>0</UseCPPCompiler>\n              <RVCTCodeConst>0</RVCTCodeConst>\n"
        "              <RVCTZI>0</RVCTZI>\n              <RVCTOtherData>0</RVCTOtherData>\n"
        "              <ModuleSelection>0</ModuleSelection>\n              <IncludeInBuild>0</IncludeInBuild>\n"
        "              <AlwaysBuild>2</AlwaysBuild>\n              <GenerateAssemblyFile>2</GenerateAssemblyFile>\n"
        "              <AssembleAssemblyFile>2</AssembleAssemblyFile>\n              <PublicsOnly>2</PublicsOnly>\n"
        "              <StopOnExitCode>11</StopOnExitCode>\n              <CustomArgument></CustomArgument>\n"
        "              <IncludeLibraryModules></IncludeLibraryModules>\n              <ComprImg>1</ComprImg>\n"
        "            </CommonProperty>\n          </GroupOption>\n")
    return f"        <Group>\n          <GroupName>{name}</GroupName>\n{option}          <Files>\n{file_xml}          </Files>\n        </Group>\n"


def set_tag(text, tag, value, count=0):
    new, n = re.subn(rf"<{tag}>[^<]*</{tag}>", lambda m: f"<{tag}>{value}</{tag}>", text, count=count)
    if n == 0:
        sys.exit(f"工程里找不到 <{tag}>：CubeMX 生成的格式变了，需要更新 tools/keil_sync.py")
    return new


def find_targets(text, block):
    # CubeMX 和 Keil 的缩进不同（空格 / 制表符），Keil 保存时还会加空行，所以不按缩进匹配
    return re.findall(r"^[ \t]*<Target>\n.*?\n[ \t]*</Target>\n", text, flags=re.S | re.M)


def check_projx(text, robots):
    """只比较会影响编译的内容（与格式无关，Keil 重新保存工程不算变化）。返回不一致的说明列表"""
    problems = []
    targets = {re.search(r"<TargetName>([^<]*)</TargetName>", t).group(1): t for t in find_targets(text, "Target")}
    for robot, (files, defines, includes) in robots.items():
        t = targets.get(robot)
        if t is None:
            problems.append(f"没有 Target {robot}")
            continue
        have = set()
        for g in re.findall(r"<Group>\s*<GroupName>(0[1-5]_[^<]*)</GroupName>(.*?)</Group>", t, flags=re.S):
            if "<IncludeInBuild>0</IncludeInBuild>" in g[1].split("<Files>")[0]:
                continue  # 这个分组在本 Target 不参与编译
            have.update(p.replace("\\", "/") for p in re.findall(r"<FilePath>([^<]*)</FilePath>", g[1]))
        want = {os.path.relpath(os.path.join(REPO, f), MDK).replace("\\", "/") for f in files}
        for f in sorted(want - have):
            problems.append(f"{robot}：Keil 工程缺少 {f}")
        for f in sorted(have - want):
            problems.append(f"{robot}：Keil 工程多了 {f}（CMake 不编它）")
        cads = re.search(r"<Cads>.*?</Cads>", t, flags=re.S).group(0)
        if re.search(r"<Define>([^<]*)</Define>", cads).group(1) != ",".join(defines + KEIL_DEFINES):
            problems.append(f"{robot}：宏定义与 CMake 不一致")
        if re.search(r"<IncludePath>([^<]*)</IncludePath>", cads).group(1) != ";".join(keil_path(d) for d in includes):
            problems.append(f"{robot}：头文件路径与 CMake 不一致")
        if f"<ScatterFile>{keil_path(os.path.join(BOARD, 'dm_mc02.sct'))}</ScatterFile>" not in t:
            problems.append(f"{robot}：没有使用 dm_mc02.sct")
        if "RVDS" in t:
            problems.append(f"{robot}：FreeRTOS 仍是 RVDS 移植层（应为 GCC）")
    return problems


def pick_template(targets):
    for t in targets:
        if f"<TargetName>{TEMPLATE_TARGET}</TargetName>" in t:
            return t
    return targets[0]  # 已经整理过：用第一个兵种的 Target，下面会去掉上次加的分组


def build_projx(text, robots):
    targets = find_targets(text, "Target")
    tmpl = pick_template(targets)
    # 去掉上次加的本框架分组（组名以层的编号开头），只留 CubeMX 的分组
    tmpl = re.sub(r"        <Group>\n          <GroupName>0[1-5]_[^<]*</GroupName>\n.*?\n        </Group>\n", "", tmpl, flags=re.S)
    tmpl = tmpl.replace("portable/RVDS/ARM_CM4F", "portable/GCC/ARM_CM4F").replace("portable\\RVDS\\ARM_CM4F", "portable\\GCC\\ARM_CM4F")
    if "<uAC6>" not in tmpl:
        tmpl = tmpl.replace("<ToolsetName>ARM-ADS</ToolsetName>\n",
                            f"<ToolsetName>ARM-ADS</ToolsetName>\n      <pArmCC>{AC6}</pArmCC>\n      <pCCUsed>{AC6}</pCCUsed>\n      <uAC6>1</uAC6>\n", 1)

    # 所有兵种的本框架文件，按目录分组；只有某个兵种用到的分组在其他兵种里不参与编译
    per_robot = {r: robots[r][0] for r in robots}
    all_files = sorted(set(f for fs in per_robot.values() for f in fs))
    groups = {}
    for f in all_files:
        groups.setdefault(os.path.dirname(f), []).append(f)

    out = []
    for robot, (files, defines, includes) in robots.items():
        t = set_tag(tmpl, "TargetName", robot, 1)
        t = set_tag(t, "OutputDirectory", keil_path(os.path.join(REPO, "build", "keil", robot)) + "\\", 1)
        t = set_tag(t, "OutputName", "COD_RoboCore", 1)
        t = set_tag(t, "ListingPath", keil_path(os.path.join(REPO, "build", "keil", robot)) + "\\", 1)
        t = set_tag(t, "CreateHexFile", "0", 1)
        t = set_tag(t, "BrowseInformation", "1", 1)
        cads = re.search(r"<Cads>.*?</Cads>", t, flags=re.S).group(0)
        c = set_tag(cads, "Optim", "1", 1)  # -O0，与 CMake 的 Debug 相同
        c = set_tag(c, "v6Lang", "6", 1)  # gnu11，与 CMake 相同
        c = set_tag(c, "OneElfS", "1", 1)
        c = set_tag(c, "Define", ",".join(defines + KEIL_DEFINES), 1)
        c = set_tag(c, "IncludePath", ";".join(keil_path(d) for d in includes), 1)
        t = t.replace(cads, c)
        ld = re.search(r"<LDads>.*?</LDads>", t, flags=re.S).group(0)
        l2 = set_tag(set_tag(ld, "umfTarg", "0", 1), "ScatterFile", keil_path(os.path.join(BOARD, "dm_mc02.sct")), 1)
        t = t.replace(ld, l2)
        mine = "".join(group_xml(g, fs, any(f in files for f in fs)) for g, fs in sorted(groups.items()))
        t = t.replace("      </Groups>\n", mine + "      </Groups>\n", 1)
        out.append(t)

    start = text.index(targets[0]); end = text.index(targets[-1]) + len(targets[-1])
    return text[:start] + "".join(out) + text[end:]


def build_optx(text, robots):
    targets = find_targets(text, "OptTarget")
    tmpl = pick_template(targets)
    tmpl = set_tag(tmpl, "pMon", "Segger\\JL2CM3.dll", 1)
    tmpl = set_tag(tmpl, "nTsel", "4", 1)
    if "<Key>JL2CM3</Key>" not in tmpl:
        tmpl = tmpl.replace("<TargetDriverDllRegistry>\n",
                            "<TargetDriverDllRegistry>\n\t\t\t\t<SetRegEntry>\n\t\t\t\t\t<Number>0</Number>\n"
                            "\t\t\t\t\t<Key>JL2CM3</Key>\n"
                            f"\t\t\t\t\t<Name>{JLINK_ARGS}</Name>\n\t\t\t\t</SetRegEntry>\n", 1)
    out = [set_tag(tmpl, "TargetName", r, 1) for r in robots]
    start = text.index(targets[0]); end = text.index(targets[-1]) + len(targets[-1])
    return text[:start] + "".join(out) + text[end:]


def main():
    robots = {r: load_robot(r) for r in ROBOTS}
    if "--check" in sys.argv:
        with open(PROJX, encoding="utf-8", newline="") as f:
            problems = check_projx(f.read().replace("\r\n", "\n"), robots)
        if problems:
            print("Keil 工程与 CMake 不一致（在 Keil 里补上，或运行 python3 tools/keil_sync.py）：")
            print("\n".join("  " + p for p in problems))
            return 1
        print("Keil 工程是最新的")
        return 0
    check = False
    changed = []
    for path, build in ((PROJX, build_projx), (OPTX, build_optx)):
        with open(path, encoding="utf-8", newline="") as f:
            old = f.read().replace("\r\n", "\n")
        # CubeMX 把空项写成 <X />，Keil 自己保存时写 <X></X>；统一成后者再改（两种写法 Keil 都认）
        new = build(re.sub(r"<(\w+) />", r"<\1></\1>", old), robots)
        if new != old:
            changed.append(os.path.relpath(path, REPO))
            if not check:
                with open(path, "w", encoding="utf-8", newline="\r\n") as f:
                    f.write(new)
    if check:
        if changed:
            print("Keil 工程不是最新，运行 python3 tools/keil_sync.py：", ", ".join(changed))
            return 1
        print("Keil 工程是最新的")
        return 0
    print("已更新：" + (", ".join(changed) if changed else "无变化"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
