# -*- coding: utf-8 -*-
"""诊断：最大化后读子控件实际矩形，验证 ApplyLayout 是否生效"""
import ctypes, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = "C:/Users/54881/WorkBuddy/2026-10-20-19-29-44/Teleprompter"
DLG = "芸豆提词器 · 教学模式"

subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)
p = subprocess.Popen([ROOT + "/build/TeacherPrompter.exe"],
                     env=dict(__import__("os").environ, TP_CAPTURE_ALLOWED="1"))
time.sleep(3.0)

dlg = u32.FindWindowW(None, DLG)
u32.ShowWindow(dlg, 3)      # SW_MAXIMIZE
time.sleep(0.8)

rows = []


def text_of(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


@ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def cb(c, l):
    r = wintypes.RECT()
    u32.GetWindowRect(c, ctypes.byref(r))
    rows.append((text_of(c)[:12], r.left, r.top, r.right, r.bottom))
    return True


u32.EnumChildWindows(dlg, cb, 0)
print("客户区与窗口：")
cr = wintypes.RECT(); u32.GetClientRect(dlg, ctypes.byref(cr))
print("  client %dx%d" % (cr.right, cr.bottom))
for t, l, tp, rr, bb in rows:
    print("  %-14s (%5d,%5d)-(%5d,%5d)  w=%4d h=%3d" % (t, l, tp, rr, bb, rr - l, bb - tp))

p.terminate()
