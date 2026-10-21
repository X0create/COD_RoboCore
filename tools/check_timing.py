"""WSL offline checks; invoke via bash tools/check_timing.sh (no hardware access)."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

repo = Path(__file__).resolve().parents[1]
out = repo / "build/timing_20261021"


def command(*args):
    return subprocess.check_output(args, cwd=repo).decode("utf-8")


def stripped(source):
    return re.sub(r"(/\* USER CODE BEGIN ([^\n]+?) \*/).*?(/\* USER CODE END \2 \*/)",
                  r"\1\3", source.replace("\r\n", "\n"), flags=re.S)


for rel in ("06_boards/dm_mc02_h723/Core/Inc/FreeRTOSConfig.h",
            "06_boards/dm_mc02_h723/Core/Src/stm32h7xx_it.c"):
    before = command("git", "show", "HEAD:" + rel)
    assert stripped(before) == stripped((repo / rel).read_text()), f"Outside USER CODE: {rel}"

irq_source = (repo / "06_boards/dm_mc02_h723/Core/Src/stm32h7xx_it.c").read_text()
irq_names = re.findall(r"void (\w+)_IRQHandler\(void\)", irq_source)
platform_source = (repo / "05_platform/trace/trace_stm32h7.c").read_text()
coverage_table = re.search(r"covered_irqs\[\] = \{(.*?)\};", platform_source, re.S)[1]
covered_names = re.findall(r"(\w+)_IRQn", coverage_table)
assert sorted(irq_names) == sorted(covered_names)
assert irq_source.count("RM_TRACE_EVENT(RM_TRACE_IRQ_IN") == len(irq_names) + 1
assert irq_source.count("RM_TRACE_EVENT(RM_TRACE_IRQ_OUT") == len(irq_names) + 1

diag = repo / "build/Linux/h723-m3508-timing"
cache = (diag / "CMakeCache.txt").read_text()
nm = re.search(r"^CMAKE_NM:FILEPATH=(.+)$", cache, re.M)[1]
elf = diag / "COD_RoboCore.elf"
symbols = command(nm, "-S", "--defined-only", str(elf))
match = re.search(r"^([0-9a-f]+)\s+([0-9a-f]+)\s+[BbDd]\s+rm_trace$", symbols, re.M)
assert match is not None
address, size = (int(x, 16) for x in match.groups())
assert size == 32928
for symbol in ("rm_trace_ready", "rm_trace_emit", "rm_trace_control_state", "SysTick_Handler"):
    assert re.search(r"\b" + symbol + r"$", symbols, re.M), symbol
entries = json.loads((diag / "compile_commands.json").read_text())
assert all("-DRM_MOTOR_BENCH_TIMING=1" in entry["command"] for entry in entries)
kernel_object = diag / "stm32cubemx/CMakeFiles/FreeRTOS.dir/__/__/Middlewares/Third_Party/FreeRTOS/Source/tasks.c.obj"
undefined = command(nm, "-u", str(kernel_object))
assert "rm_trace_ready" in undefined and "rm_trace_emit" in undefined
wrapper_object = kernel_object.parent / "CMSIS_RTOS_V2/cmsis_os2.c.obj"
assert "SysTick_Handler" not in command(nm, "--defined-only", str(wrapper_object))
for preset in ("h723-m3508", "h723-debug", "c-debug"):
    build = repo / "build/Linux" / preset
    normal = command(nm, "--defined-only", str(build / "COD_RoboCore.elf"))
    assert "rm_trace" not in normal, preset
    entries_normal = json.loads((build / "compile_commands.json").read_text())
    assert all("-DRM_MOTOR_BENCH_TIMING" not in entry["command"] for entry in entries_normal)
    assert not any("/trace/trace" in entry["file"] for entry in entries_normal)
for log in out.glob("*_build.log"):
    assert "warning:" not in log.read_text().lower(), log.name

fixture = out / "c_fixture.bin"
command(str(repo / "build/Linux/host-tests/tests/host/test_trace"), "--dump", str(fixture))
sys.path.insert(0, str(repo / "tools"))
import decode_timing_trace as dec
report, events = dec.decode(fixture.read_bytes())
assert report["retained_events"] == 16
assert report["control_cycles"][0]["ready_to_first_in_us"] == 349
assert report["control_cycles"][0]["first_in_to_entry_us"] == 50
assert report["control_cycles"][0]["irq_overlap_us"] == 299
assert report["irq_union_us"] == 300
assert report["frames"][0]["type"] == 0x10 and report["frames"][0]["accepted"]
(out / "c_fixture_report.json").write_text(json.dumps(report, indent=2) + "\n")
manifest = {"elf": str(elf.relative_to(repo)), "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
            "source_commit_at_check": command("git", "rev-parse", "HEAD").strip(),
            "source_dirty_at_check": bool(command("git", "status", "--porcelain").strip()),
            "symbol": "rm_trace", "address": f"0x{address:08X}", "bytes": size,
            "external_irq_pairs": len(irq_names), "core_irq_pairs": ["SysTick"],
            "not_measured": ["probe cost", "masked interval", "physical stopping", "runtime timing"],
            "note": "ELF identity/address only; not evidence of the firmware actually flashed"}
(out / "capture_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
# Preparation only: never execute this file in the checker. User must confirm frozen/no-motion first.
(out / "export_trace.jlink").write_text(
    f"h\nSaveBin build/timing_20261021/timing_capture.bin 0x{address:08X} 0x{size:X}\nexit\n")
print(json.dumps(manifest, indent=2))
print("PASS: USER CODE scope, kernel hooks, 31 IRQ pairs, ordinary ELF isolation and actual C -> Python fixture")
