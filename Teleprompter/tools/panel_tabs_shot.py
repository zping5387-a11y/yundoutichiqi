# -*- coding: utf-8 -*-
"""主面板改造验收：左侧「稿件 / 语音 / 设置」三个页签 + 可调整大小 / 可最大化

产出截图：稿件页、语音页、设置页、最大化后的设置页。
另外打印客户区尺寸、页签切换后各控件的可见数量，便于人工核对。
"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
u32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
u32.mouse_event.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                            wintypes.DWORD, ctypes.c_size_t]
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"
SW_MAXIMIZE = 3
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004

from PIL import ImageGrab


def text_of(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def client_size(h):
    r = wintypes.RECT()
    u32.GetClientRect(h, ctypes.byref(r))
    return r.right - r.left, r.bottom - r.top


def to_screen(h, x, y):
    pt = wintypes.POINT(int(x), int(y))
    u32.ClientToScreen(h, ctypes.byref(pt))
    return pt.x, pt.y


def click(x, y, settle=0.5):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.08)
    u32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.05)
    u32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    time.sleep(settle)


def grab(h, name):
    l, t, r, b = rect(h)
    p = os.path.join(ROOT, "build", name)
    ImageGrab.grab(bbox=(l, t, r, b), all_screens=True).save(p)
    print("   截图：%s" % p)
    return p


def visible_children(h):
    out = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(c, l):
        if u32.IsWindowVisible(c):
            out.append(c)
        return True

    u32.EnumChildWindows(h, cb, 0)
    return out


subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)

p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1"))
time.sleep(3.0)

dlg = u32.FindWindowW(None, DLG)
if not dlg:
    print("FAIL: 找不到主面板")
    p.terminate()
    sys.exit(1)

dpi = u32.GetDpiForWindow(dlg) or 96
s = dpi / 96.0
print("面板 rect=%s 客户区=%s dpi=%d" % (rect(dlg), client_size(dlg), dpi))

style = u32.GetWindowLongW(dlg, -16)          # GWL_STYLE
print("WS_THICKFRAME=%d WS_MAXIMIZEBOX=%d" %
      (bool(style & 0x00040000), bool(style & 0x00010000)))

grab(dlg, "shot_panel_script.png")
n1 = len(visible_children(dlg))
print("稿件页可见子控件数：%d" % n1)

# 点「语音」页签（设计坐标：x=16..100，第二个 y=134..186）
click(*to_screen(dlg, (16 + 42) * s, (76 + 52 + 6 + 26) * s))
time.sleep(0.4)
n2 = len(visible_children(dlg))
print("语音页可见子控件数：%d" % n2)
grab(dlg, "shot_panel_voice.png")

# 点「设置」页签（第三个 y=192..244）
click(*to_screen(dlg, (16 + 42) * s, (76 + 2 * (52 + 6) + 26) * s))
time.sleep(0.4)
n3 = len(visible_children(dlg))
print("设置页可见子控件数：%d" % n3)
grab(dlg, "shot_panel_settings.png")

# 切回稿件页
click(*to_screen(dlg, (16 + 42) * s, (76 + 26) * s))
time.sleep(0.3)
print("切回稿件页可见子控件数：%d" % len(visible_children(dlg)))

# 最大化 → 看排版是否跟手
u32.ShowWindow(dlg, SW_MAXIMIZE)
time.sleep(0.8)
print("最大化后客户区：%s" % (client_size(dlg),))
click(*to_screen(dlg, (16 + 42) * s, (76 + 2 * (52 + 6) + 26) * s))
time.sleep(0.5)
grab(dlg, "shot_panel_max.png")

u32.ShowWindow(dlg, 9)      # SW_RESTORE
time.sleep(0.4)
p.terminate()
time.sleep(0.3)
print("DONE")
