# -*- coding: utf-8 -*-
"""稿件列表滚动条验收：面板「最近文稿」 + 浮层「切换稿件」弹窗

面板：预设 8 条最近文稿（列表一屏只放得下 3 行），
  1. 截图确认右侧出现滚动条
  2. 向列表投递 VK_END → 应滚动到末尾、第 8 行完整可见
  3. 双击第 8 行 → 稿件行变成第 8 篇（证明之前看不到的行现在点得到）

弹窗：把浮层挪到屏幕底部再点「切换稿件」→ 窗口应被压到可用高度、
  右侧出现滚动条；滚轮向下滚 3 格 → 点第一可视行应载入第 4 篇。
"""
import ctypes, os, re, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
u32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
u32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                             ctypes.c_int, ctypes.c_int, wintypes.UINT]
u32.SetWindowPos.restype = wintypes.BOOL
u32.mouse_event.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                            wintypes.DWORD, ctypes.c_size_t]
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

from PIL import ImageGrab

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")
RECENT = os.path.join(ROOT, "build", "scroll_list")
DATA = os.path.join(ROOT, "build", "testdata_scroll")
DLG = "芸豆提词器 · 教学模式"
OVERLAY = "芸豆提词"
TOOLBAR = "提词按钮条"
MENU = "提词文稿列表"
IDC_ST_SCRIPT = 1004
IDC_LST_RECENT = 1074
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_WHEEL = 0x0800
VK_END = 0x23

ok = True


def text_of(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def grab(h, name):
    l, t, r, b = rect(h)
    p = os.path.join(ROOT, "build", name)
    ImageGrab.grab(bbox=(l, t, r, b), all_screens=True).save(p)
    print("   截图：%s" % p)
    return p


def click(x, y, settle=0.45):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.08)
    u32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.04)
    u32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    time.sleep(settle)


def dblclick(x, y, settle=0.9):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.1)
    for _ in range(2):
        u32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        time.sleep(0.03)
        u32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        time.sleep(0.08)
    time.sleep(settle)


def wheel(x, y, delta):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.1)
    u32.mouse_event(MOUSEEVENTF_WHEEL, 0, 0, delta & 0xFFFFFFFF, 0)
    time.sleep(0.4)


def ini_read(path):
    if not os.path.exists(path):
        return ""
    return open(path, "rb").read().decode("gbk", "surrogateescape")


def ini_set_file_section(path, last, recents):
    src = ini_read(path)
    lines = [l for l in src.splitlines()
             if not re.match(r"(?i)^\s*(recent\d+|lastScript)\s*=", l)]
    want = ["lastScript=" + last] + ["recent%d=%s" % (i, p) for i, p in enumerate(recents)]
    out, done = [], False
    for ln in lines:
        out.append(ln)
        if not done and ln.strip().lower() == "[file]":
            out.extend(want)
            done = True
    if not done:
        out.append("[file]")
        out.extend(want)
    open(path, "wb").write(("\r\n".join(out) + "\r\n").encode("gbk", "surrogateescape"))


def find_top(label):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if u32.IsWindowVisible(h) and text_of(h) == label:
            hit.append(h)
            return False
        return True

    u32.EnumWindows(cb, 0)
    return hit[0] if hit else None


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.6)


def script_label():
    dlg = u32.FindWindowW(None, DLG)
    h = u32.GetDlgItem(dlg, IDC_ST_SCRIPT) if dlg else None
    return text_of(h) if h else ""


# ---------------------------------- 准备 ----------------------------------
kill()
N = 8
os.makedirs(RECENT, exist_ok=True)
samples = []
for i in range(N):
    p = os.path.join(RECENT, "%d.txt" % (i + 1))
    with open(p, "w", encoding="utf-8-sig") as f:
        f.write("第%d篇讲稿：滚动验收用。" % (i + 1))
    samples.append(p)

if os.path.isdir(DATA):
    for f in os.listdir(DATA):
        os.remove(os.path.join(DATA, f))
os.makedirs(DATA, exist_ok=True)

ini_backup = ini_read(INI)      # 测试结束后还原，不污染其它测试/真实使用
ini_set_file_section(INI, samples[0], samples)

env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_DATA_DIR=DATA)
p = subprocess.Popen([EXE], env=env)
time.sleep(3.0)

