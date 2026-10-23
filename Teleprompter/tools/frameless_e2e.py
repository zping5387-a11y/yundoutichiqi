# -*- coding: utf-8 -*-
"""无边框主窗口验收（v1.21，参考 mfcwnd 实现）

场景：
  A. 窗口样式：无 WS_CAPTION / WS_SYSMENU / WS_THICKFRAME
  B. 拖动：按住标题带拖动 → 窗口移动（HTCAPTION 交给系统拖动）
  C. 最大化按钮：点击右上角中间按钮 → IsZoomed 且窗口贴合工作区；再点还原
  D. 关闭按钮：点击右上角 × → 面板隐藏到托盘（进程不退出、也不是最小化）
  E. 最小化按钮：同样等于隐藏面板（不改任务栏状态）
  F. Ctrl+Alt+Q 热键仍能真正退出程序

注意：开头就 SetProcessDPIAware，全程物理坐标；
      缩放一律用「实际窗口宽 ÷ 设计宽 640」（应用有 m_uiScale 钳制，不等于 dpi/96）。
"""
import ctypes, ctypes.wintypes as wt, os, subprocess, sys, time

u32 = ctypes.windll.user32
u32.SetProcessDPIAware()          # 物理坐标空间，与被测应用的坐标一致

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"
KDESIGN_W = 640.0
ok = True

def fail(msg):
    global ok
    ok = False
    print("FAIL: " + msg)

class RECT(ctypes.Structure):
    _fields_ = [("l", ctypes.c_long), ("t", ctypes.c_long),
                ("r", ctypes.c_long), ("b", ctypes.c_long)]

def rect(h):
    r = RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return (r.l, r.t, r.r, r.b)

def style(h):
    return u32.GetWindowLongW(h, -16) & 0xFFFFFFFF

def click(x, y, settle=0.8):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.1)
    hit = u32.WindowFromPoint(wt.POINT(int(x), int(y)))
    if hit != dlg:
        cls = ctypes.create_unicode_buffer(128)
        u32.GetClassNameW(hit, cls, 128)
        print("WARN: 点击点被其它窗口挡住 cls=%s" % cls.value)
        return False
    u32.mouse_event(2, 0, 0, 0, 0)
    time.sleep(0.06)
    u32.mouse_event(4, 0, 0, 0, 0)
    time.sleep(settle)
    return True

def drag(x1, y1, x2, y2, steps=12):
    u32.SetCursorPos(int(x1), int(y1))
    time.sleep(0.12)
    u32.mouse_event(2, 0, 0, 0, 0)
    time.sleep(0.08)
    for i in range(1, steps + 1):
        u32.SetCursorPos(int(x1 + (x2 - x1) * i / steps),
                         int(y1 + (y2 - y1) * i / steps))
        time.sleep(0.02)
    u32.mouse_event(4, 0, 0, 0, 0)

subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
time.sleep(0.8)
subprocess.Popen([EXE])
time.sleep(2.5)
dlg = u32.FindWindowW(None, DLG)
if not dlg:
    print("FAIL: 找不到主窗口")
    sys.exit(1)

# 置顶 + 激活，再挪到屏幕下方空白处，避开桌面右上角的其它悬浮窗
HWND_TOPMOST = -1
u32.SetWindowPos(dlg, HWND_TOPMOST, 120, 760, 0, 0, 0x1)   # SWP_NOSIZE
u32.SetForegroundWindow(dlg)
time.sleep(0.5)

# --- A. 样式 ---
st = style(dlg)
print("样式: 0x%08X" % st)
if st & 0x00C00000: fail("仍带 WS_CAPTION（有标题栏）")
if st & 0x00080000: fail("仍带 WS_SYSMENU")
if st & 0x00040000: fail("仍带 WS_THICKFRAME（有边框）")

# --- B. 标题带拖动 ---
l, t, r, b = rect(dlg)
sc = (r - l) / KDESIGN_W
print("缩放 sc=%.4f" % sc)
before = rect(dlg)
drag(l + 120 * sc, t + 22 * sc, l + 120 * sc - 60, t + 22 * sc + 40)
time.sleep(0.4)
after = rect(dlg)
dx, dy = after[0] - before[0], after[1] - before[1]
print("拖动位移: (%d, %d)" % (dx, dy))
if abs(dx) < 30 or abs(dy) < 20:
    fail("标题带拖动无效")
else:
    print("标题带拖动 OK")

# --- C. 最大化按钮 ---
class MI(ctypes.Structure):
    _fields_ = [("cbSize", ctypes.c_uint32), ("rcMonitor", RECT),
                ("rcWork", RECT), ("dwFlags", ctypes.c_uint32)]
info = MI()
info.cbSize = ctypes.sizeof(MI)
u32.GetMonitorInfoW(u32.MonitorFromWindow(dlg, 1), ctypes.byref(info))

