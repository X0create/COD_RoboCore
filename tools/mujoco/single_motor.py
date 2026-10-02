"""Conda MuJoCo → 原有 C PID DLL → 虚拟输出轴；不连接实际硬件。"""

import argparse
import csv
import ctypes
import math
from pathlib import Path
import time

import mujoco


ROOT = Path(__file__).resolve().parents[2]
CONTROL_DT_S = 0.001
# 仿真专用参数；ki 为每次误差累加的系数，不能再乘 dt。
KP, KI, KD, INTEGRAL_LIMIT, TORQUE_LIMIT_NM = 0.12, 0.0004, 0.0, 5000.0, 0.5
# 每段结束时刻、目标 rad/s、仿真使能。关闭时是零力矩滑行，不是锁住转轴。
PHASES = ((0.25, 0.0, False), (2.5, 10.0, True), (5.0, -5.0, True), (6.0, 0.0, False))


def reference(time_s):
    for end_s, target_rad_s, enabled in PHASES:
        if time_s < end_s:
            return target_rad_s, enabled
    return 0.0, False


def load_controller(path):
    lib = ctypes.CDLL(str(path.resolve()))
    lib.pid_bridge_init.argtypes = [ctypes.c_float] * 5
    lib.pid_bridge_init.restype = None
    lib.pid_bridge_step.argtypes = [ctypes.c_float, ctypes.c_float, ctypes.c_int]
    lib.pid_bridge_step.restype = ctypes.c_float
    lib.pid_bridge_init(KP, KI, KD, INTEGRAL_LIMIT, TORQUE_LIMIT_NM)
    return lib


def run(model, data, controller, viewer=None):
    joint = model.joint("shaft")
    dof = int(joint.dofadr[0])
    actuator = model.actuator("drive").id
    rows = []
    total_steps = round(PHASES[-1][0] / CONTROL_DT_S)
    wall_start = time.perf_counter()
    # 渲染约 60 Hz，控制和物理始终按固定仿真步长推进；不依赖 Windows sleep 的精度。
    render_steps = 16
    for step in range(total_steps):
        if viewer is not None and step % render_steps == 0 and not viewer.is_running():
            break
        time_s = step * CONTROL_DT_S
        target, enabled = reference(time_s)
        measure = float(data.qvel[dof])
        torque = float(controller.pid_bridge_step(target, measure, int(enabled)))
        data.ctrl[actuator] = torque
        # 每行的反馈为施加本周期力矩前的值，时间与 target / ctrl 对齐。
        rows.append((time_s, target, measure, torque, int(enabled)))
        mujoco.mj_step(model, data)
        if viewer is not None and (step + 1) % render_steps == 0:
            viewer.sync()
            remaining = wall_start + (step + 1) * CONTROL_DT_S - time.perf_counter()
            if remaining > 0:
                time.sleep(remaining)
    return rows


def check_result(rows):
    # 检查整条闭环的行为，能发现 DLL 没接上、方向反了、限幅或开关失效。
    if len(rows) != round(PHASES[-1][0] / CONTROL_DT_S):
        raise RuntimeError("Simulation ended before the profile completed")
    if not all(math.isfinite(value) for row in rows for value in row):
        raise RuntimeError("Non-finite simulation state")
    if max(abs(row[3]) for row in rows) > TORQUE_LIMIT_NM + 1e-6:
        raise RuntimeError("Torque limit exceeded")
    if any(row[3] != 0.0 for row in rows if not row[4]):
        raise RuntimeError("Disabled shaft received torque")
    for index in (1, 2):
        end, target, _ = PHASES[index]
        errors = [abs(row[2] - target) for row in rows if end - 0.2 <= row[0] < end]
        error = max(errors)
        print(f"Target {target:+.1f} rad/s: final 0.2 s max error {error:.6f} rad/s")
        if error > 0.15:
            raise RuntimeError("Speed tracking failed")
    print("PASS: forward/reverse tracking, torque limit, disabled zero torque")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headless", action="store_true", help="Run without a graphics window")
    parser.add_argument("--check", action="store_true", help="Verify the full demo profile")
    parser.add_argument("--dll", type=Path, default=ROOT / "build/mujoco-windows/pid_bridge.dll")
    parser.add_argument("--csv", type=Path, default=ROOT / "build/mujoco-windows/single_motor.csv")
    args = parser.parse_args()
    controller = load_controller(args.dll)
    model = mujoco.MjModel.from_xml_path(str(Path(__file__).with_suffix(".xml")))
    model.opt.timestep = CONTROL_DT_S
    # 力矩限幅只在上面的仿真参数中定义，模型采用同一值。
    model.actuator_ctrllimited[model.actuator("drive").id] = True
    model.actuator_ctrlrange[model.actuator("drive").id] = (-TORQUE_LIMIT_NM, TORQUE_LIMIT_NM)
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    print(f"MuJoCo {mujoco.__version__}; C PID DLL: {args.dll}; dt={CONTROL_DT_S} s")
    if args.headless:
        rows = run(model, data, controller)
    else:
        from mujoco import viewer as mujoco_viewer

        with mujoco_viewer.launch_passive(model, data) as viewer:
            rows = run(model, data, controller, viewer)
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(("time_s", "target_rad_s", "measure_rad_s", "torque_nm", "enabled"))
        writer.writerows(rows)
    print(f"CSV: {args.csv}")
    if args.check:
        check_result(rows)


if __name__ == "__main__":
    main()
