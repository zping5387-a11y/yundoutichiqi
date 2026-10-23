import ctypes, subprocess, time, os, sys
from ctypes import wintypes
from PIL import ImageGrab

u32 = ctypes.WinDLL('user32', use_last_error=True)
u32.FindWindowW.restype = wintypes.HWND
u32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]

env = os.environ.copy()
env['TP_CAPTURE_ALLOWED'] = '1'
exe = r"C:\Users\54881\WorkBuddy\2026-10-20-19-29-44\Teleprompter\build\TeacherPrompter.exe"
proc = subprocess.Popen([exe], env=env)
time.sleep(3)

title = "芸豆提词器 · 教学模式"
hwnd = u32.FindWindowW(None, title)
print("main hwnd:", hwnd)
u32.ShowWindow(hwnd, 5)
u32.SetForegroundWindow(hwnd)
time.sleep(0.6)

# 点击「示例文稿」按钮
target = []
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def cb(h, l):
    n = u32.GetWindowTextLengthW(h)
    buf = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, buf, n + 1)
    if buf.value == "示例文稿":
        target.append(h)
    return True
u32.EnumChildWindows(hwnd, EnumProc(cb), 0)
print("button:", target)
if target:
    u32.SendMessageW(target[0], 0x00F5, 0, 0)  # BM_CLICK
    time.sleep(1.2)

u32.SetForegroundWindow(hwnd)
time.sleep(0.8)

img = ImageGrab.grab()
out = r"C:\Users\54881\WorkBuddy\2026-10-20-19-29-44\Teleprompter\build\shot.png"
img.save(out)
print("saved", out, img.size)
print("pid", proc.pid)
