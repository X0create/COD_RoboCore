"""27 哨兵半舵半全向底盘：MuJoCo 反馈 → 原有 C 底盘 → 最终电机力矩。"""

import argparse
from collections import deque
import csv
import ctypes as ct
from dataclasses import dataclass
import math
from pathlib import Path
import threading
import time

import mujoco
import numpy as np

from sentry_model import SNAPSHOT_SIZE, SentrySpec, WHEELS, build_model, write_png


ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = ROOT / "build/mujoco-windows"
Float4 = ct.c_float * 4
Float3 = ct.c_float * 3


class BridgeInput(ct.Structure):
    _fields_ = [("now_us", ct.c_uint64), ("cmd", Float3),
                ("drive_angle_rad", Float4), ("drive_speed_rad_s", Float4),
                ("steer_angle_rad", Float4), ("steer_speed_rad_s", Float4),
                ("feedback_mask", ct.c_uint32), ("armed", ct.c_uint32),
                ("rc_online", ct.c_uint32), ("imu_ready", ct.c_uint32)]


class BridgeOutput(ct.Structure):
    _fields_ = [("drive_torque_nm", Float4), ("steer_torque_nm", Float4),
                ("drive_target_rad_s", Float4), ("heading_target_rad", Float4),
                ("measure_velocity", Float3), ("target_velocity", Float3),
                ("mode", ct.c_uint32), ("all_online", ct.c_uint32)]


@dataclass(frozen=True)
class Phase:
    name: str
    end_s: float
    cmd: tuple = (0.0, 0.0, 0.0)
    armed: bool = True
    feedback_mask: int = 0xFF
    rc_online: bool = True


MOTION_PHASES = (Phase("settle", 0.5, armed=False),
                 Phase("forward", 3.5, (0.5, 0.0, 0.0)),
                 Phase("left", 6.5, (0.0, 0.5, 0.0)),
                 Phase("turn", 9.5, (0.0, 0.0, 0.7)),
                 Phase("combined", 12.5, (0.35, 0.2, 0.25)))
DEMO_PHASES = MOTION_PHASES + (Phase("stop", 14.0, armed=False),)
fault_start_s = MOTION_PHASES[-1].end_s
CHECK_PHASES = MOTION_PHASES + (
    Phase("wheel_offline", fault_start_s + 1.0, (0.5, 0.0, 0.0), feedback_mask=0xFE),
    Phase("rc_loss", fault_start_s + 1.5, (0.5, 0.0, 0.0), rc_online=False),
    Phase("rc_restored", fault_start_s + 2.0, (0.5, 0.0, 0.0)),
    Phase("rearm_down", fault_start_s + 2.5, armed=False),
    Phase("rearmed", fault_start_s + 3.0),
    Phase("stop", fault_start_s + 3.5, armed=False))


def phase_at(time_s, phases):
    for phase in phases:
        if time_s < phase.end_s:
            return phase
    return Phase("stop", math.inf, armed=False)


class Controller:
    def __init__(self, spec, path):
        self.lib = ct.CDLL(str(path.resolve()))
        for name, structure in (("input", BridgeInput), ("output", BridgeOutput)):
            query = getattr(self.lib, f"chassis_bridge_{name}_size")
            query.argtypes = []
            query.restype = ct.c_size_t
            if query() != ct.sizeof(structure):
                raise RuntimeError("C / Python structure layout mismatch; rebuild the DLL")
        self.lib.chassis_bridge_period_us.argtypes = []
        self.lib.chassis_bridge_period_us.restype = ct.c_uint32
        self.period_us = self.lib.chassis_bridge_period_us()
        self.dt_s = self.period_us * 1e-6
        self.lib.chassis_bridge_init.argtypes = [ct.c_float] * 3 + [ct.c_int]
        self.lib.chassis_bridge_init.restype = ct.c_bool
        self.lib.chassis_bridge_step.argtypes = [ct.POINTER(BridgeInput), ct.POINTER(BridgeOutput)]
        self.lib.chassis_bridge_step.restype = None
        if not self.lib.chassis_bridge_init(spec.radius_m, spec.half_wheelbase_m,
                                            spec.half_track_m, spec.diagonal):
            raise RuntimeError("Chassis initialization failed")
        self.input = BridgeInput()
        self.output = BridgeOutput()