try:
    # ================= 面板「最近文稿」滚动条 =================
    dlg = u32.FindWindowW(None, DLG)
    if not dlg:
        print("FAIL: 找不到主面板"); raise SystemExit(1)
    u32.SetWindowPos(dlg, -1, 120, 100, 0, 0, 0x1)   # 挪到左上，避开右侧浮层且完整可见
    u32.SetForegroundWindow(dlg)
    time.sleep(0.5)

    lst = u32.GetDlgItem(dlg, IDC_LST_RECENT)
    if not lst:
        print("FAIL: 找不到最近文稿列表"); raise SystemExit(1)
    l, t, r, b = rect(lst)
    s = (r - l) / 512.0
    print("列表 rect=(%d,%d,%d,%d) 缩放s=%.4f" % (l, t, r, b, s))
    print("初始稿件行：%s" % script_label())
    grab(lst, "shot_scroll_panel.png")

    # VK_END → 滚到末尾
    u32.PostMessageW(lst, 0x0100, VK_END, 0)      # WM_KEYDOWN（无须焦点）
    time.sleep(0.6)
    grab(lst, "shot_scroll_panel_end.png")

    # 双击第 8 行（应已在底部完整可见；点行中部，避开按钮行与 ×）
    row_h = 72 * s
    dblclick(l + (r - l) / 2.0, b - row_h / 2.0 - 2)
    got = script_label()
    print("滚动到底后双击第 8 行 → 稿件行：%s" % got)
    if "第8篇" not in got and "8.txt" not in got:
        print("FAIL: 第 8 行未通过滚动可见/可点"); ok = False
    else:
        print("   [OK] 面板列表滚动条生效")

    # ================= 浮层「切换稿件」弹窗滚动条 =================
    ov = find_top(OVERLAY)
    if not ov:
        print("FAIL: 找不到浮层"); ok = False
    else:
        sw, sh = u32.GetSystemMetrics(0), u32.GetSystemMetrics(1)
        ol, ot, orr, ob = rect(ov)
        # 挪到屏幕中部偏下：按钮条（贴浮层上缘）下方只剩 ~740px，
        # 放不下 920px 的满高菜单 → 必须压缩进入滚动模式
        u32.SetWindowPos(ov, -1, 200, 650, 0, 0, 0x1)
        time.sleep(1.2)

        # 按钮条默认隐藏：先把鼠标悬到浮层上唤醒，再找它、点最右「切换稿件」
        orc = rect(ov)
        u32.SetCursorPos(int((orc[0] + orc[2]) / 2), int((orc[1] + orc[3]) / 2))
        time.sleep(0.8)
        tb = find_top(TOOLBAR)
        if not tb:
            print("FAIL: 悬停后按钮条仍未显示"); ok = False
        else:
            tr = rect(tb)
            print("按钮条 rect=(%d,%d,%d,%d)" % tr)
            cell = (tr[2] - tr[0]) / 5.0
            menu = None
            for attempt in range(3):
                # 先把鼠标移到按钮上（悬停高亮），再按下抬起
                bx, by = tr[0] + cell * 4.5, (tr[1] + tr[3]) / 2.0
                u32.SetCursorPos(int(bx), int(by))
                time.sleep(0.5)
                click(bx, by, settle=0.7)
                menu = find_top(MENU)
                if menu:
                    break
                print("   第 %d 次点击没弹出列表，重试" % (attempt + 1))
                # 悬停可能已超时隐藏，重新唤醒
                u32.SetCursorPos(int((orc[0] + orc[2]) / 2), int((orc[1] + orc[3]) / 2))
                time.sleep(0.8)
                tb2 = find_top(TOOLBAR)
                if tb2:
                    tr = rect(tb2)
            if not menu:
                print("FAIL: 弹窗没打开"); ok = False
            else:
                ml, mt, mr, mb = rect(menu)
                print("弹窗 rect=(%d,%d,%d,%d) 高=%d" % (ml, mt, mr, mb, mb - mt))
                grab(menu, "shot_scroll_menu.png")

                # 满高度（9 行）应放不下 → 窗口被压矮
                msc = (u32.GetDpiForWindow(menu) or 96) / 96.0
                full_h = (2 * 8 + 30 + 9 * 46) * msc   # pad*2 + 标题 + 9 行
                if mb - mt >= full_h - 2:
                    print("FAIL: 弹窗没有压缩高度（未进入滚动模式）"); ok = False
                else:
                    print("   [OK] 弹窗压到 %d < 满高 %.0f，进入滚动模式" % (mb - mt, full_h))

                # 滚轮向下 3 格 → 首个可视行 = 第 4 条（recent3 = 4.txt）
                for _ in range(3):
                    wheel((ml + mr) / 2.0, (mt + mb) / 2.0, -120)
                grab(menu, "shot_scroll_menu_wheeled.png")

                pad, title, row = 8 * msc, 30 * msc, 46 * msc
                click((ml + mr) / 2.0, mt + pad + title + row * 0.5)
                time.sleep(0.6)
                got2 = script_label()
                print("滚轮 3 格后点首行 → 稿件行：%s" % got2)
                if "4.txt" not in got2 and "第4篇" not in got2:
                    print("FAIL: 滚轮滚动未生效"); ok = False
                else:
                    print("   [OK] 弹窗滚轮滚动生效")
finally:
    kill()
    open(INI, "wb").write(ini_backup.encode("gbk", "surrogateescape"))

print("RESULT: %s" % ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
