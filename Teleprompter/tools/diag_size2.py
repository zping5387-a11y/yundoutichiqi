# -*- coding: utf-8 -*-
"""拖动缩放逐边验证"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

EXE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "TeacherPrompter.exe")
OVERLAY_TITLE = "芸豆提词"
DLG_TITLE = "芸豆提词器 · 教学模式"


def rect():
    h = u32.FindWindowW(None, OVERLAY_TITLE)
    if not h:
        return None
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def find_button(parent, text):
    out = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        buf = ctypes.create_unicode_buffer(256)
        u32.GetWindowTextW(h, buf, 256)
        cls = ctypes.create_unicode_buffer(64)
        u32.GetClassNameW(h, cls, 64)
        if cls.value == "Button" and buf.value == text:
            out.append(h)
            return False
        return True

    u32.EnumChildWindows(parent, cb, 0)
    return out[0] if out else None


def drag(x1, y1, x2, y2, label=""):
    u32.SetCursorPos(x1, y1)
    time.sleep(0.2)
    u32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.2)
    steps = 10
    for i in range(1, steps + 1):
        u32.SetCursorPos(int(x1 + (x2 - x1) * i / steps), int(y1 + (y2 - y1) * i / steps))
        time.sleep(0.035)
    time.sleep(0.1)
    u32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.4)
    print(f"{label}: {rect()}")


def main():
    env = dict(os.environ); env["TP_CAPTURE_ALLOWED"] = "1"
    p = subprocess.Popen([EXE], env=env)
    time.sleep(3.0)
    dlg = u32.FindWindowW(None, DLG_TITLE)

    find_button(dlg, "调整位置")
    b = find_button(dlg, "调整位置")
    u32.SendMessageW(b, 0x00F5, 0, 0)
    time.sleep(0.8)
    print("进入调整模式:", rect())

    r = rect()
    drag(r[2] - 5, (r[1] + r[3]) // 2, r[2] - 5 + 200, (r[1] + r[3]) // 2, "右边第1次")
    r = rect()
    drag(r[2] - 5, (r[1] + r[3]) // 2, r[2] - 5 - 260, (r[1] + r[3]) // 2, "右边第2次(缩)")
    r = rect()
    drag(r[0] + 5, r[3] - 5, r[0] + 5, r[3] - 5 + 120, "底边(增高)")
    r = rect()
    drag(r[0] + 5, r[1] + 5, r[0] + 5 + 120, r[1] + 5 + 90, "左上角(缩小)")
    r = rect()
    drag((r[0] + r[2]) // 2, r[1] + 80, (r[0] + r[2]) // 2 + 150, r[1] + 80 + 90, "内部(移动)")

    from PIL import ImageGrab
    time.sleep(0.4)
    img = ImageGrab.grab()
    out = os.path.join(os.path.dirname(EXE), "shot_size.png")
    img.save(out)
    print("截图:", out)
    p.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