class Controls:
    """键盘只改变虚拟操纵输入，C 安全门仍在主线程统一决定能否动作。"""
    def __init__(self):
        self.lock = threading.Lock()
        self.manual = False
        self.cmd = [0.0, 0.0, 0.0]
        self.armed = False
        self.down_steps = 0
        self.held = set()

    def key(self, key, pressed=True):
        movement = "WSADQE"
        with self.lock:
            if key in map(ord, movement):
                if pressed:
                    self.manual = True
                    self.held.add(key)
                else:
                    self.held.discard(key)
                active = lambda char: int(ord(char) in self.held)
                speed_m_s, turn_rad_s = 0.5, 0.7
                self.cmd = [speed_m_s * (active("W") - active("S")),
                            speed_m_s * (active("A") - active("D")),
                            turn_rad_s * (active("Q") - active("E"))]
                return
            if not pressed or key not in (ord("X"), 32, 257, 335):
                return
            self.manual = True
            if key in (ord("X"), 32):
                self.held.clear()
                self.cmd = [0.0, 0.0, 0.0]
                if key == 32:
                    self.armed = False
            else:
                # 主线程先提供两个周期的“下”，再“中”，走原来的解锁边沿。
                self.down_steps = 2
                self.armed = True

    def phase(self, auto_phase):
        with self.lock:
            if not self.manual:
                return auto_phase
            armed = self.armed and self.down_steps == 0
            if self.down_steps:
                self.down_steps -= 1
            return Phase("keyboard", math.inf, tuple(self.cmd), armed)


class Simulation:
    def __init__(self, spec, controller):
        path = build_model(spec, OUTPUT_DIR / "sentry_chassis.xml")
        self.model = mujoco.MjModel.from_xml_path(str(path))
        self.model.opt.timestep = controller.dt_s
        self.data = mujoco.MjData(self.model)
        self.spec = spec
        self.body = self.model.body("chassis").id
        self.drive_joints = [self.model.joint(f"drive_{name}") for name in WHEELS]
        self.steer_joints = [self.model.joint(f"steer_{name}") if spec.is_steer(i) else None
                             for i, name in enumerate(WHEELS)]
        self.drive_motors = [self.model.actuator(f"drive_motor_{name}").id for name in WHEELS]
        self.steer_motors = [self.model.actuator(f"steer_motor_{name}").id if spec.is_steer(i)
                             else None for i, name in enumerate(WHEELS)]
        self.velocity = np.zeros(6)
        mujoco.mj_forward(self.model, self.data)
        print(f"Model: {path}; {self.model.nu} actuators; "
              f"{2 * spec.roller_count} passive omni rollers")

    def step(self, step, phase, controller):
        m, d = self.model, self.data
        # 更新本时刻的机体坐标与速度，再读取反馈；不写底盘位姿或速度。
        mujoco.mj_forward(m, d)
        if not np.isfinite(d.qpos).all() or not np.isfinite(d.qvel).all():
            raise RuntimeError("Physics state is non-finite")
        inp = controller.input
        inp.now_us = step * controller.period_us
        inp.cmd[:] = phase.cmd
        inp.armed = int(phase.armed)
        inp.rc_online = int(phase.rc_online)
        inp.imu_ready = 1
        inp.feedback_mask = phase.feedback_mask
        for i in range(4):
            joint = self.drive_joints[i]
            inp.drive_angle_rad[i] = d.qpos[joint.qposadr[0]]
            inp.drive_speed_rad_s[i] = d.qvel[joint.dofadr[0]]
            joint = self.steer_joints[i]
            if joint is not None:
                inp.steer_angle_rad[i] = d.qpos[joint.qposadr[0]]
                inp.steer_speed_rad_s[i] = d.qvel[joint.dofadr[0]]
        # 反馈编码会转换成 int16 rpm；在进入 C 的这个边界保证转换范围。
        if max(abs(value) for value in (*inp.drive_speed_rad_s, *inp.steer_speed_rad_s)) >= 100:
            raise RuntimeError("Virtual motor speed exceeds the CAN adapter range")
        out = controller.output
        controller.lib.chassis_bridge_step(ct.byref(inp), ct.byref(out))
        for i in range(4):
            d.ctrl[self.drive_motors[i]] = out.drive_torque_nm[i]
            if self.steer_motors[i] is not None:
                d.ctrl[self.steer_motors[i]] = out.steer_torque_nm[i]
        # XBODY 是车架坐标；BODY 的局部方向是惯性主轴，可能与车架 X/Y/Z 不同。
        mujoco.mj_objectVelocity(m, d, mujoco.mjtObj.mjOBJ_XBODY, self.body, self.velocity, 1)
        # CSV 所有状态均为施加本周期力矩前的反馈，力矩是本周期最终发送值。
        record = (step * controller.dt_s, *phase.cmd, *self.velocity[3:5], self.velocity[2],
                  int(out.mode), int(out.all_online), *d.xpos[self.body][:2],
                  *out.target_velocity, *out.drive_torque_nm, *out.steer_torque_nm,
                  *out.drive_target_rad_s, *out.heading_target_rad)
        mujoco.mj_step(m, d)
        return record

    def snapshot(self, path):
        camera = mujoco.MjvCamera()
        camera.lookat[:] = self.data.xpos[self.body]
        camera.distance = 1.8
        camera.azimuth = 135
        camera.elevation = -45
        with mujoco.Renderer(self.model, height=SNAPSHOT_SIZE[1], width=SNAPSHOT_SIZE[0]) as renderer:
            renderer.update_scene(self.data, camera=camera)
            path.parent.mkdir(parents=True, exist_ok=True)
            write_png(path, renderer.render())
        print(f"Preview: {path}")


