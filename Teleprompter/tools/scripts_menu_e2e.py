# -*- coding: utf-8 -*-
"""端到端：浮层顶部「切换稿件」按钮 → 弹出最近文稿列表 → 点一下切换

步骤：
  1. 预先把 INI 的 [file] 段写成 lastScript=a.txt、recent0=a.txt、recent1=b.txt
     （直接改文件而不是等程序自己记住，测试才不依赖历史状态）
  2. 启动后用真实鼠标点按钮条最右边的「切换稿件」，等「提词文稿列表」窗口出现
  3. 点列表第 2 项（b.txt）→ 面板「稿件」行应该变成 b.txt
  4. 再打开一次点第 1 项（a.txt）→ 切回 a.txt
  5. 再打开一次，点列表外面 → 列表收起，稿件不变

注意：菜单靠 SetCapture 收鼠标（浮层是穿透的），所以点“外面”也必须用真实鼠标事件，
PostMessage 那种注入消息不算数。
"""
import ctypes, os, re, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
u32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
u32.mouse_event.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                            wintypes.DWORD, ctypes.c_size_t]
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")
RECENT = os.path.join(ROOT, "build", "recent")
SHOT = os.path.join(ROOT, "build", "shot_scriptmenu.png")

DLG = "芸豆提词器 · 教学模式"
OVERLAY = "芸豆提词"
TOOLBAR = "提词按钮条"
MENU = "提词文稿列表"
IDC_ST_SCRIPT = 1004

MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004

ok = True


def text_of(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


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


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r


def dpi_scale(h):
    try:
        dpi = u32.GetDpiForWindow(h)
    except Exception:
        dpi = 96
    return (dpi or 96) / 96.0


def click(x, y, settle=0.45):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.06)
    u32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.04)
    u32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    time.sleep(settle)


# ---------------- INI（GBK 字节，和 GetPrivateProfile* 一致） ----------------
def ini_read(path):
    if not os.path.exists(path):
        return ""
    return open(path, "rb").read().decode("gbk", "surrogateescape")


def ini_write(path, s):
    open(path, "wb").write(s.encode("gbk", "surrogateescape"))


def ini_file_section(path):
    """取主 INI 的 [file] 段原文，用于断言程序没有改写用户的稿件定义"""
    m = re.search(r"(?ims)^\s*\[file\]\s*$(.*?)(?=^\s*\[|\Z)", ini_read(path))
    return m.group(1).strip() if m else ""


def ini_set_file_section(path, last, recents):
    """把 [file] 段的 lastScript / recentN 重写成本次测试的值，其它键原样保留"""
    src = ini_read(path)
    lines = [l for l in src.splitlines()
             if not re.match(r"(?i)^\s*(recent\d+|lastScript)\s*=", l)]

    want = ["lastScript=" + last]
    want += ["recent%d=%s" % (i, p) for i, p in enumerate(recents)]

    out, done = [], False
    for ln in lines:
        out.append(ln)
        if not done and ln.strip().lower() == "[file]":
            out.extend(want)
            done = True
    if not done:
        out.append("[file]")
        out.extend(want)
    ini_write(path, "\r\n".join(out) + "\r\n")


# ---------------- 稿件样本 ----------------
A = ["第一篇文章：今天讲二次函数。",
     "开口方向看二次项系数，a 大于零开口向上，a 小于零开口向下。",
     "对称轴是 x 等于负 b 除以 2a。"]
B = ["第二篇文章：今天我们聊聊概率。",
     "古典概型要求样本空间有限、每个样本点等可能。",
     "互斥事件不能同时发生，独立事件互不影响。"]


def make_samples():
    os.makedirs(RECENT, exist_ok=True)
    pa = os.path.join(RECENT, "a.txt")
    pb = os.path.join(RECENT, "b.txt")
    open(pa, "w", encoding="utf-8-sig").write("\n".join(A))
    open(pb, "w", encoding="utf-8-sig").write("\n".join(B))
    return pa, pb


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.6)


def script_label():
    dlg = u32.FindWindowW(None, DLG)
    h = u32.GetDlgItem(dlg, IDC_ST_SCRIPT) if dlg else None
    return text_of(h) if h else ""


def open_menu(toolbar):
    """点按钮条最右一格（切换稿件），返回菜单窗口句柄。
    按钮条现在是「鼠标悬停浮层才显示」：先把鼠标移到浮层上唤醒它。"""
    ov = find_top(OVERLAY)
    if ov:
        orc = rect(ov)
        u32.SetCursorPos(int((orc.left + orc.right) / 2), int((orc.top + orc.bottom) / 2))
        time.sleep(0.5)
    r = rect(toolbar)
    s = dpi_scale(toolbar)
    cell = (r.right - r.left) / 5.0
    click(r.left + cell * 4.5, (r.top + r.bottom) / 2.0)
    time.sleep(0.45)
    return find_top(MENU)


def row_point(menu, idx):
    """列表第 idx 行的屏幕坐标中心（行高布局见 COverlayScriptMenu::RowRect）"""
    r = rect(menu)
    s = dpi_scale(menu)
    pad, title, row = 8 * s, 30 * s, 46 * s
    y = r.top + pad + title + row * (idx + 0.5)
    return (r.left + r.right) / 2.0, y


# ---------------------------------- 主流程 ----------------------------------
kill()
pa, pb = make_samples()
orig = ini_read(INI)
ini_set_file_section(INI, pa, [pa, pb])
ini_after_set = ini_file_section(INI)     # 本次测试写下的「稿件定义」，运行期不许被改写

