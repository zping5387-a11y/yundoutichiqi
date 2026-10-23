# -*- coding: utf-8 -*-
"""诊断「去提词」按钮命中：网格扫描行内点击点，找出能触发载入的坐标。"""
import ctypes
import os
import subprocess
import sys
import time
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
DATA = os.path.join(BUILD, "recent_list")
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"
u32 = ctypes.windll.user32
u32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
u32.FindWindowW.restype = wintypes.HWND
u32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
u32.GetDlgItem.restype = wintypes.HWND
u32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
u32.GetWindowTextLengthW.restype = ctypes.c_int
u32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
u32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

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


subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
time.sleep(0.5)
p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1",
                                     TP_DATA_DIR=DATA))
time.sleep(3)
dlg = u32.FindWindowW(None, DLG)
u32.SetWindowPos(dlg, -1, 120, 760, 0, 0, 0x1)
u32.SetForegroundWindow(dlg)
time.sleep(0.5)
lst = u32.GetDlgItem(dlg, 1074)
st = u32.GetDlgItem(dlg, 1004)
l, t, r, b = rect(lst)
dpi = u32.GetDpiForWindow(dlg) or 96
s = (r - l) / 512.0
print("列表 rect=(%d,%d,%d,%d) s=%.4f 状态=%s" % (l, t, r, b, s, wtext(st)))

WM_MOVE, WM_DOWN, WM_UP = 0x0200, 0x0201, 0x0202


def post(cx, cy):
    u32.PostMessageW(lst, WM_MOVE, 0, ((int(cy) & 0xFFFF) << 16) | (int(cx) & 0xFFFF))
    time.sleep(0.05)
    u32.PostMessageW(lst, WM_DOWN, 1, ((int(cy) & 0xFFFF) << 16) | (int(cx) & 0xFFFF))
    time.sleep(0.1)
    u32.PostMessageW(lst, WM_UP, 0, ((int(cy) & 0xFFFF) << 16) | (int(cx) & 0xFFFF))
    time.sleep(0.5)


# 行 2（idx=1，b.txt）：扫描 y=行底-70 .. 行底-5，x=0..420
hits = []
for dy in range(70, 4, -8):
    y = (1 + 217 * 2) - dy
    for x in range(20, 430, 25):
        post(x, y)
        if "b.txt" in wtext(st):
            hits.append((x, y))
            print("命中: 客户区(%d,%d)  dy=%d" % (x, y, dy))
            break
    if hits:
        break

if not hits:
    print("整行扫描未命中。理论按钮 y=%.0f（=行底-32.8）" % ((1 + 217 * 2) - 14 * s))
subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
