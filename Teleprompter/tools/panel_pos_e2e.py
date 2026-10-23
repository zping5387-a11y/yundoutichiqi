# -*- coding: utf-8 -*-
"""主面板位置 + 尺寸记忆回归。

覆盖：
  1. 挪位置 + 改大小 -> 正常退出（WM_CLOSE，走 SaveAndClose）-> INI 记下 panelPos / panelSize
  2. 重新启动 -> 面板回到上次的位置和大小
  3. INI 里的位置落在屏幕外（换显示器/改分辨率的典型后果）-> 启动时夹回工作区内
  4. INI 里的尺寸大过工作区 -> 启动时按工作区收小
  5. 最大化状态下退出 -> 记住的是「还原后」的尺寸，重启后不是最大化的铺满尺寸
"""
import ctypes, os, re, subprocess, time
from ctypes import wintypes

u32 = ctypes.windll.user32
u32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                             ctypes.c_int, ctypes.c_int, wintypes.UINT]
u32.SetWindowPos.restype = wintypes.BOOL
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")
DATA = os.path.join(ROOT, "build", "testdata_panelpos")
PANEL = "芸豆提词器 · 教学模式"


def find_panel():
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _):
        n = u32.GetWindowTextLengthW(h)
        b = ctypes.create_unicode_buffer(n + 1)
        u32.GetWindowTextW(h, b, n + 1)
        if u32.IsWindowVisible(h) and b.value == PANEL:
            hit.append(h)
        return True

    u32.EnumWindows(cb, 0)
    return hit[0] if hit else None


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def size_of(h):
    r = rect(h)
    return (r[2] - r[0], r[3] - r[1])


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)


def launch():
    os.makedirs(DATA, exist_ok=True)
    env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_DATA_DIR=DATA)
    subprocess.Popen([EXE], env=env)
    time.sleep(3.2)
    return find_panel()


def read_ini():
    return open(INI, "rb").read().decode("gbk", "surrogateescape")


def write_ini(text):
    open(INI, "wb").write(text.encode("gbk", "surrogateescape"))


def _set_key(text, key, value):
    text = re.sub(r"(?im)^\s*%s\s*=.*$\r?\n?" % key, "", text)
    if value is None:
        return text
    lines = text.splitlines()
    out, done = [], False
    for ln in lines:
        out.append(ln)
        if not done and ln.strip().lower() == "[window]":
            out.append("%s=%s" % (key, value))
            done = True
    if not done:                       # 没有 [window] 段就补一个
        out.extend(["[window]", "%s=%s" % (key, value)])
    return "\r\n".join(out) + "\r\n"


def set_panel(text, pos=None, size=None):
    text = _set_key(text, "panelPos", None if pos is None else "%d,%d" % pos)
    return _set_key(text, "panelSize", None if size is None else "%d,%d" % size)


def get_pair(text, key):
    m = re.search(r"(?im)^\s*%s\s*=\s*(-?\d+)\s*,\s*(-?\d+)\s*$" % key, text)
    return (int(m.group(1)), int(m.group(2))) if m else None


def close_gracefully(h):
    """走 WM_CLOSE -> OnClose -> SaveAndClose，让设置真正落盘"""
    u32.PostMessageW(h, 0x0010, 0, 0)   # WM_CLOSE
    for _ in range(40):
        if not u32.IsWindow(h):
            break
        time.sleep(0.1)
    time.sleep(0.6)


def work_area():
    r = wintypes.RECT()
    u32.SystemParametersInfoW(0x0030, 0, ctypes.byref(r), 0)   # SPI_GETWORKAREA
    return (r.left, r.top, r.right, r.bottom)


def near(a, b, tol=1):
    return abs(a - b) <= tol


