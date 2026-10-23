# -*- coding: utf-8 -*-
"""光标回归：浮层在三种状态下鼠标光标是否正确。

覆盖：
  1. 锁定 + 鼠标穿透（默认）-> 鼠标完全穿透，光标由下面的程序决定（不做断言，只报告）
  2. 锁定 + 不穿透          -> 浮层上任意位置都应是普通箭头（以前是上下双箭头，像在等你去拉高度）
  3. 调整位置（interactive） -> 内部普通箭头；左右/上下边缘、四角才换成对应缩放光标
"""
import ctypes, os, re, subprocess, time
from ctypes import wintypes

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")
DATA = os.path.join(ROOT, "build", "testdata_cursor")
OVERLAY = "芸豆提词"

STD = {"ARROW": 32512, "SIZENS": 32645, "SIZEWE": 32644,
       "SIZEALL": 32646, "SIZENWSE": 32642, "SIZENESW": 32643}
CURSORS = {n: u32.LoadCursorW(None, i) for n, i in STD.items()}


class CURSORINFO(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("flags", wintypes.DWORD),
                ("hCursor", wintypes.HANDLE), ("ptScreenPos", wintypes.POINT)]


def cursor_name():
    ci = CURSORINFO(ctypes.sizeof(CURSORINFO))
    u32.GetCursorInfo(ctypes.byref(ci))
    for n, h in CURSORS.items():
        if h == ci.hCursor:
            return n
    return "OTHER"


def window_text(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def window_rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def find_window(label):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _):
        if u32.IsWindowVisible(h) and window_text(h) == label:
            hit.append(h)
        return True

    u32.EnumWindows(cb, 0)
    return hit[0] if hit else None


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def write_view(locked, click_through):
    base = open(INI, "rb").read().decode("gbk", "surrogateescape")
    base = re.sub(r"(?im)^\s*locked\s*=.*$", "", base)
    base = re.sub(r"(?im)^\s*clickThrough\s*=.*$", "", base)
    out, done = [], False
    for ln in base.splitlines():
        out.append(ln)
        if not done and ln.strip().lower() == "[view]":
            out.append("locked=%d" % locked)
            out.append("clickThrough=%d" % click_through)
            done = True
    open(INI, "wb").write(("\r\n".join(out) + "\r\n").encode("gbk", "surrogateescape"))


def sample(ov):
    """返回 {位置: 光标名}

    光标形状由 WM_SETCURSOR 逐个鼠标移动事件更新，SetCursorPos 之后
    偶尔会漏掉一次投递（读到上一个位置残留的光标）。这里每个点先做
    1px 抖动再连取 3 次，取出现最多的结果，避免偶发误判。
    """
    ol, ot, orr, ob = window_rect(ov)
    cx, cy = (ol + orr) // 2, (ot + ob) // 2
    pts = [("center", (cx, cy)), ("left", (ol + 4, cy)), ("right", (orr - 4, cy)),
           ("top", (cx, ot + 4)), ("bottom", (cx, ob - 4)),
           ("left-top", (ol + 3, ot + 3)), ("right-bottom", (orr - 3, ob - 3))]
    res = {}
    for name, (x, y) in pts:
        seen = []
        for i in range(3):
            u32.SetCursorPos(int(x), int(y) + (1 if i % 2 else -1))   # 抖动 1px 触发 WM_SETCURSOR
            time.sleep(0.12)
            u32.SetCursorPos(int(x), int(y))
            time.sleep(0.22)
            seen.append(cursor_name())
        res[name] = max(set(seen), key=seen.count)                    # 取众数
    return res


def launch():
    os.makedirs(DATA, exist_ok=True)
    env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_DATA_DIR=DATA)
    subprocess.Popen([EXE], env=env)
    time.sleep(3.2)
    return find_window(OVERLAY)


def main():
    ok = True
    backup = open(INI, "rb").read()
    try:
        # 2. 锁定 + 不穿透：全部普通箭头
        write_view(1, 0)
        ov = launch()
        if not ov:
            print("FAIL: 找不到浮层")
            return 1
        got = sample(ov)
        bad = {k: v for k, v in got.items() if v != "ARROW"}
        print("锁定+不穿透:", got)
        if bad:
            print("   FAIL: 以下位置光标应为 ARROW：%s" % bad)
            ok = False
        else:
            print("   [OK] 全部为普通箭头")
        kill()
        time.sleep(0.6)

        # 3. 调整位置：内部箭头，边缘/角落对应缩放光标
        write_view(0, 0)
        ov = launch()
        if not ov:
            print("FAIL: 找不到浮层")
            return 1
        got = sample(ov)
        print("调整位置   :", got)
        expect = {"center": "ARROW", "left": "SIZEWE", "right": "SIZEWE",
                  "top": "SIZENS", "bottom": "SIZENS",
                  "left-top": "SIZENWSE", "right-bottom": "SIZENWSE"}
        bad = {k: got.get(k) for k, v in expect.items() if got.get(k) != v}
        if bad:
            print("   FAIL: 期望 %s，实际不符 %s" % (expect, bad))
            ok = False
        else:
            print("   [OK] 内部箭头、边缘/四角缩放光标正确")
        kill()
        time.sleep(0.6)

        # 1. 锁定 + 穿透：仅报告（穿透时光标由下面程序决定）
        write_view(1, 1)
        ov = launch()
        if ov:
            print("锁定+穿透  :", sample(ov), "（穿透态，光标来自下层程序，不作断言）")
        kill()
    finally:
        open(INI, "wb").write(backup)
        kill()

    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
