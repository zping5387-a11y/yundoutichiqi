# -*- coding: utf-8 -*-
"""探针：对比主面板 GetWindowRect / DWM 扩展边界 / GetClientRect。"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
dwm = ctypes.windll.dwmapi
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


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


print("GetWindowRect      =", rect(dlg))
r = wintypes.RECT()
u32.GetClientRect(dlg, ctypes.byref(r))
print("GetClientRect      =", (r.left, r.top, r.right, r.bottom))
pt = wintypes.POINT(0, 0)
u32.ClientToScreen(dlg, ctypes.byref(pt))
print("ClientToScreen(0,0)=", (pt.x, pt.y))

EXT = 9
e = wintypes.RECT()
dwm.DwmGetWindowAttribute(dlg, EXT, ctypes.byref(e), ctypes.sizeof(e))
print("DWM ExtendedBounds =", (e.left, e.top, e.right, e.bottom))
print("DPI                =", u32.GetDpiForWindow(dlg))
print("style=0x%08X ex=0x%08X" % (u32.GetWindowLongW(dlg, -16) & 0xFFFFFFFF, u32.GetWindowLongW(dlg, -20)))

gdi = ctypes.windll.gdi32
hrgn = gdi.CreateRectRgn(0, 0, 0, 0)
res = u32.GetWindowRgn(dlg, hrgn)
print("GetWindowRgn       = res=%d (0=FAIL 1=NULL 2=SIMPLE 3=COMPLEX)" % res)
if res == 2 or res == 3:
    rr = wintypes.RECT()
    gdi.GetRgnBox(hrgn, ctypes.byref(rr))
    print("  region box       =", (rr.left, rr.top, rr.right, rr.bottom))
gdi.DeleteObject(hrgn)
p.terminate()
