#!/usr/bin/env python3
"""生成 README 用的三张结构图（docs/images/*.svg）。

改图时改这里再运行 `python tools/gen_readme_diagrams.py`，不要手改 SVG。
配色取自数据可视化参考调色板（分类色 6 个，已用校验脚本检查色盲区分度）；
图自带浅色底板，GitHub / Gitee 的浅色和深色主题下都能看清。
"""
from pathlib import Path
from xml.sax.saxutils import escape

OUT = Path(__file__).resolve().parent.parent / "docs" / "images"

FONT = "'PingFang SC','Microsoft YaHei','Noto Sans CJK SC','Source Han Sans SC',sans-serif"
SURFACE = "#fcfcfb"
BORDER = "#e4e3df"
INK = "#0b0b0b"
INK2 = "#52514e"
MUTED = "#8a8984"

# 分类色（参考调色板第 1、3、2、7、4、5 槽）
BLUE, AQUA, ORANGE, VIOLET, YELLOW, MAGENTA = (
    "#2a78d6", "#1baf7a", "#eb6834", "#4a3aa7", "#eda100", "#e87ba4")
GRAY = "#8a8984"
GOOD, CRITICAL = "#0ca30c", "#d03b3b"  # 状态色，只和文字标签一起用


def tint(hex_color, a):
    """把颜色和白色按比例混合，a=颜色占比"""
    h = hex_color.lstrip("#")
    r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4))
    mix = lambda c: round(c * a + 255 * (1 - a))
    return "#{:02x}{:02x}{:02x}".format(mix(r), mix(g), mix(b))


def text_w(s, size):
    """估算文字宽度：中文按 1 个字号，其余按 0.62 个字号"""
    return sum(size if ord(ch) > 0x2E80 else size * 0.62 for ch in s)


class Svg:
    def __init__(self, w, h, title):
        self.w, self.h, self.parts = w, h, []
        self.parts.append(
            f'<rect x="0.5" y="0.5" width="{w - 1}" height="{h - 1}" rx="12" '
            f'fill="{SURFACE}" stroke="{BORDER}"/>')
        self.text(24, 34, title, 17, INK, bold=True)

    def rect(self, x, y, w, h, fill, stroke=BORDER, rx=8, dash=None, sw=1):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" '
                          f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def text(self, x, y, s, size=13, color=INK, bold=False, anchor="start"):
        wt = ' font-weight="600"' if bold else ""
        self.parts.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}"{wt} '
                          f'text-anchor="{anchor}">{escape(s)}</text>')

    def line(self, x1, y1, x2, y2, color=MUTED, dash=None, arrow=True, sw=1.5):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        m = ' marker-end="url(#ah)"' if arrow else ""
        self.parts.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" '
                          f'stroke-width="{sw}"{d}{m}/>')

    def path(self, d, color=MUTED, dash=None, arrow=True, sw=1.5):
        ds = f' stroke-dasharray="{dash}"' if dash else ""
        m = ' marker-end="url(#ah)"' if arrow else ""
        self.parts.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{sw}"{ds}{m}/>')

    def chip(self, x, y, s, color, planned=False):
        w = text_w(s, 12) + 16
        self.rect(x, y, w, 22, "#ffffff", tint(color, 0.55), rx=11,
                  dash="3 3" if planned else None)
        self.text(x + w / 2, y + 15.5, s, 12, MUTED if planned else INK, anchor="middle")
        return w

    def chips(self, x, y, right, items, color):
        """从 (x, y) 开始排小标签，超过 right 换行；items 里以 * 开头的是规划中的"""
        cx, cy = x, y
        for it in items:
            planned = it.startswith("*")
            s = it.lstrip("*")
            w = text_w(s, 12) + 16
            if cx + w > right:
                cx, cy = x, cy + 28
            self.chip(cx, cy, s, color, planned)
            cx += w + 6

    def save(self, name):
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}" font-family="{FONT}">'
                f'<defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
                f'markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" '
                f'fill="{MUTED}"/></marker></defs>')
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / name).write_text(head + "".join(self.parts) + "</svg>\n", encoding="utf-8", newline="\n")


