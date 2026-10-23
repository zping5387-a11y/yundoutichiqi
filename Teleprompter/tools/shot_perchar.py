# -*- coding: utf-8 -*-
"""验证逐字高亮：把高亮锚点放在不同字符位置，截浮层局部图。"""
import ctypes
import os
import re
import subprocess
import time
from ctypes import wintypes

from PIL import ImageGrab

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
SCRIPT = os.path.join(ROOT, "build", "anchor.txt")
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")

TEXT = (
    "同学们，上课！上一节课我们讨论了物体在液体中的沉浮条件，今天这节课，"
    "我们来学习浮力的大小究竟与什么有关。"
    "先看一个生活中的现象：把一个空矿泉水瓶按进水里，手会感觉到一个向上的力。"
)

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass


def wtext(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def overlay_hwnd():
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _l):
        if u32.IsWindowVisible(h) and wtext(h) == "芸豆提词":
            hit.append(h)
            return False
        return True

    u32.EnumWindows(cb, 0)
    return hit[0] if hit else None


def main():
    with open(SCRIPT, "w", encoding="utf-8-sig", newline="\r\n") as f:
        f.write(TEXT)

    if os.path.exists(INI):
        s = open(INI, encoding="gbk", errors="ignore").read()
        s = re.sub(r"lastScript=.*", "lastScript=" + SCRIPT.replace("\\", "/"), s)
        s = re.sub(r"highlightCur=.*", "highlightCur=1", s)
        if not re.search(r"highlightCur=", s):
            s += "highlightCur=1\n"
        open(INI, "w", encoding="gbk").write(s)

    for tag, anchor in (("a", 6), ("b", 18), ("c", 31)):
        env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_ANCHOR=str(anchor))
        p = subprocess.Popen([EXE], env=env)
        time.sleep(2.6)
        ov = overlay_hwnd()
        if not ov:
            print("FAIL: 没找到浮层")
            p.terminate()
            return 1
        r = wintypes.RECT()
        u32.GetWindowRect(ov, ctypes.byref(r))
        img = ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True)
        out = os.path.join(ROOT, "build", f"shot_char_{tag}.png")
        img.save(out)
        print(f"anchor={anchor} -> {out} {img.size}")
        p.terminate()
        time.sleep(0.8)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
