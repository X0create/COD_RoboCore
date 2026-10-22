#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Windows Git worktree 的 .git 是盘符路径；只修正本次进程环境，不改仓库 / 系统配置。
if [[ -f .git ]]; then
    rm_git_pointer=$(head -n 1 .git)
    rm_git_pointer=${rm_git_pointer#gitdir: }
    rm_git_pointer=${rm_git_pointer%$'\r'}
    if [[ "$rm_git_pointer" == [A-Za-z]:/* ]]; then
        export GIT_DIR="$(wslpath -u "$rm_git_pointer")"
        export GIT_WORK_TREE="$PWD"
    fi
fi
out=build/timing_20261021
mkdir -p "$out"
python3 tools/format.py 05_platform/trace/trace.h 05_platform/trace/trace.c \
    05_platform/trace/trace_stm32h7.c tests/host/05_platform/test_trace.c \
    01_applic/system/motor_bench_main.c 01_applic/tasks/motor_bench_task.c \
    01_applic/tasks/motor_bench_rx_task.c tests/host/01_applic/test_sysid_timing.c \
    01_applic/modules/motor_bench/telemetry/sysid/sysid.c
for preset in host-tests h723-m3508-timing h723-m3508 h723-debug c-debug; do
    cmake --preset "$preset" > "$out/${preset}_configure.log" 2>&1
    cmake --build --preset "$preset" -j 6 > "$out/${preset}_build.log" 2>&1
    if grep -n 'warning:' "$out/${preset}_build.log"; then exit 1; fi
    echo "PASS build $preset (zero warnings)"
done
ctest --preset host-tests --output-on-failure
python3 tools/keil_sync.py --check
python3 -B tests/test_timing_trace.py
python3 -B tools/check_timing.py
python3 tools/format.py --check