CSV_COLUMNS = ("time_s", "cmd_vx_m_s", "cmd_vy_m_s", "cmd_wz_rad_s", "vx_m_s", "vy_m_s",
               "wz_rad_s", "mode", "all_online", "x_m", "y_m", "target_vx_m_s",
               "target_vy_m_s", "target_wz_rad_s")
CSV_COLUMNS += tuple(f"{prefix}_{wheel}" for prefix in (
    "drive_torque_nm", "steer_torque_nm", "drive_target_rad_s", "heading_target_rad")
    for wheel in WHEELS)


def check_result(rows, sim):
    data = np.asarray(rows)
    if not np.isfinite(data).all() or any(warning.number for warning in sim.data.warning):
        raise RuntimeError("Non-finite state or MuJoCo physics warning")
    for phase in MOTION_PHASES[1:]:
        tail = data[(data[:, 0] >= phase.end_s - 0.4) & (data[:, 0] < phase.end_s)]
        error = np.sqrt(np.mean((tail[:, 4:7] - phase.cmd) ** 2, axis=0))
        print(f"{phase.name}: body velocity RMS error {error.round(4)} [m/s, m/s, rad/s]")
        if np.any(error > (0.15, 0.15, 0.2)):
            raise RuntimeError(f"Physical chassis tracking failed: {phase.name}")
    for phase in CHECK_PHASES:
        tail = data[(data[:, 0] >= phase.end_s - 0.2) & (data[:, 0] < phase.end_s)]
        if phase.name == "wheel_offline":
            if np.any(tail[:, 8] != 0) or np.any(tail[:, 14] != 0):
                raise RuntimeError("Offline wheel did not receive zero torque")
            if np.max(np.abs(tail[:, 11:14])) > 0.01:
                raise RuntimeError("Offline mechanism did not ramp its target to zero")
        if phase.name in ("settle", "rc_loss", "rc_restored", "rearm_down", "stop"):
            if np.any(tail[:, 7] == 2) or np.any(tail[:, 14:22] != 0):
                raise RuntimeError("Safety gate stop or rearm requirement failed")
        if phase.name == "rearmed" and np.any(tail[:, 7] != 2):
            raise RuntimeError("Explicit rearm did not enter Manual")
    if np.max(np.abs(data[:, 14:18])) > 6.0 or np.max(np.abs(data[:, 18:22])) > 2.001:
        raise RuntimeError("Motor torque bound exceeded")
    print("PASS: physical chassis motion, wheel offline stop, RC loss, explicit rearm, torque bounds")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--diagonal", choices=("lf_rb", "lb_rf"), default="lf_rb")
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--csv", type=Path, default=OUTPUT_DIR / "sentry_chassis.csv")
    parser.add_argument("--dll", type=Path, default=OUTPUT_DIR / "chassis_bridge.dll")
    args = parser.parse_args()
    spec = SentrySpec(diagonal=0 if args.diagonal == "lf_rb" else 1)
    controller = Controller(spec, args.dll)
    sim = Simulation(spec, controller)
    if args.snapshot:
        sim.snapshot(args.snapshot)
    phases = CHECK_PHASES if args.check else DEMO_PHASES
    total_steps = round(phases[-1].end_s / controller.dt_s)
    # 交互时只保留最近一段演示时长的数据，长期开窗口不会让日志无限占用内存。
    rows = deque(maxlen=total_steps)
    if args.headless or args.check:
        for step in range(total_steps):
            rows.append(sim.step(step, phase_at(step * controller.dt_s, phases), controller))
    else:
        from chassis_viewer import ChassisViewer

        controls = Controls()
        print("Auto: forward / left / turn / combined / stop. Keyboard: Enter=arm; "
              "hold W/S=forward/back; A/D=left/right; Q/E=turn; "
              "release=zero target; X=zero target; Space=stop output.")
        with ChassisViewer(sim.model, sim.data, sim.body, controls) as viewer:
            step = 0
            wall_start = time.perf_counter()
            render_steps = 16
            while viewer.is_running():
                viewer.poll()
                if not viewer.is_running():
                    break
                for _ in range(render_steps):
                    phase = controls.phase(phase_at(step * controller.dt_s, phases))
                    rows.append(sim.step(step, phase, controller))
                    step += 1
                viewer.render(phase, ("Init", "Safe", "Manual")[controller.output.mode])
                remaining = wall_start + step * controller.dt_s - time.perf_counter()
                if remaining > 0:
                    time.sleep(remaining)
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(CSV_COLUMNS)
        writer.writerows(rows)
    print(f"CSV: {args.csv}")
    if args.check:
        check_result(rows, sim)


if __name__ == "__main__":
    main()
