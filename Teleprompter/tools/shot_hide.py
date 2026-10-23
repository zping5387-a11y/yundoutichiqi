# -*- coding: utf-8 -*-
"""验证：点击「隐藏面板」后面板消失，且托盘不弹出气泡提示。"""
import ctypes, os, subprocess, time
from ctypes import wintypes
from PIL import ImageGrab

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"


def text(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def find_button(parent, label):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        cls = ctypes.create_unicode_buffer(64)
        u32.GetClassNameW(h, cls, 64)
        if cls.value == "Button" and text(h) == label:
            hit.append(h)
            return False
        return True

    u32.EnumChildWindows(parent, cb, 0)
    return hit[0] if hit else None


p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1"))
time.sleep(3)
dlg = u32.FindWindowW(None, DLG)
if not dlg:
    print("FAIL: 找不到主窗口")
    p.terminate()
    raise SystemExit(1)

btn = find_button(dlg, "隐藏面板")
print("找到隐藏按钮:", bool(btn))
u32.SendMessageW(btn, 0x00F5, 0, 0)   # BM_CLICK
time.sleep(0.4)
print("点击后面板可见:", bool(u32.IsWindowVisible(dlg)))

# 等 1.2s，气泡若存在此时应已显示
time.sleep(1.2)
w = u32.GetSystemMetrics(0)
h = u32.GetSystemMetrics(1)
box = (max(0, w - 760), max(0, h - 460), w, h)
img = ImageGrab.grab(bbox=box, all_screens=True)
out = os.path.join(ROOT, "build", "shot_hide.png")
img.save(out)
print("截图已保存:", out, img.size)

p.terminate()
print("DONE")
