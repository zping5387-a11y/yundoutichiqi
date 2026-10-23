# -*- coding: utf-8 -*-
"""麦克风采集 + 电平 + 识别链路实测"""
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

ID_CMB_MIC, ID_BTN_SPEECH, ID_ST_LEVEL, ID_ST_HEARD, ID_ST_SPEECH = 1014, 1010, 1015, 1013, 1012
CB_SETCURSEL, CBN_SELCHANGE, BM_CLICK, WM_COMMAND = 0x014E, 1, 0x00F5, 0x0111


def text_of(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def combo_items(h):
    n = u32.SendMessageW(h, 0x0146, 0, 0)  # CB_GETCOUNT
    out = []
    for i in range(n):
        length = u32.SendMessageW(h, 0x0149, i, 0)  # CB_GETLBTEXTLEN
        buf = ctypes.create_unicode_buffer(length + 1)
        u32.SendMessageW(h, 0x0148, i, ctypes.byref(buf))  # CB_GETLBTEXT
        out.append(buf.value)
    return out


def select_combo(dlg, cid, index):
    h = u32.GetDlgItem(dlg, cid)
    u32.SendMessageW(h, CB_SETCURSEL, index, 0)
    u32.SendMessageW(dlg, WM_COMMAND, (CBN_SELCHANGE << 16) | cid, h)


def click_text(dlg, label):
    res = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if text_of(h) == label:
            res.append(h)
            return False
        return True

    u32.EnumChildWindows(dlg, cb, 0)
    if res:
        u32.SendMessageW(res[0], BM_CLICK, 0, 0)
        time.sleep(0.6)
    return bool(res)


p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1"))
time.sleep(3)
dlg = u32.FindWindowW(None, DLG)
click_text(dlg, "示例文稿")       # 先载入稿件，否则跟读会被拦下

items = combo_items(u32.GetDlgItem(dlg, ID_CMB_MIC))
print("麦克风列表:")
for i, it in enumerate(items):
    print("  [%d] %s" % (i, it))

# 选择名字里带 USB Microphone 的那一项
target = 0
for i, it in enumerate(items):
    if "USB Microphone" in it:
        target = i
        break
print("选择索引:", target, items[target] if items else "")
select_combo(dlg, ID_CMB_MIC, target)
time.sleep(0.5)

u32.SendMessageW(u32.GetDlgItem(dlg, ID_BTN_SPEECH), BM_CLICK, 0, 0)
time.sleep(2)
print("状态:", text_of(u32.GetDlgItem(dlg, ID_ST_SPEECH)))
print("电平:", text_of(u32.GetDlgItem(dlg, ID_ST_LEVEL)))

# 用扬声器播一段中文，让麦克风拾音
wav = os.path.join(ROOT, "build", "tts_long.wav")
player = subprocess.Popen(["powershell", "-NoProfile", "-Command",
                           "(New-Object Media.SoundPlayer '%s').PlaySync()" % wav])
peak = 0
heard = ""
for _ in range(40):
    time.sleep(0.3)
    lv = text_of(u32.GetDlgItem(dlg, ID_ST_LEVEL))
    h = text_of(u32.GetDlgItem(dlg, ID_ST_HEARD))
    if h:
        heard = h
    import re
    m = re.search(r"(\d+)%", lv or "")
    if m:
        peak = max(peak, int(m.group(1)))
player.wait()

print("最大电平: %d%%" % peak)
print("识别结果:", heard if heard else "(无)")
print("最终状态:", text_of(u32.GetDlgItem(dlg, ID_ST_SPEECH)))
u32.SendMessageW(u32.GetDlgItem(dlg, ID_BTN_SPEECH), BM_CLICK, 0, 0)
time.sleep(0.5)
p.terminate()
print("DONE")