def architecture():
    W, H = 960, 470
    s = Svg(W, H, "分层结构")
    s.text(24, 54, "左侧五层只能从上往下调用；右侧模块各层都可以使用。虚线框为规划中、尚无代码。", 12.5, INK2)

    layers = [
        ("app/<兵种>", "兵种", "这台车怎么组装、怎么控制", BLUE,
         ["robot.c：对象 · 上电 · 任务表", "*_task.c", "config.h"]),
        ("app/<机构>", "机构", "一个机构的完整逻辑", AQUA,
         ["ins 惯导", "底盘", "安全门", "*云台", "*发射", "*轮腿"]),
        ("devices", "设备驱动", "协议字节 ⇄ 物理量", ORANGE,
         ["DJI 电机", "达妙电机", "BMI088", "DR16", "VT13 图传", "视觉帧", "电池", "蜂鸣器"]),
        ("platform", "外设接口", "唯一直接操作硬件的一层", VIOLET,
         ["can", "uart", "spi", "pwm", "adc", "usb_cdc", "time", "status_led"]),
        ("boards", "板级", "CubeMX 生成代码、链接脚本", GRAY,
         ["DM-MC02 · STM32H723", "*C 板 · STM32F407", "HAL", "FreeRTOS", "CMSIS", "USB Device"]),
    ]
    x0, x1, y, rh, gap = 24, 640, 74, 66, 14
    for i, (name, cn, role, color, items) in enumerate(layers):
        s.rect(x0, y, x1 - x0, rh, tint(color, 0.08), tint(color, 0.35))
        s.rect(x0, y, 6, rh, color, color, rx=3)
        s.text(x0 + 20, y + 28, name, 15, INK, bold=True)
        s.text(x0 + 20 + text_w(name, 15) + 8, y + 28, cn, 13, INK2)
        s.text(x0 + 20, y + 48, role, 12, INK2)
        need = sum(text_w(t.lstrip("*"), 12) + 22 for t in items)
        s.chips(x0 + 232, y + 22 if need <= x1 - 12 - (x0 + 232) else y + 8, x1 - 12, items, color)
        if i < len(layers) - 1:
            s.line(x0 + 110, y + rh + 1, x0 + 110, y + rh + gap - 1)
        y += rh + gap

    # 右侧共用模块
    rx, rw = 664, 272
    s.text(rx, 68, "各层都可以用", 13, INK2, bold=True)
    shared = [
        ("algorithm", "纯计算，不碰硬件", ["PID", "斜坡", "低通", "卡尔曼", "四元数 EKF", "矩阵", "轮组运动学"], YELLOW, False),
        ("core", "基础设施", ["话题与消息 imu_state · rc_state …", "设备看门狗", "RTT 日志", "任务封装"], YELLOW, False),
        ("tests/host", "电脑上的单元测试", ["假 CAN / SPI / PWM / 时钟", "替换 platform"], MAGENTA, True),
    ]
    cy, ch = 78, 88
    for name, role, items, color, dashed in shared:
        s.rect(rx, cy, rw, ch, tint(color, 0.08), tint(color, 0.45), dash="5 4" if dashed else None)
        s.text(rx + 14, cy + 22, name, 14, INK, bold=True)
        s.text(rx + 14 + text_w(name, 14) + 8, cy + 22, role, 12, INK2)
        s.chips(rx + 14, cy + 32, rx + rw - 10, items, color)
        cy += ch + 8
    s.save("architecture.svg")


