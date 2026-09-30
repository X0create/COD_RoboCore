#!/usr/bin/env python3
"""IMU 加热的热模型：用上板数据拟合，再在模型上比较加热参数（ADR 0042）。

    python tools/heater_model.py            # 拟合 docs/data/heater_2026-09-30.csv，打印参数，并比较新旧加热参数

数据来自 Ozone Data Sampling：imu_state.data.temperature_c、ins.imu.heater_pid.output、ins.imu.heater_pid.integral。
那一段里电池断电约 50 s（加热片不供电，单片机由调试器供电），再接上电池。
模型三个节点：加热片附近 H、芯片读数 L、板子 B，环境温度 A：
    dH = a·u − k1(H − L)，dL = k2(H − L) − k3(L − B)，dB = k4(L − B) − k5(B − A)
传感器每 1.28 s 更新一次、分辨率 0.125 °C。只用标准库。
"""
import bisect
import csv
import math
import random
from pathlib import Path

DATA = Path(__file__).resolve().parent.parent / "docs" / "data" / "heater_2026-09-30.csv"
SENSOR_PERIOD_S, SENSOR_LSB_C, TARGET_C = 1.28, 0.125, 40.0


def load():
    rows = list(csv.DictReader(open(DATA, encoding="utf-8")))
    t = [float(r["Time"]) for r in rows]
    y = [float(r["imu_state.data.temperature_c"]) for r in rows]
    u = [min(max(float(r["ins.imu.heater_pid.output"]), 0.0), 1.0) for r in rows]
    return t, y, u


def fit(t, y, u, t0=36.0, t1=112.7, dt=0.02, iters=2500):
    """在 t0–t1（含断电、上电、调节）上拟合；参数：a,k1,k2,k3,k4,k5,A,上电时刻,板温初值"""
    obs = [(t[k], y[k]) for k in range(1, len(t)) if y[k] != y[k - 1] and t[k] >= t0]
    grid = [t0 + i * dt for i in range(int((t1 - t0) / dt) + 1)]
    ug = [u[max(bisect.bisect_right(t, g) - 1, 0)] for g in grid]

    def err(p):
        a, k1, k2, k3, k4, k5, amb, t_on, b0 = p
        h = l = 36.0
        b, e, oi = b0, 0.0, 0
        for i, g in enumerate(grid):
            uu = ug[i] if g >= t_on else 0.0
            h, l, b = (h + (a * uu - k1 * (h - l)) * dt, l + (k2 * (h - l) - k3 * (l - b)) * dt,
                       b + (k4 * (l - b) - k5 * (b - amb)) * dt)
            while oi < len(obs) and obs[oi][0] <= g:
                e += (l - obs[oi][1]) ** 2
                oi += 1
        return e

    p = [60, 3, 1.5, 0.3, 0.05, 0.02, 26, 85.3, 33]
    lo = [1, 0.05, 0.05, 0.005, 0.001, 0.0005, 15, 84.0, 25]
    hi = [500, 50, 50, 5, 2, 1, 32, 86.5, 40]
    best = err(p)
    rnd = random.Random(1)
    for _ in range(iters):
        q = [min(max(v + rnd.gauss(0, 0.3) if j >= 6 else v * math.exp(rnd.gauss(0, 0.15)), lo[j]), hi[j])
             for j, v in enumerate(p)]
        e = err(q)
        if e < best:
            best, p = e, q
    return p, math.sqrt(best / len(obs))


def simulate(model, cap, period_s, kp, ti_s, gain=1.0, amb=None, t_end=300.0, off=None, dt=0.01):
    """按 pid.c 的位置式 + 条件积分（ADR 0040）模拟加热闭环，返回 [(t, 芯片温度)]"""
    a, k1, k2, k3, k4, k5, a0 = model[:7]
    amb = a0 if amb is None else amb
    h = l = b = amb
    ki = kp * period_s / ti_s
    ilim = cap / ki
    integ, u, reading, next_s, next_c, out = 0.0, 0.0, 0.0, 0.0, 0.0, []
    for i in range(int(t_end / dt)):
        now = i * dt
        if now >= next_s:
            reading, next_s = round(l / SENSOR_LSB_C) * SENSOR_LSB_C, next_s + SENSOR_PERIOD_S
        if now >= next_c:
            next_c += period_s
            e = TARGET_C - reading
            p = kp * e
            cand = min(max(integ + e, -ilim), ilim)
            s = p + ki * cand
            if not (abs(s) > cap and (s > 0) == (e > 0)):
                integ = cand
            else:
                room = ((cap if s > 0 else -cap) - p) / ki
                if (room > integ) if e > 0 else (room < integ):
                    integ = room
            u = min(max(p + ki * integ, 0.0), cap)
        ue = 0.0 if off and off[0] <= now < off[1] else u * gain
        h, l, b = (h + (a * ue - k1 * (h - l)) * dt, l + (k2 * (h - l) - k3 * (l - b)) * dt,
                   b + (k4 * (l - b) - k5 * (b - amb)) * dt)
        out.append((now, l))
    return out


def main():
    model, rmse = fit(*load())
    print("模型参数 a,k1,k2,k3,k4,k5,环境,上电时刻,板温初值:", [round(v, 4) for v in model], f"拟合误差 {rmse:.2f} °C")
    for name, c in [("UniC 参数（25%、100 ms、kp 0.05）", (0.25, 0.1, 0.05, 20.0)),
                    ("ADR 0042（8%、1280 ms、kp 0.01、Ti 10 s）", (0.08, 1.28, 0.01, 10.0))]:
        for amb in (15.0, 25.0, 35.0):
            for gain in (0.7, 1.0, 1.3):
                h = simulate(model, *c, gain=gain, amb=amb)
                reach = next((t for t, l in h if l >= 39.5), float("nan"))
                peak = max(l for _, l in h)
                tail = [l for t, l in h if t >= 200]
                print(f"{name} 环境 {amb:4.0f} °C 功率×{gain}: 到 39.5 °C {reach:5.1f} s，峰值 {peak:5.2f} °C，"
                      f"稳态峰峰 {max(tail) - min(tail):4.2f} °C")


if __name__ == "__main__":
    main()