def main():
    ok = True
    backup = open(INI, "rb").read()
    try:
        # ---- 1. 没有记录 -> 挪位置 + 改大小 -> 正常退出 ----
        write_ini(set_panel(read_ini(), None, None))
        h = launch()
        if not h:
            print("FAIL: 找不到主面板")
            return 1
        wl, wt, wr, wb = work_area()
        start_w, start_h = size_of(h)
        # 目标点必须让整个窗口完整落在工作区内，否则启动时会按工作区夹取（期望值就变了）
        target = (150, 30)
        tw = min(start_w + 120, wr - wl - 200)
        th = start_h
        if target[1] + th > wb - wt:
            target = (target[0], max(0, (wb - wt) - th))
        u32.SetWindowPos(h, -1, target[0], target[1], tw, th, 0x0)   # 位置+尺寸一起改
        time.sleep(0.8)
        got = rect(h)
        print("改为: pos=%s size=%dx%d" % (got[:2], got[2] - got[0], got[3] - got[1]))
        close_gracefully(h)
        txt = read_ini()
        saved_pos, saved_size = get_pair(txt, "panelPos"), get_pair(txt, "panelSize")
        print("退出后 INI panelPos=%s panelSize=%s" % (saved_pos, saved_size))
        if not saved_pos or not near(saved_pos[0], target[0]) or not near(saved_pos[1], target[1]):
            print("   FAIL: 位置没记下来（期望 %s）" % (target,))
            ok = False
        else:
            print("   [OK] 位置已记录")
        if not saved_size or not near(saved_size[0], tw) or not near(saved_size[1], th):
            print("   FAIL: 尺寸没记下来（期望 %dx%d）" % (tw, th))
            ok = False
        else:
            print("   [OK] 尺寸已记录")

        # ---- 2. 重启 -> 位置和大小都恢复 ----
        h = launch()
        if not h:
            print("FAIL: 第二次启动找不到主面板")
            return 1
        got = rect(h)
        gw, gh = got[2] - got[0], got[3] - got[1]
        print("重启后: pos=%s size=%dx%d" % (got[:2], gw, gh))
        if not near(got[0], target[0]) or not near(got[1], target[1]):
            print("   FAIL: 没有恢复位置（期望 %s）" % (target,))
            ok = False
        else:
            print("   [OK] 位置已恢复")
        if not near(gw, tw) or not near(gh, th):
            print("   FAIL: 没有恢复尺寸（期望 %dx%d）" % (tw, th))
            ok = False
        else:
            print("   [OK] 尺寸已恢复")
        close_gracefully(h)

        # ---- 3. 记录的位置在屏幕外 -> 夹回工作区 ----
        write_ini(set_panel(read_ini(), (-4000, -3000)))
        h = launch()
        if not h:
            print("FAIL: 第三次启动找不到主面板")
            return 1
        got = rect(h)
        inside = got[0] >= wl - 1 and got[1] >= wt - 1 and got[0] < wr and got[1] < wb
        print("越界位置启动后:", got[:2], "工作区:", (wl, wt, wr, wb))
        if not inside:
            print("   FAIL: 没有夹回可见区域")
            ok = False
        else:
            print("   [OK] 位置已夹回工作区")
        close_gracefully(h)

        # ---- 4. 记录的尺寸大过工作区 -> 收小到工作区 ----
        write_ini(set_panel(read_ini(), (wl, wt), (5000, 4000)))
        h = launch()
        if not h:
            print("FAIL: 第四次启动找不到主面板")
            return 1
        gw, gh = size_of(h)
        print("超大尺寸启动后: %dx%d（工作区 %dx%d）" % (gw, gh, wr - wl, wb - wt))
        if gw > (wr - wl) + 1 or gh > (wb - wt) + 1:
            print("   FAIL: 尺寸没有被夹到工作区内")
            ok = False
        else:
            print("   [OK] 尺寸已夹到工作区内")
        close_gracefully(h)

        # ---- 5. 最大化状态下退出 -> 记住还原后的尺寸 ----
        # 先清掉记录，让窗口回到设计稿尺寸，最大化后的铺满尺寸才和它有明显区别
        write_ini(set_panel(read_ini(), None, None))
        h = launch()
        if not h:
            print("FAIL: 第五次启动找不到主面板")
            return 1
        normal_w, normal_h = size_of(h)
        u32.ShowWindow(h, 3)                     # SW_MAXIMIZE
        time.sleep(1.0)
        max_w, max_h = size_of(h)
        print("最大化后 %dx%d（还原态 %dx%d）" % (max_w, max_h, normal_w, normal_h))
        close_gracefully(h)
        saved_size = get_pair(read_ini(), "panelSize")
        print("最大化退出后 INI panelSize =", saved_size)
        if not saved_size or not near(saved_size[0], normal_w, 3) or not near(saved_size[1], normal_h, 3):
            print("   FAIL: 记成了最大化尺寸（期望 %dx%d）" % (normal_w, normal_h))
            ok = False
        else:
            print("   [OK] 记的是还原后的尺寸")

        h = launch()
        if not h:
            print("FAIL: 第六次启动找不到主面板")
            return 1
        gw, gh = size_of(h)
        maximized = bool(u32.IsZoomed(h))
        print("最大化退出后重启: %dx%d zoomed=%d" % (gw, gh, maximized))
        if maximized or not near(gw, normal_w, 3) or not near(gh, normal_h, 3):
            print("   FAIL: 没有按还原尺寸恢复")
            ok = False
        else:
            print("   [OK] 按还原尺寸恢复")
        close_gracefully(h)
    finally:
        kill()
        open(INI, "wb").write(backup)

    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
