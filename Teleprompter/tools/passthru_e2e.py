# -*- coding: utf-8 -*-
"""端到端验证「鼠标穿透为可选」：

  默认（穿透开）  → 浮层中心 WindowFromPoint 不命中浮层（点击落到下层）
  取消勾选穿透    → WindowFromPoint 命中浮层，滚轮 PostMessage 能滚动内容
  重新勾选        → 恢复穿透
"""
import ctypes, os, subprocess, time
from ctypes import wintypes
from PIL import ImageGrab, ImageChops

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"
OVERLAY = "芸豆提词"
IDC_TAB_SETTINGS = 1072     # 左侧页签「设置」（1070 稿件 / 1071 语音 / 1072 设置）

WM_MOUSEWHEEL = 0x020A


class POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


def text(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def find_top(label):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if u32.IsWindowVisible(h) and text(h) == label:
            hit.append(h)
            return False
        return True

    u32.EnumWindows(cb, 0)
    return hit[0] if hit else None


def find_child(parent, label):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if label in text(h):
            hit.append(h)
            return False
        return True

    u32.EnumChildWindows(parent, cb, 0)
    return hit[0] if hit else None


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r


def window_at(x, y):
    h = u32.WindowFromPoint(POINT(x, y))
    return h, text(h)


def overlay_center(ov):
    r = rect(ov)
    return (r.left + r.right) // 2, (r.top + r.bottom) // 2


def ex_style(h):
    return u32.GetWindowLongW(h, -20)   # GWL_EXSTYLE


def click_check(chk):
    """真实鼠标点击（拖动测试后 BM_CLICK 合成消息会被吞一次，真实点击无此问题）"""
    r = rect(chk)
    u32.SetCursorPos((r.left + r.right) // 2, (r.top + r.bottom) // 2)
    time.sleep(0.15)
    u32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.08)
    u32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.5)
    state = u32.SendMessageW(chk, 0x00F0, 0, 0)   # BM_GETCHECK
    return state


def open_settings_page(dlg):
    """「鼠标穿透」复选框在「设置」页 —— 先真实点击左侧页签切过去"""
    tab = u32.GetDlgItem(dlg, IDC_TAB_SETTINGS)
    if not tab:
        print("FAIL: 找不到「设置」页签")
        return False
    r = rect(tab)
    u32.SetCursorPos((r.left + r.right) // 2, (r.top + r.bottom) // 2)
    time.sleep(0.15)
    u32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.08)
    u32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.5)
    return bool(u32.IsWindowVisible(tab))


def wheel(ov, delta):
    wp = (((delta & 0xFFFF) << 16) & 0xFFFFFFFF)
    for _ in range(6):
        u32.PostMessageW(ov, WM_MOUSEWHEEL, wp, 0)
        time.sleep(0.08)


ok = True
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")


def set_ini_click_through(val):
    """进程被强杀时设置不落盘，测试前固定 INI 初始状态"""
    try:
        with open(INI, encoding="gbk", errors="ignore") as f:
            lines = f.readlines()
        out, seen = [], False
        for ln in lines:
            if ln.strip().lower().startswith("clickthrough="):
                out.append("clickThrough=%d\n" % val)
                seen = True
            else:
                out.append(ln)
        if not seen:
            out.append("clickThrough=%d\n" % val)
        with open(INI, "w", encoding="gbk", errors="ignore") as f:
            f.writelines(out)
    except FileNotFoundError:
        pass


subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)
set_ini_click_through(1)
time.sleep(0.5)

p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1"))
time.sleep(3.0)

dlg = find_top(DLG)
ov = find_top(OVERLAY)
if not dlg or not ov:
    print("FAIL: 找不到主窗口或浮层（dlg=%s ov=%s）" % (bool(dlg), bool(ov)))
    p.terminate()
    raise SystemExit(1)

btn = find_child(dlg, "示例文稿")
if btn:
    u32.SendMessageW(btn, 0x00F5, 0, 0)   # BM_CLICK
    time.sleep(0.8)
    ov = find_top(OVERLAY)

cx, cy = overlay_center(ov)

