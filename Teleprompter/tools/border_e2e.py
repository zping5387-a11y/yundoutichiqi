# -*- coding: utf-8 -*-
"""主窗口黑色边框验收：截图后检查四条边缘像素是否为黑色。"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"

from PIL import ImageGrab


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


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
l, t, r, b = rect(dlg)
w_win, h_win = r - l, b - t

# 用 PrintWindow 抓窗口自身内容，不受其他窗口遮挡影响
gdi = ctypes.windll.gdi32
hdc = u32.GetWindowDC(dlg)
hmem = gdi.CreateCompatibleDC(hdc)
hbmp = gdi.CreateCompatibleBitmap(hdc, w_win, h_win)
gdi.SelectObject(hmem, hbmp)
PW_RENDERFULLCONTENT = 0x00000002
print("PrintWindow ok=%s size=%dx%d" % (bool(u32.PrintWindow(dlg, hmem, PW_RENDERFULLCONTENT)), w_win, h_win))


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
bmi.biCompression = 0
buf = ctypes.create_string_buffer(w_win * h_win * 4)
gdi.GetDIBits(hmem, hbmp, 0, h_win, buf, ctypes.byref(bmi), 0)
u32.ReleaseDC(dlg, hdc)
gdi.DeleteObject(hbmp)
gdi.DeleteDC(hmem)

from PIL import Image
img = Image.frombuffer("RGBX", (w_win, h_win), buf.raw, "raw", "RGBX", 0, 1).convert("RGB")
out = os.path.join(ROOT, "build", "shot_border.png")
img.save(out)
print("截图: %s" % out)

w, h = img.size
px = img.load()

def dark(p, thr=90):
    return p[0] < thr and p[1] < thr and p[2] < thr

# 检查四条边：最外圈必须黑（边框），深入 6px 处必须是背景色（证明无陈旧 region 裁剪）
samples = []
for x in (w // 4, w // 2, 3 * w // 4):
    samples.append(("top", [px[x, d] for d in (0, 1, 6)]))
    samples.append(("bottom", [px[x, h - 1 - d] for d in (0, 1, 6)]))
for y in (h // 4, h // 2, 3 * h // 4):
    samples.append(("left", [px[d, y] for d in (0, 1, 6)]))
    samples.append(("right", [px[w - 1 - d, y] for d in (0, 1, 6)]))

ok = True
for name, ps in samples:
    good = dark(ps[0]) and dark(ps[1]) and not dark(ps[2])
    if not good:
        ok = False
    print("%-6s edge=%s inner=%s -> %s" % (name, ps[0], ps[2], "PASS" if good else "FAIL"))

print("BORDER E2E: %s" % ("PASS" if ok else "FAIL"))
p.terminate()
time.sleep(0.3)
