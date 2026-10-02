"""对角线半舵半全向底盘：生成 MJCF；几何 / 质量均为仿真占位值。"""

from dataclasses import dataclass
import math
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zlib


WHEELS = ("lf", "lb", "rb", "rf")
SNAPSHOT_SIZE = (960, 640)


@dataclass(frozen=True)
class SentrySpec:
    radius_m: float = 0.076
    half_wheelbase_m: float = 0.22
    half_track_m: float = 0.22
    chassis_mass_kg: float = 20.0
    # 0 = 左前 / 右后舵轮；1 = 左后 / 右前舵轮。
    diagonal: int = 0
    roller_count: int = 16

    def is_steer(self, wheel):
        return wheel % 2 == self.diagonal


def vec(*values):
    return " ".join(f"{value:.9g}" for value in values)


def add(parent, tag, **attrs):
    return ET.SubElement(parent, tag, {key: str(value) for key, value in attrs.items()})


def build_model(spec, path):
    root = ET.Element("mujoco", model="27 sentry - diagonal steer + omni (simplified)")
    add(root, "compiler", angle="radian")
    add(root, "option", integrator="implicitfast", cone="elliptic", iterations="50")
    add(add(root, "visual"), "global", offwidth=SNAPSHOT_SIZE[0], offheight=SNAPSHOT_SIZE[1])
    defaults = add(root, "default")
    add(defaults, "geom", friction="0.9 0.002 0.0001", condim="3", solref="0.008 1")
    add(defaults, "joint", damping="0.001", armature="0.0001")
    assets = add(root, "asset")
    add(assets, "texture", name="checker", type="2d", builtin="checker", width="256",
        height="256", rgb1="0.15 0.18 0.21", rgb2="0.3 0.34 0.38")
    add(assets, "material", name="floor", texture="checker", texrepeat="12 12",
        texuniform="true")
    world = add(root, "worldbody")
    add(world, "light", pos="0 0 4", directional="true")
    # 接触只发生在轮子 / 车架与地面之间，避免相邻滚子互相碰撞。
    add(world, "geom", name="floor", type="plane", size="6 6 0.1", material="floor",
        contype="1", conaffinity="2")
    body = add(world, "body", name="chassis", pos=vec(0, 0, spec.radius_m + 0.073))
    add(body, "freejoint", name="base")
    add(body, "geom", type="box", size="0.17 0.15 0.035", mass=spec.chassis_mass_kg,
        rgba="0.12 0.18 0.24 1", contype="2", conaffinity="1")
    add(body, "geom", type="box", pos="0 0 0.043", size="0.155 0.135 0.008", mass="0",
        rgba="0.7 0.77 0.81 1", contype="0", conaffinity="0")
    for start, end in (((0.05, 0, 0.055), (0.13, 0, 0.055)),
                       ((0.10, 0.025, 0.055), (0.13, 0, 0.055)),
                       ((0.10, -0.025, 0.055), (0.13, 0, 0.055))):
        add(body, "geom", type="capsule", fromto=vec(*start, *end), size="0.006", mass="0",
            rgba="1 0.2 0.15 1", contype="0", conaffinity="0")
    motors = add(root, "actuator")
    locations = ((1, 1), (-1, 1), (-1, -1), (1, -1))
    for i, name in enumerate(WHEELS):
        x = locations[i][0] * spec.half_wheelbase_m
        y = locations[i][1] * spec.half_track_m
        add(body, "geom", type="capsule", fromto=vec(x * 0.5, y * 0.5, 0, x, y, -0.055),
            size="0.017", mass="0.1", rgba="0.25 0.3 0.35 1", contype="0", conaffinity="0")
        mount = add(body, "body", name=f"mount_{name}", pos=vec(x, y, -0.07))
        steer = spec.is_steer(i)
        if steer:
            add(mount, "joint", name=f"steer_{name}", type="hinge", axis="0 0 1",
                damping="0.04", armature="0.008")
            add(mount, "geom", type="box", pos="0 0 0.04", size="0.035 0.04 0.015",
                mass="0.25", rgba="1 0.5 0.1 1", contype="0", conaffinity="0")
            add(motors, "motor", name=f"steer_motor_{name}", joint=f"steer_{name}", gear="1")
        else:
            # 全向轮沿中心圆的切向安装；正转对应底盘逆时针方向。
            angle = math.atan2(x, -y)
            mount.set("quat", vec(math.cos(angle / 2), 0, 0, math.sin(angle / 2)))
        wheel = add(mount, "body", name=f"wheel_{name}")
        add(wheel, "joint", name=f"drive_{name}", type="hinge", axis="0 1 0",
            damping="0.001", armature="0.0005")
        radius = spec.radius_m if steer else spec.radius_m - 0.012
        add(wheel, "geom", type="cylinder", size=vec(radius, 0.021),
            quat="0.707106781 0.707106781 0 0", mass="0.65",
            rgba="1 0.45 0.05 1" if steer else "0.1 0.6 0.95 1",
            contype="2" if steer else "0", conaffinity="1" if steer else "0")
        add(wheel, "geom", type="box", pos=vec(radius * 0.55, 0.024, 0),
            size="0.025 0.004 0.006", mass="0", rgba="1 1 1 1", contype="0", conaffinity="0")
        if not steer:
            # 真实自由滚子关节提供横向滚动，没有给普通轮子强行设底盘速度。
            for roller in range(spec.roller_count):
                angle = roller * 2 * math.pi / spec.roller_count
                roller_body = add(wheel, "body", name=f"roller_{name}_{roller}",
                                  pos=vec(radius * math.sin(angle), 0, radius * math.cos(angle)),
                                  quat=vec(math.cos(angle / 2), 0, math.sin(angle / 2), 0))
                add(roller_body, "joint", type="hinge", axis="1 0 0", damping="0.00001",
                    armature="0.000001")
                add(roller_body, "geom", type="ellipsoid", size="0.018 0.012 0.012",
                    mass="0.015", rgba="0.8 0.84 0.87 1", contype="2", conaffinity="1")
        add(motors, "motor", name=f"drive_motor_{name}", joint=f"drive_{name}", gear="1")
    ET.indent(root, space="  ")
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)
    return path


def write_png(path, pixels):
    """保存 MuJoCo 渲染结果；仅用标准库，不为截图安装图像包。"""
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))
    height, width, _ = pixels.shape
    scanlines = b"".join(b"\x00" + row.tobytes() for row in pixels)
    content = b"\x89PNG\r\n\x1a\n"
    content += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    content += chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b"")
    Path(path).write_bytes(content)