# ---- 1) 默认穿透开：不命中浮层 ----
h, t = window_at(cx, cy)
print("默认：浮层中心 WindowFromPoint = %r" % t)
if h == ov:
    print("FAIL: 默认状态浮层仍拦截鼠标（穿透没生效）")
    ok = False

# ---- 2) 取消勾选：命中浮层 + 滚轮能滚 ----
open_settings_page(dlg)              # 复选框在「设置」页，先切过去
chk = find_child(dlg, "鼠标穿透")
if not chk:
    print("FAIL: 找不到「鼠标穿透」复选框")
    ok = False
else:
    st0 = click_check(chk)
    print("取消勾选后：复选框状态=%d EXSTYLE=0x%X" % (st0, ex_style(ov)))

    h2, t2 = window_at(cx, cy)
    print("关闭穿透后：WindowFromPoint = %r" % t2)
    if h2 != ov:
        print("FAIL: 关闭穿透后浮层仍收不到鼠标")
        ok = False

    r = rect(ov)
    grab = lambda: ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True)
    before = grab()
    wheel(ov, -360)   # 连续 6 次 -120，模拟滚轮向下
    time.sleep(0.8)
    after = grab()
    diff = ImageChops.difference(before.convert("RGB"), after.convert("RGB"))
    moved = diff.getbbox() is not None
    print("滚轮滚动：画面变化 = %s" % ("是" if moved else "否"))
    if not moved:
        print("FAIL: 关闭穿透后滚轮没有滚动内容")
        ok = False
    after.save(os.path.join(ROOT, "build", "shot_passthru_wheel.png"))

    # ---- 2b) 按住拖动：内容跟手（向上拖 → 内容上移 / 视图下移） ----
    if os.environ.get("SKIP_DRAG"):
        print("(跳过拖动测试)")
    else:
        u32.SetCursorPos(cx, cy)
        time.sleep(0.2)
        before_drag = grab()
        u32.mouse_event(0x0002, 0, 0, 0, 0)          # LEFTDOWN
        time.sleep(0.15)
        for i in range(1, 7):                        # 向上拖 240px，分 6 步
            u32.SetCursorPos(cx, cy - 40 * i)
            time.sleep(0.06)
        u32.mouse_event(0x0004, 0, 0, 0, 0)          # LEFTUP
        time.sleep(0.5)
        after_drag = grab()
        diff = ImageChops.difference(before_drag.convert("RGB"), after_drag.convert("RGB"))
        moved = diff.getbbox() is not None
        print("按住拖动：画面变化 = %s" % ("是" if moved else "否"))
        if not moved:
            print("FAIL: 拖动没有带动内容")
            ok = False
        after_drag.save(os.path.join(ROOT, "build", "shot_passthru_drag.png"))
        print("拖动后：复选框状态=%d EXSTYLE=0x%X"
              % (u32.SendMessageW(chk, 0x00F0, 0, 0), ex_style(ov)))

        # 拖回原处附近（向下拖），顺便验证双向
        u32.SetCursorPos(cx, cy - 240)
        u32.mouse_event(0x0002, 0, 0, 0, 0)
        time.sleep(0.15)
        for i in range(1, 7):
            u32.SetCursorPos(cx, cy - 240 + 40 * i)
            time.sleep(0.06)
        u32.mouse_event(0x0004, 0, 0, 0, 0)
        time.sleep(0.5)

    # 点击落在浮层上（不再穿到下层）——由 WindowFromPoint 命中已验证

# ---- 3) 重新勾选：恢复穿透 ----
if chk:
    state = click_check(chk)
    h3, t3 = window_at(cx, cy)
    print("重新开启穿透：WindowFromPoint = %r，复选框状态=%d，EXSTYLE=0x%X"
          % (t3, state, ex_style(ov)))
    if h3 == ov:
        print("FAIL: 重新勾选后穿透没有恢复")
        ok = False

# 面板截图（新复选框）
rd = rect(dlg)
ImageGrab.grab(bbox=(rd.left, rd.top, rd.right, rd.bottom), all_screens=True).save(
    os.path.join(ROOT, "build", "shot_passthru_panel.png"))

time.sleep(0.3)
p.terminate()

print("RESULT:", "PASS" if ok else "FAIL")
raise SystemExit(0 if ok else 1)