# 桌面上有其它置顶窗口（浏览器）挡住真实鼠标点击，为避免误点用户浏览器，
# 标题按钮改用直接投递 WM_LBUTTONDOWN/UP（应用物理客户区坐标，sc 已按实际窗口宽折算）
# 按钮锚定实际客户区右上角：right = clientW - S(16)，btn 中心 = right - (3-i)*S(36) + S(18)
WM_LBDOWN, WM_LBUP = 0x0201, 0x0202
def click_caption(btn_idx):
    rc = wt.RECT()
    u32.GetClientRect(dlg, ctypes.byref(rc))
    cw = rc.right
    cx = int(cw - (16 + (3 - btn_idx) * 36 - 18) * sc)   # 0=最小 1=最大 2=关闭，中心
    cy = int(23 * sc)
    lp = (cy << 16) | (cx & 0xFFFF)
    u32.PostMessageW(dlg, WM_LBDOWN, 1, lp)
    time.sleep(0.15)
    u32.PostMessageW(dlg, WM_LBUP, 0, lp)
    time.sleep(0.8)

click_caption(1)
if True:
    zoomed = bool(u32.IsZoomed(dlg))
    wl, wt2, wr, wb = rect(dlg)
    wa_w, wa_h = info.rcWork.r - info.rcWork.l, info.rcWork.b - info.rcWork.t
    print("最大化: IsZoomed=%s 窗口=%dx%d 工作区=%dx%d" %
          (zoomed, wr - wl, wb - wt2, wa_w, wa_h))
    if not zoomed:
        fail("最大化按钮无效")
    elif abs((wr - wl) - wa_w) > 8 or abs((wb - wt2) - wa_h) > 8:
        fail("最大化没有贴合工作区（任务栏被遮挡？）")
    else:
        print("最大化贴合工作区 OK")

    # 还原
    click_caption(1)
    if True:
        if u32.IsZoomed(dlg):
            fail("再次点击未还原")
        else:
            print("还原 OK")

# --- D/E. 关闭按钮、最小化按钮 = 隐藏面板 ---
# 应用内热键 ID：HK_TOGGLE_PANEL=17（显示/隐藏面板）、HK_QUIT=18（退出）
WM_HOTKEY, HK_TOGGLE_PANEL, HK_QUIT = 0x0312, 17, 18

def panel_state():
    """(可见, 最小化)"""
    return bool(u32.IsWindowVisible(dlg)), bool(u32.IsIconic(dlg))

def toggle_panel_hotkey():
    u32.PostMessageW(dlg, WM_HOTKEY, HK_TOGGLE_PANEL, 0)
    time.sleep(0.9)

def proc_alive():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq TeacherPrompter.exe"],
                         capture_output=True).stdout.decode("gbk", "ignore")
    return "TeacherPrompter.exe" in out

def overlay_visible():
    """提词浮层是独立窗口（标题「芸豆提词」），隐藏面板不应把它一起收走"""
    h = u32.FindWindowW(None, "芸豆提词")
    return bool(h) and bool(u32.IsWindowVisible(h))

for name, btn in (("关闭按钮 ×", 2), ("最小化按钮 —", 0)):
    vis0, ico0 = panel_state()
    ov0 = overlay_visible()
    click_caption(btn)
    time.sleep(0.9)
    vis, ico = panel_state()
    alive = proc_alive()
    ov1 = overlay_visible()
    print("%s：可见 %s→%s 最小化 %s→%s 进程存活=%s 浮层 %s→%s" %
          (name, vis0, vis, ico0, ico, alive, ov0, ov1))
    if not (vis0 and not vis):
        fail("%s没有隐藏面板" % name)
    elif ico:
        fail("%s把窗口最小化到了任务栏（应该直接隐藏）" % name)
    elif not alive:
        fail("%s把进程退出了（应只隐藏面板）" % name)
    elif ov0 and not ov1:
        fail("%s把提词浮层也收走了（浮层应继续显示）" % name)
    else:
        print("%s隐藏面板 OK" % name)

    toggle_panel_hotkey()
    vis2, _ = panel_state()
    print("   Ctrl+Alt+M 恢复：可见=%s" % vis2)
    if not vis2:
        fail("%s隐藏后无法用 Ctrl+Alt+M 调回面板" % name)

# --- F. 真退出：Ctrl+Alt+Q ---
u32.PostMessageW(dlg, WM_HOTKEY, HK_QUIT, 0)
time.sleep(1.2)
if proc_alive():
    fail("Ctrl+Alt+Q 没能退出程序（隐藏后没有真正的退出入口）")
else:
    print("Ctrl+Alt+Q 退出进程 OK")

print("RESULT: %s" % ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
