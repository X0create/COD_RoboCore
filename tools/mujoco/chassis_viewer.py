"""底盘专用 GLFW 窗口：控制按键只分派一次，不触发 Simulate 的显示快捷键。"""

import glfw
import mujoco


class ChassisViewer:
    def __init__(self, model, data, body, controls, visible=True):
        self.model = model
        self.data = data
        self.controls = controls
        self.context = None
        self.scene = None
        self.window = None
        if not glfw.init():
            raise RuntimeError("GLFW initialization failed")
        glfw.window_hint(glfw.VISIBLE, glfw.TRUE if visible else glfw.FALSE)
        self.window = glfw.create_window(960, 640, "27 Sentry - chassis controls", None, None)
        if not self.window:
            glfw.terminate()
            raise RuntimeError("Chassis window creation failed")
        try:
            glfw.make_context_current(self.window)
            glfw.swap_interval(0)
            self.context = mujoco.MjrContext(model, mujoco.mjtFontScale.mjFONTSCALE_150)
            self.scene = mujoco.MjvScene(model, maxgeom=1000)
            self.option = mujoco.MjvOption()
            self.camera = mujoco.MjvCamera()
            self.camera.type = mujoco.mjtCamera.mjCAMERA_TRACKING
            self.camera.trackbodyid = body
            self.camera.distance = 2.0
            self.camera.azimuth = 135
            self.camera.elevation = -45
            self.last_cursor = glfw.get_cursor_pos(self.window)
            glfw.set_key_callback(self.window, self.on_key)
            glfw.set_cursor_pos_callback(self.window, self.on_cursor)
            glfw.set_scroll_callback(self.window, self.on_scroll)
        except Exception:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        if self.context is not None:
            self.context.free()
            self.context = None
        self.scene = None  # MjvScene 的原生内存由 Python 对象析构释放。
        if self.window is not None:
            glfw.destroy_window(self.window)
            self.window = None
        glfw.terminate()

    def is_running(self):
        return not glfw.window_should_close(self.window)

    def poll(self):
        glfw.poll_events()

    def on_key(self, window, key, _scancode, action, _mods):
        if action not in (glfw.PRESS, glfw.RELEASE):
            return
        if key == glfw.KEY_ESCAPE:
            if action == glfw.PRESS:
                glfw.set_window_should_close(window, True)
        else:
            # 直接使用 GLFW 键码；Enter 为 KEY_ENTER，不经过 Simulate 的键码转换。
            self.controls.key(key, pressed=action == glfw.PRESS)

    def on_cursor(self, window, xpos, ypos):
        previous_x, previous_y = self.last_cursor
        self.last_cursor = xpos, ypos
        if glfw.get_mouse_button(window, glfw.MOUSE_BUTTON_LEFT) != glfw.PRESS:
            return
        _, height = glfw.get_window_size(window)
        if height > 0:
            mujoco.mjv_moveCamera(self.model, mujoco.mjtMouse.mjMOUSE_ROTATE_V,
                                 (xpos - previous_x) / height, (ypos - previous_y) / height,
                                 self.camera)

    def on_scroll(self, _window, _xoffset, yoffset):
        mujoco.mjv_moveCamera(self.model, mujoco.mjtMouse.mjMOUSE_ZOOM,
                             0, -0.05 * yoffset, self.camera)

    def render(self, phase, mode):
        width, height = glfw.get_framebuffer_size(self.window)
        if width == 0 or height == 0:
            return  # 窗口最小化时不渲染，物理控制仍由调用方推进。
        viewport = mujoco.MjrRect(0, 0, width, height)
        mujoco.mjv_updateScene(self.model, self.data, self.option, None, self.camera,
                              mujoco.mjtCatBit.mjCAT_ALL, self.scene)
        mujoco.mjr_render(viewport, self.scene, self.context)
        source = "MANUAL" if self.controls.manual else "AUTO"
        status = f"{source} | {mode} | vx {phase.cmd[0]:+.2f}  vy {phase.cmd[1]:+.2f}  wz {phase.cmd[2]:+.2f}"
        help_text = ("Enter: arm / take control     X: zero target\n"
                     "Hold W/S: forward/back     A/D: left/right     Q/E: turn\n"
                     "Space: zero torque     Esc: close\n"
                     "Release movement keys: zero target / brake.\n"
                     "Mouse: left drag to orbit, wheel to zoom")
        mujoco.mjr_overlay(mujoco.mjtFont.mjFONT_NORMAL, mujoco.mjtGridPos.mjGRID_TOPLEFT,
                           viewport, status + "\n" + help_text, "", self.context)
        glfw.swap_buffers(self.window)
