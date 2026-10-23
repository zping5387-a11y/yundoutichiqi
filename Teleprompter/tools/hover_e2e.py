# -*- coding: utf-8 -*-
"""顶部按钮条悬停显隐验收（v1.17）

场景：
  A. 鼠标不在浮层上 → 按钮条隐藏
  B. 鼠标移到浮层上 → 按钮条出现
  C. 鼠标移到浮层外 → 按钮条延迟隐藏
  D. 鼠标再移回来 → 按钮条再次出现
"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
u32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
OVERLAY = "芸豆提词"
TOOLBAR = "提词按钮条"
SCRIPT = os.path.join(ROOT, "build", "hover_test.txt")


def text_of(h):
    n = u32.GetWindowTextLengthW(h)
    buf = ctypes.create_unicode_buffer(n + 2)
    u32.GetWindowTextW(h, buf, n + 2)
    return buf.value


def find_top(label, visible_only=True):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if (not visible_only or u32.IsWindowVisible(h)) and text_of(h) == label:
            hit.append(h)
            return False
        return True

    u32.EnumWindows(cb, 0)
    return hit[0] if hit else None


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r


def toolbar_visible():
    tb = find_top(TOOLBAR)
    return tb is not None


def move_to(x, y, wait=0.6):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(wait)


def main():
    ok = True

    with open(SCRIPT, "w", encoding="utf-8") as f:
        f.write("\n".join("悬停测试第 %d 行，用来撑出滚动内容。" % i for i in range(60)))

    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)
    env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_SCRIPT=SCRIPT)
    p = subprocess.Popen([EXE], env=env)
    time.sleep(3.0)

    try:
        ov = find_top(OVERLAY)
        if not ov:
            print("FAIL: 找不到浮层窗口")
            return 1
        r = rect(ov)
        cx, cy = (r.left + r.right) // 2, (r.top + r.bottom) // 2

        # A. 初始：鼠标在远处（浮层外）
        move_to(r.left - 200, r.top + 400, wait=0.8)
        if toolbar_visible():
            print("FAIL: 鼠标不在浮层上时按钮条也显示")
            ok = False
        else:
            print("A. 鼠标在浮层外 → 按钮条隐藏  OK")

        # B. 移到浮层上 → 显示
        move_to(cx, cy)
        if not toolbar_visible():
            print("FAIL: 鼠标移到浮层上按钮条没有出现")
            ok = False
        else:
            print("B. 鼠标在浮层上 → 按钮条出现  OK")

        # C. 移出（等 350ms 延迟 + 余量）→ 隐藏
        move_to(r.left - 200, r.top + 400, wait=1.0)
        if toolbar_visible():
            print("FAIL: 鼠标离开浮层后按钮条没有隐藏")
            ok = False
        else:
            print("C. 鼠标离开浮层 → 按钮条隐藏  OK")

        # D. 移回来 → 再显示
        move_to(cx, cy)
        if not toolbar_visible():
            print("FAIL: 鼠标再次进入浮层按钮条没有出现")
            ok = False
        else:
            print("D. 鼠标回到浮层 → 按钮条再次出现  OK")

        print("RESULT: %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1
    finally:
        p.terminate()
        time.sleep(0.3)


if __name__ == "__main__":
    sys.exit(main())
