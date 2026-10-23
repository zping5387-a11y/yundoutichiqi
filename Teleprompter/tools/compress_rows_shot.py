# -*- coding: utf-8 -*-
# 验证：条目多导致行高压缩时，按钮行不得与路径行重叠
import ctypes, os, subprocess, sys, time
from ctypes import wintypes
from PIL import ImageGrab

ctypes.windll.user32.SetProcessDPIAware()
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass
u32 = ctypes.windll.user32

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DATA = os.path.join(ROOT, "build", "compress_test")
DLG = "芸豆提词器 · 教学模式"

os.makedirs(DATA, exist_ok=True)

# 造 8 个真实存在的 txt
paths = []
for i in range(8):
    p = os.path.join(DATA, "item%d.txt" % i)
    with open(p, "w", encoding="utf-8") as f:
        f.write("第 %d 篇讲稿。 今天讲一个知识点，这里有足够长的内容用来显示预览行。" % i)
    paths.append(p)

# sidecar：8 条最近记录 → 列表行高必然被压缩
with open(os.path.join(DATA, "recent.ini"), "w", encoding="utf-8") as f:
    f.write("[file]\r\nlastScript=%s\r\n" % paths[0])
    for i, p in enumerate(paths):
        f.write("recent%d=%s\r\n" % (i, p))

env = dict(os.environ)
env["TP_DATA_DIR"] = DATA
proc = subprocess.Popen([EXE], env=env)
time.sleep(3.0)


def find(title):
    res = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if u32.IsWindowVisible(h):
            n = u32.GetWindowTextLengthW(h)
            if n:
                b = ctypes.create_unicode_buffer(n + 1)
                u32.GetWindowTextW(h, b, n + 1)
                if title in b.value:
                    res.append(h)
        return True
    u32.EnumWindows(cb, 0)
    return res[0] if res else None


dlg = find(DLG)
if not dlg:
    print("FAIL: 找不到主窗口")
    proc.terminate()
    sys.exit(1)

r = wintypes.RECT()
u32.GetWindowRect(dlg, ctypes.byref(r))
p = os.path.join(ROOT, "build", "shot_compress.png")
ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(p)
print("截图: %s" % p)
proc.terminate()
print("DONE")
