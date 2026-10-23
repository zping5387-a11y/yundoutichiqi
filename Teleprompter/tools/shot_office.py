# -*- coding: utf-8 -*-
"""用真实 Word / PPT 文件启动提词器并截图（人工核对抽出来的稿子长什么样）

用法：shot_office.py <文稿路径> [输出图片]
"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes
from PIL import ImageGrab

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
OVERLAY = "芸豆提词"

path = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build", "shot_office.png")

subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)

env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_SCRIPT=path)
p = subprocess.Popen([EXE], env=env)
time.sleep(3.5)

hit = []


def text_of(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


@ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def cb(h, l):
    if u32.IsWindowVisible(h) and text_of(h) == OVERLAY:
        hit.append(h)
        return False
    return True


u32.EnumWindows(cb, 0)
if not hit:
    print("FAIL: 浮层没有出现（文稿可能没载入）")
    p.terminate()
    raise SystemExit(1)

r = wintypes.RECT()
u32.GetWindowRect(hit[0], ctypes.byref(r))
img = ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True)
img.save(out)
print("saved %s  (%dx%d)" % (out, img.width, img.height))

p.terminate()
time.sleep(0.3)
