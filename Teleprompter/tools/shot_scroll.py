# -*- coding: utf-8 -*-
"""验证：浮层带滚动条、可拖动、初始在顶部不自动滚动。"""
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
OVERLAY = "芸豆提词"
BAR = "提词滚动条"
WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MOUSEMOVE = 0x0201, 0x0202, 0x0200


def text(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def find_child(parent, label):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if text(h) == label:
            hit.append(h)
            return False
        return True

    u32.EnumChildWindows(parent, cb, 0)
    return hit[0] if hit else None


def find_top(label):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if u32.IsWindowVisible(h) and text(h) == label:
            hit.append(h)
            return False
        return True

    u32.EnumWindows(cb, 0)
    return hit[0] if hit else None


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1"))
time.sleep(3)
dlg = u32.FindWindowW(None, DLG)
if not dlg:
    print("FAIL: 找不到主窗口")
    p.terminate()
    raise SystemExit(1)

# 载入示例文稿（够长，能滚动）
btn = find_child(dlg, "示例文稿")
print("示例文稿按钮:", bool(btn))
if btn:
    u32.SendMessageW(btn, 0x00F5, 0, 0)
    time.sleep(1.0)

ov = find_top(OVERLAY)
bar = find_top(BAR)
print("浮层窗口:", ov, rect(ov) if ov else None)
print("滚动条窗口:", bar, rect(bar) if bar else None)

# 截图：浮层 + 滚动条
if ov:
    l, t, r, b = rect(ov)
    img = ImageGrab.grab(bbox=(l - 8, t - 8, r + 8, b + 8), all_screens=True)
    out1 = os.path.join(ROOT, "build", "shot_scroll.png")
    img.save(out1)
    print("截图1:", out1, img.size)

# 拖动滚动条：在滑块位置按下并往下拖 200px
if bar and ov:
    bl, bt, br, bb = rect(bar)
    bx = (bl + br) // 2
    by = bt + (bb - bt) // 6            # 顶部滑块附近
    u32.SendMessageW(bar, WM_LBUTTONDOWN, 1, (by << 16) | (bx & 0xFFFF))
    for step in range(1, 9):
        y = by + step * 25
        u32.SendMessageW(bar, WM_MOUSEMOVE, 1, (y << 16) | (bx & 0xFFFF))
        time.sleep(0.05)
    u32.SendMessageW(bar, WM_LBUTTONUP, 0, ((by + 200) << 16) | (bx & 0xFFFF))
    time.sleep(0.6)
    img2 = ImageGrab.grab(bbox=(l - 8, t - 8, r + 8, b + 8), all_screens=True)
    out2 = os.path.join(ROOT, "build", "shot_scroll_drag.png")
    img2.save(out2)
    print("截图2:", out2, img2.size)
    print("拖动后画面有变化:", list(img.getdata()) != list(img2.getdata()))

print("进程存活:", p.poll() is None)
p.terminate()
print("DONE")
