# -*- coding: utf-8 -*-
"""在有输入信号时抓取控制台截图（回环自检）"""
import ctypes, os, re, subprocess, time, winsound
from ctypes import wintypes

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"


def t(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def click(dlg, label):
    r = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if t(h) == label:
            r.append(h)
            return False
        return True

    u32.EnumChildWindows(dlg, cb, 0)
    if r:
        u32.SendMessageW(r[0], 0x00F5, 0, 0)
        time.sleep(0.6)


p = subprocess.Popen([EXE], env=dict(os.environ, TP_MIC_LOOPBACK="1", TP_CAPTURE_ALLOWED="1"))
time.sleep(3)
dlg = u32.FindWindowW(None, DLG)
click(dlg, "示例文稿")
click(dlg, "开始跟读")
winsound.PlaySound(os.path.join(ROOT, "build", "tts_long.wav"), winsound.SND_FILENAME | winsound.SND_ASYNC)

from PIL import ImageGrab
shot = False
for _ in range(200):
    lv = t(u32.GetDlgItem(dlg, 1015))
    m = re.search(r"(\d+)%", lv or "")
    if m and int(m.group(1)) >= 25:
        ImageGrab.grab().save(os.path.join(ROOT, "build", "shot_mic.png"))
        shot = True
        break
    time.sleep(0.08)

print("captured at signal:", shot)
print("status:", t(u32.GetDlgItem(dlg, 1012)))
print("level :", t(u32.GetDlgItem(dlg, 1015)))
print("heard :", t(u32.GetDlgItem(dlg, 1013)))
p.terminate()
