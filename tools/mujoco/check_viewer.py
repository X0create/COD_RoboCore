"""隐藏窗口回归：Windows 键消息 → GLFW → 底盘输入，显示状态与地板保持不变。"""

import ctypes as ct

import glfw
import mujoco
import numpy as np

from chassis_viewer import ChassisViewer
from sentry_chassis import Controls, Controller, OUTPUT_DIR, Phase, Simulation
from sentry_model import SentrySpec, write_png


def main():
    spec = SentrySpec()
    controller = Controller(spec, OUTPUT_DIR / "chassis_bridge.dll")
    sim = Simulation(spec, controller)
    controls = Controls()
    user32 = ct.WinDLL("user32", use_last_error=True)
    user32.PostMessageW.argtypes = [ct.c_void_p, ct.c_uint, ct.c_size_t, ct.c_ssize_t]
    user32.PostMessageW.restype = ct.c_bool
    user32.MapVirtualKeyW.argtypes = [ct.c_uint, ct.c_uint]
    user32.MapVirtualKeyW.restype = ct.c_uint

    with ChassisViewer(sim.model, sim.data, sim.body, controls, visible=False) as viewer:
        hwnd = glfw.get_win32_window(viewer.window)

        def press(vkey):
            scan = user32.MapVirtualKeyW(vkey, 0)
            key_bits = 1 | (scan << 16)
            for message, bits in ((0x0100, key_bits), (0x0101, key_bits | (3 << 30))):
                if not user32.PostMessageW(hwnd, message, vkey, bits):
                    raise ct.WinError(ct.get_last_error())
            viewer.poll()

        def image():
            # 两个交换缓冲都画同一场景，读取交换后的窗口缓冲仍可稳定比较。
            phase = controls.phase(Phase("auto", 1, armed=False))
            mode = ("Init", "Safe", "Manual")[controller.output.mode]
            viewer.render(phase, mode)
            viewer.render(phase, mode)
            width, height = glfw.get_framebuffer_size(viewer.window)
            pixels = np.zeros((height, width, 3), dtype=np.uint8)
            mujoco.mjr_readPixels(pixels, None, mujoco.MjrRect(0, 0, width, height), viewer.context)
            return pixels

        initial = image()
        option_flags = viewer.option.flags.copy()
        scene_flags = viewer.scene.flags.copy()
        press(0x0D)  # Windows VK_RETURN，由 GLFW 转为 KEY_ENTER。
        assert [controls.phase(Phase("auto", 1)).armed for _ in range(3)] == [False, False, True]
        expected = {"W": (0.5, 0, 0), "D": (0.5, -0.5, 0), "X": (0, 0, 0),
                    "S": (-0.5, 0, 0), "A": (-0.5, 0.5, 0),
                    "Q": (-0.5, 0.5, 0.7), "E": (-0.5, 0.5, -0.7)}
        for key, cmd in expected.items():
            press(ord(key))
            assert controls.phase(Phase("auto", 1)).cmd == cmd, f"Key dispatch failed: {key}"
            pixels = image()
            assert np.array_equal(viewer.option.flags, option_flags), f"Visualization changed: {key}"
            assert np.array_equal(viewer.scene.flags, scene_flags), f"Rendering changed: {key}"
            # 底部 2/3 包含车与地板，不含顶部的指令文字；应与按键前完全一致。
            crop = pixels.shape[0] * 2 // 3
            assert np.array_equal(initial[:crop], pixels[:crop]), f"Car / floor image changed: {key}"
        press(0x20)
        assert not controls.phase(Phase("auto", 1)).armed
        assert controls.phase(Phase("auto", 1)).cmd == (0, 0, 0)
        press(0x0D)
        step = 0

        def advance(seconds):
            nonlocal step
            for _ in range(round(seconds / controller.dt_s)):
                phase = controls.phase(Phase("auto", 1))
                row = sim.step(step, phase, controller)
                step += 1
            return row

        advance(0.5)
        press(ord("W"))
        row = advance(3.0)
        assert controller.output.mode == 2 and row[4] > 0.35, "Keyboard did not drive C chassis"
        press(ord("X"))
        row = advance(3.0)
        assert np.max(np.abs(row[4:7])) < 0.05, "X did not brake chassis"
        press(ord("D"))
        row = advance(3.0)
        assert row[5] < -0.35, "D did not drive sideways"
        viewer.on_scroll(viewer.window, 0, 1)
        viewer.render(controls.phase(Phase("auto", 1)), "Manual")
        pixels = image()
        write_png(OUTPUT_DIR / "keyboard_preview.png", np.flipud(pixels))
        press(0x1B)
        assert not viewer.is_running(), "Escape did not close window"
    print("PASS: native Enter/WASDQE/X/Space/Esc events; unchanged car/floor rendering; C motion/braking")
    print(f"Preview: {OUTPUT_DIR / 'keyboard_preview.png'}")


if __name__ == "__main__":
    main()
