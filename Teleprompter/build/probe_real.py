# -*- coding: utf-8 -*-
"""诊断 2：用真实鼠标点「去提词」，验证是否只有 PostMessage 路径有问题。"""
import ctypes
import os
import subprocess
import time
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
DATA = os.path.join(BUILD, "recent_list")
EXE = os.path.join(BUILD, "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"
u32 = ctypes.windll.user32
u32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
u32.FindWindowW.restype = wintypes.HWND
u32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
u32.GetDlgItem.restype = wintypes.HWND
u32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
u32.GetWindowTextLengthW.restype = ctypes.c_int
u32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass


def wtext(h):
    n = u32.GetWindowTextLengthW(h)
    if n <= 0:
        return ""
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def real_click(x, y):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.15)
    u32.mouse_event(0x0002, 0, 0, 0, 0)   # LEFTDOWN
    time.sleep(0.08)
    u32.mouse_event(0x0004, 0, 0, 0, 0)   # LEFTUP
    time.sleep(0.8)


subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
time.sleep(0.5)
p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_DATA_DIR=DATA))
time.sleep(3)
dlg = u32.FindWindowW(None, DLG)
u32.SetWindowPos(dlg, -1, 120, 760, 0, 0, 0x1)
u32.SetForegroundWindow(dlg)
time.sleep(0.8)
lst = u32.GetDlgItem(dlg, 1074)
st = u32.GetDlgItem(dlg, 1004)
l, t, r, b = rect(lst)
s = (r - l) / 512.0
lh = b - t

n = 0
ini = os.path.join(DATA, "recent.ini")
if os.path.exists(ini):
    txt = open(ini, "rb").read().decode("gbk", "surrogateescape")
    n = len([x for x in txt.splitlines() if x.strip().startswith("recent")])
avail = lh - 2
pref = 100 * s
rh = pref if n * pref <= avail else max(int(avail / max(1, n)), 72 * s)
print("条目=%d 列表高=%d s=%.3f 行高=%.0f(%.1f 逻辑) 状态=%s" % (n, lh, s, rh, rh / s, wtext(st)))

for idx in (1, 0):
    y_c = (1 + rh * (idx + 1)) - 14 * s
    for dx_design in (38, 90):        # 文件夹中心 / 去提词中心
        x_c = dx_design * s
        real_click(l + x_c, t + y_c)
        got = wtext(st)
        print("  真实点击 idx=%d 设计x=%d 客户区(%.0f,%.0f) → %s" % (idx, dx_design, x_c, y_c, got))

subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