# 「最近文稿」sidecar 放到独立目录并清空：列表条数只由本脚本写的 INI 预设决定，
# 不受上一次运行 / 真实使用历史影响（主 INI 的 [file] 段程序只读，不再被改写）
DATA = os.path.join(ROOT, "build", "testdata_recent")
if os.path.isdir(DATA):
    for f in os.listdir(DATA):
        os.remove(os.path.join(DATA, f))
os.makedirs(DATA, exist_ok=True)

env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_DATA_DIR=DATA)
p = subprocess.Popen([EXE], env=env)
time.sleep(3.0)

try:
    label = script_label()
    print("启动稿件行：%s" % label)
    if "a.txt" not in label:
        print("FAIL: 启动时没有恢复 lastScript=a.txt")
        ok = False

    tb = find_top(TOOLBAR)
    if not tb:
        # 按钮条是「鼠标悬停浮层才显示」：先移鼠标到浮层上唤醒再找
        ov = find_top(OVERLAY)
        if ov:
            orc = rect(ov)
            u32.SetCursorPos(int((orc.left + orc.right) / 2),
                             int((orc.top + orc.bottom) / 2))
            time.sleep(0.5)
        tb = find_top(TOOLBAR)
    if not tb:
        print("FAIL: 找不到浮层按钮条（%s）" % TOOLBAR)
        raise SystemExit(1)

    r = rect(tb)
    print("按钮条 rect=(%d,%d,%d,%d) 宽 %.0f" % (r.left, r.top, r.right, r.bottom,
                                              r.right - r.left))

    # --- 场景 1：点「切换稿件」列出两篇 ---
    menu = open_menu(tb)
    if not menu:
        print("FAIL: 点「切换稿件」没有弹出最近文稿列表")
        ok = False
        raise SystemExit(1)

    mr = rect(menu)
    s = dpi_scale(menu)
    want_h = int((8 * 2 + 30 + 3 * 46) * s)     # 2 篇 + 「打开其他文稿…」
    print("列表 rect=(%d,%d,%d,%d) 高 %d（期望约 %d）"
          % (mr.left, mr.top, mr.right, mr.bottom, mr.bottom - mr.top, want_h))
    if abs((mr.bottom - mr.top) - want_h) > 4:
        print("FAIL: 列表高度不对（行数/布局异常）")
        ok = False
    if mr.left < r.right - int(360 * s) or mr.left > r.right:
        print("WARN: 列表没有对齐到按钮条右端（%d vs %d）" % (mr.left, r.right))

    from PIL import ImageGrab
    ImageGrab.grab(bbox=(mr.left - 6, min(mr.top, r.top) - 6, max(mr.right, r.right) + 6,
                         mr.bottom + 6), all_screens=True).save(SHOT)

    # --- 场景 2：点第 2 项（b.txt）切换 ---
    click(*row_point(menu, 1), settle=1.2)
    label = script_label()
    print("点第 2 项后稿件行：%s" % label)
    if "b.txt" not in label:
        print("FAIL: 点列表第 2 项没有切到 b.txt")
        ok = False
    if find_top(MENU):
        print("FAIL: 选中后列表没有收起")
        ok = False

    # --- 场景 3：再开一次，切回上一篇 ---
    # 注意：刚用过的稿子会被提到列表最前面，所以这时第 1 项是 b.txt、第 2 项才是 a.txt
    menu = open_menu(tb)
    if not menu:
        print("FAIL: 第二次没有弹出列表")
        ok = False
    else:
        click(*row_point(menu, 1), settle=1.2)
        label = script_label()
        print("点第 2 项后稿件行：%s" % label)
        if "a.txt" not in label:
            print("FAIL: 点列表第 2 项没有切回 a.txt（最近列表应该把刚用过的排在最前）")
            ok = False

    # --- 场景 4：点列表外面应该收起，稿件不变 ---
    menu = open_menu(tb)
    if not menu:
        print("FAIL: 第三次没有弹出列表")
        ok = False
    else:
        ov = find_top(OVERLAY)
        if ov:
            orc = rect(ov)
            click((orc.left + orc.right) / 2.0, (orc.top + orc.bottom) / 2.0, settle=0.7)
        if find_top(MENU):
            print("FAIL: 点列表外面后列表没有收起")
            ok = False
        label2 = script_label()
        print("点窗外后稿件行：%s" % label2)
        if label2 != label:
            print("FAIL: 点窗外把稿子给换了")
            ok = False

    # --- 场景 5：切换 / 双击都不许改写主 INI 的稿件定义 ---
    if ini_file_section(INI) != ini_after_set:
        print("FAIL: 切换稿件后主 INI 的 [file] 段被改写了（应保持用户定义）")
        ok = False
    else:
        print("主 INI 的 [file] 段未被改写（保持用户定义）")

    store = os.path.join(DATA, "recent.ini")
    if os.path.exists(store):
        print("sidecar 记录：%s" % " | ".join(
            ini_read(store).replace("\r", "").split("\n")[:3]))
    else:
        print("FAIL: 没有生成最近文稿 sidecar")
        ok = False
finally:
    try:
        p.terminate()
    except Exception:
        pass
    time.sleep(0.4)
    kill()
    ini_write(INI, orig)          # 还原用户原来的设置
    print("已还原 INI")

print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
