# -*- coding: utf-8 -*-
"""自动化验证浮层尺寸调整：按钮 / 热键 / 鼠标拖边"""
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


def overlay_rect():
    hwnd = u32.FindWindowW(None, OVERLAY_TITLE)
    if not hwnd:
        return None
    r = wintypes.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def find_button(parent, text):
    result = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        buf = ctypes.create_unicode_buffer(256)
        u32.GetWindowTextW(h, buf, 256)
        cls = ctypes.create_unicode_buffer(64)
        u32.GetClassNameW(h, cls, 64)
        if cls.value == "Button" and buf.value == text:
            result.append(h)
            return False
        return True

    u32.EnumChildWindows(parent, cb, 0)
    return result[0] if result else None


def click(hwnd):
    u32.SendMessageW(hwnd, 0x00F5, 0, 0)   # BM_CLICK


def hotkey(vk, shift=True):
    mods = 0x0002 | 0x0001 | (0x0004 if shift else 0)   # CTRL | ALT | SHIFT
    u32.keybd_event(0x11, 0, 0, 0)
    u32.keybd_event(0x12, 0, 0, 0)
    if shift:
        u32.keybd_event(0x10, 0, 0, 0)
    u32.keybd_event(vk, 0, 0, 0)
    time.sleep(0.05)
    u32.keybd_event(vk, 0, 2, 0)
    if shift:
        u32.keybd_event(0x10, 0, 2, 0)
    u32.keybd_event(0x12, 0, 2, 0)
    u32.keybd_event(0x11, 0, 2, 0)
    time.sleep(0.35)


def drag(x1, y1, x2, y2):
    u32.SetCursorPos(x1, y1)
    time.sleep(0.15)
    u32.mouse_event(0x0002, 0, 0, 0, 0)   # LEFT DOWN
    time.sleep(0.15)
    steps = 12
    for i in range(1, steps + 1):
        u32.SetCursorPos(int(x1 + (x2 - x1) * i / steps), int(y1 + (y2 - y1) * i / steps))
        time.sleep(0.03)
    u32.mouse_event(0x0004, 0, 0, 0, 0)   # LEFT UP
    time.sleep(0.35)


def main():
    env = dict(os.environ)
    env["TP_CAPTURE_ALLOWED"] = "1"       # 允许截屏看到浮层，便于验证
    p = subprocess.Popen([EXE], env=env)
    time.sleep(3.0)

    dlg = u32.FindWindowW(None, DLG_TITLE)
    if not dlg:
        print("FAIL: 控制台窗口未找到")
        p.terminate()
        return 1

    base = overlay_rect()
    print("初始浮层 rect:", base)

    # 1) 按钮：宽 ＋ 两次
    b = find_button(dlg, "宽 ＋")
    if not b:
        print("FAIL: 未找到「宽 ＋」按钮")
    else:
        click(b); time.sleep(0.3)
        click(b); time.sleep(0.4)
        r1 = overlay_rect()
        print("点两次 宽＋ 后:", r1, "宽度变化:", r1[2] - r1[0] - (base[2] - base[0]))

    # 2) 按钮：高 − 一次
    b = find_button(dlg, "高 −")
    if b:
        click(b); time.sleep(0.4)
        r2 = overlay_rect()
        print("点一次 高− 后:", r2, "高度变化:", r2[3] - r2[1] - (r1[3] - r1[1]))

    # 3) 热键 Ctrl+Alt+Shift+Left（缩窄）
    hotkey(0x25)   # VK_LEFT
    r3 = overlay_rect()
    print("热键缩窄后:", r3, "宽度:", r3[2] - r3[0])

    # 4) 进入调整模式，拖动右边中点放大
    dlg_btn = find_button(dlg, "调整位置")
    if dlg_btn:
        click(dlg_btn)
        time.sleep(0.6)
        r4 = overlay_rect()
        mid_y = (r4[1] + r4[3]) // 2
        drag(r4[2] - 4, mid_y, r4[2] + 160, mid_y)
        r5 = overlay_rect()
        print("拖右边后:", r5, "宽度:", r5[2] - r5[0], "(拖前", r4[2] - r4[0], ")")

        # 5) 拖动左上角
        drag(r5[0] + 4, r5[1] + 4, r5[0] - 80, r5[1] - 60)
        r6 = overlay_rect()
        print("拖左上角后:", r6, "尺寸:", r6[2] - r6[0], "x", r6[3] - r6[1])

    # 截图
    time.sleep(0.6)
    try:
        from PIL import ImageGrab
        img = ImageGrab.grab()
        out = os.path.join(os.path.dirname(EXE), "shot_size.png")
        img.save(out)
        print("截图:", out, img.size)
    except Exception as e:
        print("截图失败:", e)

    time.sleep(0.5)
    p.terminate()
    print("DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
