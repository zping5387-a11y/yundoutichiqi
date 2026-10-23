# -*- coding: utf-8 -*-
"""验证单实例：第二次启动不产生第二个进程，并把已有实例的面板叫出来。"""
import ctypes, os, subprocess, time
from ctypes import wintypes

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"
ENV = dict(os.environ, TP_CAPTURE_ALLOWED="1")


def text(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def find_child(parent, label):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if text(h) == label:
            hit.append(h)
            return False
        return True

    u32.EnumChildWindows(parent, cb, 0)
    return hit[0] if hit else None


def count_procs():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq TeacherPrompter.exe"],
                         capture_output=True, text=True, errors="ignore").stdout
    return out.count("TeacherPrompter.exe")


p1 = subprocess.Popen([EXE], env=ENV)
time.sleep(3)
dlg = u32.FindWindowW(None, DLG)
print("实例A窗口:", dlg, "| 进程数:", count_procs())
if not dlg:
    p1.terminate()
    raise SystemExit(1)

# 把面板藏起来，模拟"用户忘了程序在后台"
btn = find_child(dlg, "隐藏面板")
if btn:
    u32.SendMessageW(btn, 0x00F5, 0, 0)
time.sleep(0.6)
print("隐藏后面板可见:", bool(u32.IsWindowVisible(dlg)))

# 第二次启动
p2 = subprocess.Popen([EXE], env=ENV)
time.sleep(2.5)
print("实例B是否已退出:", p2.poll() is not None, "| 进程数:", count_procs())
print("已有实例面板被叫出:", bool(u32.IsWindowVisible(dlg)))
print("实例A存活:", p1.poll() is None)

p1.terminate()
time.sleep(0.8)
print("全部退出后进程数:", count_procs())
print("DONE")
