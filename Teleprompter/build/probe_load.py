# -*- coding: utf-8 -*-
"""临时探针：测离线模型从点击到「跟读中」的加载耗时（不进 tools）。"""
import ctypes
import os
import subprocess
import sys
import time
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
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


def wtext(h):
    n = u32.GetWindowTextLengthW(h)
    if n <= 0:
        return ""
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def click(dlg, cid):
    h = u32.GetDlgItem(dlg, cid)
    u32.PostMessageW(dlg, 0x0111, cid, h)
    time.sleep(0.4)


subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
time.sleep(0.5)
p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1"))
dlg = None
for _ in range(40):
    time.sleep(0.3)
    dlg = u32.FindWindowW(None, DLG)
    if dlg:
        break
time.sleep(1.5)
click(dlg, 1003)   # 示例文稿
click(dlg, 1071)   # 语音页
st = u32.GetDlgItem(dlg, 1012)
t0 = time.time()
click(dlg, 1010)   # 开始跟读
while time.time() - t0 < 120:
    time.sleep(0.3)
    s = wtext(st)
    if "跟读中" in s:
        break
print("加载耗时: %.1fs | 状态: %s" % (time.time() - t0, wtext(st)))
subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