def runtime():
    W, H = 960, 444
    s = Svg(W, H, "一个控制周期里数据怎么流动")
    s.text(24, 54, "中断只收数据；解析、解算、控制都在任务里。话题里的数据带写入时刻，读的一方按“最多多旧”判断丢失。", 12.5, INK2)

    top = 92
    # 1. 硬件输入
    hw = [("CAN1 / CAN2", "电机反馈"), ("UART5 / USART10", "DR16 · 图传"),
          ("USB", "视觉上位机"), ("SPI2", "BMI088 IMU")]
    hx, hwid, hh, hg = 24, 150, 48, 10
    s.text(hx, top - 8, "硬件输入", 12, INK2, bold=True)
    ys = []
    for i, (a, b) in enumerate(hw):
        yy = top + i * (hh + hg)
        ys.append(yy)
        s.rect(hx, yy, hwid, hh, "#ffffff", BORDER)
        s.text(hx + 12, yy + 20, a, 13, INK, bold=True)
        s.text(hx + 12, yy + 38, b, 12, INK2)

    # 2. 中断（前三路）
    ix, iw = 200, 124
    iy0, iy1 = ys[0], ys[2] + hh
    s.rect(ix, iy0, iw, iy1 - iy0, tint(CRITICAL, 0.07), tint(CRITICAL, 0.4))
    s.text(ix + iw / 2, iy0 + 30, "中断", 14, INK, bold=True, anchor="middle")
    for k, t in enumerate(["只收数据", "唤醒 comm_rx", "不做解析"]):
        s.text(ix + iw / 2, iy0 + 56 + k * 20, t, 12, INK2, anchor="middle")
    for i in range(3):
        s.line(hx + hwid, ys[i] + hh / 2, ix - 2, ys[i] + hh / 2)

    # 3. 任务
    tx, tw = 350, 186
    s.text(tx, top - 8, "接收与解算任务", 12, INK2, bold=True)
    s.rect(tx, iy0, tw, iy1 - iy0, tint(BLUE, 0.08), tint(BLUE, 0.4))
    s.rect(tx, iy0, 6, iy1 - iy0, BLUE, BLUE, rx=3)
    s.text(tx + 18, iy0 + 26, "comm_rx", 14, INK, bold=True)
    s.text(tx + 18 + text_w("comm_rx", 14) + 8, iy0 + 26, "优先级 4", 12, INK2)
    for k, t in enumerate(["有数据才运行", "交给对应设备解析", "校验通过才算在线", "发布遥控、电机反馈"]):
        s.text(tx + 18, iy0 + 52 + k * 22, t, 12, INK2)
    s.line(ix + iw, (iy0 + iy1) / 2, tx - 2, (iy0 + iy1) / 2)

    insy = ys[3] - 4
    insh = hh + 8 + 40
    s.rect(tx, insy, tw, insh, tint(BLUE, 0.08), tint(BLUE, 0.4))
    s.rect(tx, insy, 6, insh, BLUE, BLUE, rx=3)
    s.text(tx + 18, insy + 24, "ins", 14, INK, bold=True)
    s.text(tx + 18 + text_w("ins", 14) + 8, insy + 24, "1 kHz · 优先级 5", 12, INK2)
    for k, t in enumerate(["零偏标定 / 在线修正", "四元数 EKF · 恒温加热"]):
        s.text(tx + 18, insy + 48 + k * 22, t, 12, INK2)
    s.line(hx + hwid, ys[3] + hh / 2, tx - 2, ys[3] + hh / 2)

    # 4. 话题
    px, pw, ph = 562, 122, 40
    s.text(px, top - 8, "话题（数据 + 时刻）", 12, INK2, bold=True)
    topics = [("rc_state", "遥控，200 ms 过期"), ("电机反馈", "20 ms 过期"), ("imu_state", "姿态")]
    pys = [iy0 + 6, iy0 + 64, insy + 22]
    for (a, b), py in zip(topics, pys):
        s.rect(px, py, pw, ph + 8, tint(YELLOW, 0.1), tint(YELLOW, 0.55), rx=10)
        s.text(px + pw / 2, py + 19, a, 13, INK, bold=True, anchor="middle")
        s.text(px + pw / 2, py + 37, b, 11.5, INK2, anchor="middle")
    s.line(tx + tw, pys[0] + 24, px - 2, pys[0] + 24)
    s.line(tx + tw, pys[1] + 24, px - 2, pys[1] + 24)
    s.line(tx + tw, pys[2] + 24, px - 2, pys[2] + 24)

    # 5. control 任务
    cx, cw = 712, 224
    s.text(cx, top - 8, "control 任务 · 1 kHz · 优先级 3", 12, INK2, bold=True)
    steps = [("① 读话题快照", "过期的当作没有", BLUE), ("② 安全门", "判断能不能动", ORANGE),
             ("③ 子系统计算", "PID 等", BLUE), ("④ 全车停改写", "换成各电机的停机动作", ORANGE),
             ("⑤ 电机组打包", "经 CAN 发给电机", BLUE)]
    sy, sh, sg = top, 44, 8
    for i, (a, b, color) in enumerate(steps):
        yy = sy + i * (sh + sg)
        s.rect(cx, yy, cw, sh, tint(color, 0.08), tint(color, 0.45))
        s.rect(cx, yy, 5, sh, color, color, rx=2)
        s.text(cx + 16, yy + 19, a, 13, INK, bold=True)
        s.text(cx + 16, yy + 36, b, 11.5, INK2)
        if i < len(steps) - 1:
            s.line(cx + 30, yy + sh, cx + 30, yy + sh + sg - 1, sw=1.2)
    # 三个话题都汇到第①步
    jx = px + pw + 14
    for py in pys:
        s.path(f"M {px + pw} {py + 24} L {jx} {py + 24} L {jx} {sy + 22}", arrow=False)
    s.line(jx, sy + 22, cx - 2, sy + 22)

    # 6. 低优先级任务
    ay = 368
    s.text(24, ay - 8, "低优先级任务：只读话题，不参与控制", 12, INK2, bold=True)
    aux = [("daemon", "100 Hz · 优先级 2", "报告设备上线 / 离线", 24),
           ("heartbeat", "40 Hz · 优先级 1", "状态灯 · 蜂鸣器 · 电池 · 每秒日志", 300)]
    for name, meta, role, x in aux:
        s.rect(x, ay, 260, 58, tint(VIOLET, 0.07), tint(VIOLET, 0.35))
        s.rect(x, ay, 5, 58, VIOLET, VIOLET, rx=2)
        s.text(x + 16, ay + 24, name, 14, INK, bold=True)
        s.text(x + 16 + text_w(name, 14) + 8, ay + 24, meta, 12, INK2)
        s.text(x + 16, ay + 44, role, 12, INK2)
    # 话题 → 低优先级任务（虚线：只读）
    s.path(f"M {px + pw / 2} {pys[2] + ph + 8} L {px + pw / 2} {ay + 29} L {300 + 262} {ay + 29}",
           dash="4 4")
    s.save("runtime.svg")


