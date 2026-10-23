# -*- coding: utf-8 -*-
"""截图检查：新面板布局 + 隐藏后托盘图标。"""
import ctypes, os, subprocess, time
from ctypes import wintypes
u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass
from PIL import ImageGrab

ROOT = r"C:\Users\54881\WorkBuddy\2026-10-20-19-29-44\Teleprompter"
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"

p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1"))
time.sleep(3)
dlg = u32.FindWindowW(None, DLG)
if not dlg:
    print("FAIL 找不到主窗口")
    raise SystemExit(1)

r = wintypes.RECT()
u32.GetWindowRect(dlg, ctypes.byref(r))
pad = 8
ImageGrab.grab((r.left - pad, r.top - pad, r.right + pad, r.bottom + pad)).save(
    os.path.join(ROOT, "build", "shot_panel.png"))
print("面板截图保存，尺寸 %dx%d" % (r.right - r.left, r.bottom - r.top))

# 隐藏面板（走真实的 WM_HOTKEY 处理路径）
u32.PostMessageW(dlg, 0x0312, 16, 0)
time.sleep(1.5)
print("隐藏后 visible:", bool(u32.IsWindowVisible(dlg)))

sw = u32.GetSystemMetrics(0)
sh = u32.GetSystemMetrics(1)
ImageGrab.grab((sw - 620, sh - 70, sw, sh)).save(os.path.join(ROOT, "build", "shot_tray.png"))
print("托盘区域截图保存")

# 恢复面板，进程仍存活
u32.PostMessageW(dlg, 0x0312, 16, 0)
time.sleep(1.0)
print("恢复后 visible:", bool(u32.IsWindowVisible(dlg)), "alive:", p.poll() is None)
p.terminate()
print("DONE")
