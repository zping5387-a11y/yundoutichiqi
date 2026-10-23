# -*- coding: utf-8 -*-
"""打印主窗口四边像素序列，核对边框宽度。"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
gdi = ctypes.windll.gdi32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"

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
time.sleep(0.5)

r = wintypes.RECT()
u32.GetWindowRect(dlg, ctypes.byref(r))
w_win, h_win = r.right - r.left, r.bottom - r.top

hdc = u32.GetWindowDC(dlg)
hmem = gdi.CreateCompatibleDC(hdc)
hbmp = gdi.CreateCompatibleBitmap(hdc, w_win, h_win)
gdi.SelectObject(hmem, hbmp)
u32.PrintWindow(dlg, hmem, 2)


class BMPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
                ("biHeight", ctypes.c_long), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]


bmi = BMPINFOHEADER()
bmi.biSize = ctypes.sizeof(bmi)
bmi.biWidth = w_win
bmi.biHeight = -h_win
bmi.biPlanes = 1
bmi.biBitCount = 32
buf = ctypes.create_string_buffer(w_win * h_win * 4)
gdi.GetDIBits(hmem, hbmp, 0, h_win, buf, ctypes.byref(bmi), 0)
u32.ReleaseDC(dlg, hdc)
gdi.DeleteObject(hbmp)
gdi.DeleteDC(hmem)


def px(x, y):
    o = (y * w_win + x) * 4
    return (buf[o + 2], buf[o + 1], buf[o])


y = h_win // 2
x = w_win // 2
print("left  y=%d :" % y, [px(d, y) for d in range(8)])
print("right y=%d :" % y, [px(w_win - 1 - d, y) for d in range(8)])
print("top   x=%d :" % x, [px(x, d) for d in range(8)])
print("bot   x=%d :" % x, [px(x, h_win - 1 - d) for d in range(8)])
p.terminate()
