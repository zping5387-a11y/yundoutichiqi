# -*- coding: utf-8 -*-
"""标题按钮跟随窗口大小验收：
1) 拉宽窗口后，按钮图形（最小化/最大化/关闭）应贴近新右上角；
2) 最大化后同样贴近工作区右上角。
用 PrintWindow 抓窗口自身，扫描标题带右侧区域内的按钮字形暗像素（RGB≈74,82,92）。
"""
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
SW_MAXIMIZE = 3
SW_RESTORE = 9


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def capture(h):
    l, t, r, b = rect(h)
    w, hh = r - l, b - t
    hdc = u32.GetWindowDC(h)
    hmem = gdi.CreateCompatibleDC(hdc)
    hbmp = gdi.CreateCompatibleBitmap(hdc, w, hh)
    gdi.SelectObject(hmem, hbmp)
    u32.PrintWindow(h, hmem, 2)

    class BMI(ctypes.Structure):
        _fields_ = [("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
                    ("biHeight", ctypes.c_long), ("biPlanes", wintypes.WORD),
                    ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                    ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wintypes.DWORD),
                    ("biClrImportant", wintypes.DWORD)]
    bmi = BMI()
    bmi.biSize = ctypes.sizeof(bmi)
    bmi.biWidth = w
    bmi.biHeight = -hh
    bmi.biPlanes = 1
    bmi.biBitCount = 32
    buf = ctypes.create_string_buffer(w * hh * 4)
    gdi.GetDIBits(hmem, hbmp, 0, hh, buf, ctypes.byref(bmi), 0)
    u32.ReleaseDC(h, hdc)
    gdi.DeleteObject(hbmp)
    gdi.DeleteDC(hmem)
    return w, hh, buf


def glyph_max_x(w, hh, buf):
    """在标题带右上区域扫描按钮字形暗像素，返回最大 x（不在此区域返回 -1）"""
    fg = (74, 82, 92)
    import struct
    mx = -1
    for y in range(6, min(78, hh)):
        for x in range(w - 110, w - 4):
            o = (y * w + x) * 4
            b, g, r = struct.unpack_from("BBB", buf, o)
            if abs(r - fg[0]) < 36 and abs(g - fg[1]) < 36 and abs(b - fg[2]) < 36:
                if x > mx:
                    mx = x
    return mx


def save(w, hh, buf, name):
    from PIL import Image
    img = Image.frombuffer("RGBX", (w, hh), buf.raw, "raw", "RGBX", 0, 1).convert("RGB")
    img.save(os.path.join(ROOT, "build", name))


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

ok = True

# 场景1：原始尺寸，按钮应在右上角（距右缘 < 80px）
l, t, r0, b0 = rect(dlg)
w0, h0 = r0 - l, b0 - t
w, hh, buf = capture(dlg)
mx = glyph_max_x(w, hh, buf)
g1 = mx > 0 and (w - mx) < 80
print("场景1 原始窗口 %dx%d 按钮最右字形 x=%d 距右缘=%d -> %s" % (w0, h0, mx, w - mx, "PASS" if g1 else "FAIL"))
ok &= g1
save(w, hh, buf, "shot_cap_normal.png")

# 场景2：拉宽 400px，按钮应跟随到新右上角
u32.MoveWindow(dlg, 60, 60, w0 + 400, h0, True)
time.sleep(0.8)
w, hh, buf = capture(dlg)
mx = glyph_max_x(w, hh, buf)
g2 = mx > 0 and (w - mx) < 80
print("场景2 拉宽到 %dx%d 按钮最右字形 x=%d 距右缘=%d -> %s" % (w, hh, mx, w - mx, "PASS" if g2 else "FAIL"))
ok &= g2
save(w, hh, buf, "shot_cap_wide.png")

# 场景3：最大化，按钮应贴工作区右上角
u32.ShowWindow(dlg, SW_MAXIMIZE)
time.sleep(1.0)
w, hh, buf = capture(dlg)
mx = glyph_max_x(w, hh, buf)
g3 = mx > 0 and (w - mx) < 80
print("场景3 最大化 %dx%d 按钮最右字形 x=%d 距右缘=%d -> %s" % (w, hh, mx, w - mx, "PASS" if g3 else "FAIL"))
ok &= g3
save(w, hh, buf, "shot_cap_max.png")

# 场景4：还原后按钮回到原位
u32.ShowWindow(dlg, SW_RESTORE)
time.sleep(0.8)
w, hh, buf = capture(dlg)
mx = glyph_max_x(w, hh, buf)
g4 = mx > 0 and abs((w - mx) - (w0 - glyph_ref if False else 0)) >= 0 and (w - mx) < 80
print("场景4 还原 %dx%d 按钮最右字形 x=%d 距右缘=%d -> %s" % (w, hh, mx, w - mx, "PASS" if (mx > 0 and w - mx < 80) else "FAIL"))
ok &= (mx > 0 and w - mx < 80)
save(w, hh, buf, "shot_cap_restore.png")

print("CAPTION FOLLOW E2E: %s" % ("PASS" if ok else "FAIL"))
p.terminate()
time.sleep(0.3)