def safety_gate():
    W, H = 960, 310
    s = Svg(W, H, "安全门：什么时候能动")
    s.text(24, 54, "安全门只管“全车停”。某个机构自己的设备离线，由那个子系统自己停下（机构停）。", 12.5, INK2)

    ny, nh = 124, 92
    nodes = [("Init", 40, 230, GRAY, "启动中", ["不能解锁", "等启动流程完成"]),
             ("Safe", 340, 230, CRITICAL, "全车停", ["每个电机执行自己的停机动作", "DJI 零力矩或失能，达妙阻尼"]),
             ("Manual", 686, 234, GOOD, "允许动作", ["解锁后 300 ms 内", "输出从 0 慢慢升上来"])]
    for name, x, nw, color, a, lines in nodes:
        s.rect(x, ny, nw, nh, tint(color, 0.08), tint(color, 0.5), rx=12, sw=1.5)
        s.rect(x, ny, 6, nh, color, color, rx=3)
        s.text(x + 20, ny + 28, name, 16, INK, bold=True)
        s.text(x + 20 + text_w(name, 16) + 10, ny + 28, a, 13, INK2, bold=True)
        for k, t in enumerate(lines):
            s.text(x + 20, ny + 54 + k * 20, t, 12, INK2)

    s.line(270, ny + nh / 2, 338, ny + nh / 2)
    s.text(304, ny + nh / 2 - 8, "启动完成", 12, INK2, anchor="middle")

    sx, mx = 455, 803
    s.path(f"M {sx} {ny - 2} C {sx} 84, {mx} 84, {mx} {ny - 2}")
    s.rect(474, 72, 312, 26, SURFACE, SURFACE, rx=4)
    s.text(630, 90, "解锁：遥控在线 + IMU 就绪，拨杆“下”→“中 / 上”", 12, INK, anchor="middle")

    s.path(f"M {mx} {ny + nh + 2} C {mx} 262, {sx} 262, {sx} {ny + nh + 2}")
    s.rect(462, 238, 336, 26, SURFACE, SURFACE, rx=4)
    s.text(630, 256, "急停（拨杆到“下”）· 遥控丢失 > 200 ms · IMU 未就绪", 12, INK, anchor="middle")

    s.text(24, 292, "回到 Safe 后必须重新拨杆才能解锁：遥控恢复、上电时拨杆已在上方，都不会自己动起来。", 12, INK2)
    s.save("safety_gate.svg")


def startup():
    W, H = 960, 330
    s = Svg(W, H, "上电后按什么顺序启动")
    s.text(24, 54, "前两行都在兵种的 robot.c 里。任何一步失败都停在 halt_on_init_failure()，调试器能看到停在哪。⑧ 之前安全门一直是 Init，不能解锁。", 12.5, INK2)

    lanes = [
        ("app_main", "调度器启动前，单线程", BLUE,
         [("① 时钟", "DWT 计数器自检"), ("② 日志", "RTT 初始化"),
          ("③ 初始化对象", "组装设备和机构"), ("④ 创建任务", "按任务表静态创建"),
          ("⑤ 启动调度器", "交给 FreeRTOS")]),
        ("startup 任务", "调度器启动后运行一次", AQUA,
         [("⑥ comm_rx_start", "打开 CAN / 串口 / USB"), ("⑦ ADC · 蜂鸣器", "启动音"),
          ("⑧ 系统就绪", "允许解锁"), ("⑨ 删除自己", "“startup done”")]),
        ("周期任务", "各自按周期运行", VIOLET,
         [("ins", "上电静止标定约 2 s"), ("comm_rx", "有数据就解析"),
          ("control", "1 kHz 控制"), ("daemon · heartbeat", "上下线报告 · 灯与日志")]),
    ]
    y, lh, lx, bx = 74, 70, 24, 196
    bw_total = W - 24 - bx
    for li, (name, sub_t, color, steps) in enumerate(lanes):
        s.rect(lx, y, W - 48, lh, tint(color, 0.06), tint(color, 0.3))
        s.rect(lx, y, 6, lh, color, color, rx=3)
        s.text(lx + 18, y + 30, name, 14, INK, bold=True)
        s.text(lx + 18, y + 50, sub_t, 11.5, INK2)
        n = len(steps)
        gap = 22
        bw = (bw_total - 12 - gap * (n - 1)) / n
        for i, (a, b) in enumerate(steps):
            x = bx + i * (bw + gap)
            s.rect(x, y + 11, bw, lh - 22, "#ffffff", tint(color, 0.5))
            s.text(x + 12, y + 32, a, 13, INK, bold=True)
            s.text(x + 12, y + 50, b, 11.5, INK2)
            if i < n - 1:
                s.line(x + bw + 2, y + lh / 2, x + bw + gap - 2, y + lh / 2)
        if li < len(lanes) - 1:
            s.line(lx + 60, y + lh + 1, lx + 60, y + lh + 17)
        y += lh + 18
    s.save("startup.svg")


if __name__ == "__main__":
    architecture()
    runtime()
    safety_gate()
    startup()
    print("written:", ", ".join(p.name for p in sorted(OUT.glob("*.svg"))))
