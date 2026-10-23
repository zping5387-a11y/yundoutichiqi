# -*- coding: utf-8 -*-
"""稿件页「最近文稿」自绘列表验收：路径显示 / 悬停 / 双击载入 / × 删除 / 行内按钮

几何全部从程序的 TP_LIST_LOG 调试日志里拿（HitTest 会记录行高与按钮矩形），
不再靠测试端反推缩放 —— 之前 GetWindowRect 与程序内 GetClientRect 不一致，
DPI/自适应缩放一变，按公式算的点击坐标就会落空。

产出截图：正常态、悬停行、悬停删除按钮、删除后。
"""
import ctypes, os, re, subprocess, sys, time

ctypes.windll.user32.SetProcessDPIAware()   # 物理坐标，与被测应用一致
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
RECENT = os.path.join(ROOT, "build", "recent_list")
DATA = os.path.join(ROOT, "build", "testdata_list")
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")
LIST_LOG = os.path.join(ROOT, "build", "list_log.txt")
DLG = "芸豆提词器 · 教学模式"
IDC_ST_SCRIPT = 1004
IDC_LST_RECENT = 1074

os.environ["TP_LIST_LOG"] = LIST_LOG     # Popen 会继承，程序把点击命中写到这里

ok = True
from PIL import ImageGrab


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


def read_text(path):
    if not os.path.exists(path):
        return ""
    return open(path, "rb").read().decode("gbk", "surrogateescape")


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ---------------------------------- 准备数据 ----------------------------------
kill()
os.makedirs(RECENT, exist_ok=True)
samples = []
for name in ("a.txt", "b.txt", "c.txt"):
    p = os.path.join(RECENT, name)
    with open(p, "w", encoding="utf-8") as f:
        f.write("第 %s 篇讲稿。\n今天讲一个知识点。" % name[0])
    samples.append(p)
os.remove(samples[2])                    # c.txt 做成「缺失」，验证灰行 + 缺失标签

if os.path.isdir(DATA):
    for f in os.listdir(DATA):
        os.remove(os.path.join(DATA, f))
os.makedirs(DATA, exist_ok=True)
with open(os.path.join(DATA, "recent.ini"), "w", encoding="gbk") as f:
    f.write("[file]\r\nlastScript=%s\r\n" % samples[0])
    for i, p in enumerate(samples):
        f.write("recent%d=%s\r\n" % (i, p))

ini_before = read_text(INI)
if os.path.exists(LIST_LOG):
    os.remove(LIST_LOG)

env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_DATA_DIR=DATA)
p = subprocess.Popen([EXE], env=env)
time.sleep(3.0)

try:
    dlg = u32.FindWindowW(None, DLG)
    if not dlg:
        print("FAIL: 找不到主面板")
        raise SystemExit(1)
    # 挪到屏幕下方空白处并置顶，避开桌面其它悬浮窗（浏览器）对鼠标点击的遮挡
    u32.SetWindowPos(dlg, -1, 120, 760, 0, 0, 0x1)   # HWND_TOPMOST, SWP_NOSIZE
    u32.SetForegroundWindow(dlg)
    time.sleep(0.5)
    lst = u32.GetDlgItem(dlg, IDC_LST_RECENT)
    if not lst:
        print("FAIL: 找不到自绘列表控件")
        raise SystemExit(1)

    print("启动稿件行：%s" % text_of(u32.GetDlgItem(dlg, IDC_ST_SCRIPT)))

    # 桌面悬浮助手可能挡住真实鼠标点击，统一改为向列表子窗口直接投递鼠标消息
    WM_MOUSEMOVE, WM_LBDOWN, WM_LBUP, WM_LBDBLCLK = 0x0200, 0x0201, 0x0202, 0x0203
    def post(msg, cx, cy):
        u32.PostMessageW(lst, msg, 1 if msg != WM_LBUP else 0,
                         ((int(cy) & 0xFFFF) << 16) | (int(cx) & 0xFFFF))

    HIT_PAT = re.compile(
        r"hit pt=\((-?\d+),(-?\d+)\) idx=(\d+) zone=(\d+) rh=(\d+) scrollTop=(\d+) "
        r"row=(\d+),(\d+),(\d+),(\d+) del=(\d+),(\d+),(\d+),(\d+) "
        r"folder=(\d+),(\d+),(\d+),(\d+) goto=(\d+),(\d+),(\d+),(\d+)")

    def read_hits():
        if not os.path.exists(LIST_LOG):
            return []
        out = []
        for ln in open(LIST_LOG, encoding="utf-8-sig", errors="ignore").read().splitlines():
            m = HIT_PAT.match(ln.strip())
            if m:
                v = [int(x) for x in m.groups()]
                out.append({"pt": v[0:2], "idx": v[2], "zone": v[3], "rh": v[4],
                            "row": v[6:10], "del": v[10:14],
                            "folder": v[14:18], "goto": v[18:22]})
        return out

    def last_hit():
        hits = read_hits()
        return hits[-1] if hits else None

    def locate(idx):
        """让程序报出第 idx 行的几何：猜一个 y 发 WM_MOUSEMOVE，从日志拿真实矩形。"""
        h = last_hit()
        rh = h["rh"] if h else 200
        h = None
        for _ in range(4):
            y = 1 + rh * (idx + 0.5)
            post(WM_MOUSEMOVE, 20, int(y))
            time.sleep(0.3)
            h = last_hit()
            if h and h["idx"] == idx:
                return h
            if h:
                rh = h["rh"]
        return h

    def center(r):
        return (r[0] + r[2]) / 2.0, (r[1] + r[3]) / 2.0

    def click_rect(r):
        cx, cy = center(r)
        post(WM_MOUSEMOVE, cx, cy)
        time.sleep(0.15)
        post(WM_LBDOWN, cx, cy)
        time.sleep(0.12)
        post(WM_LBUP, cx, cy)
        time.sleep(0.9)

    grab(dlg, "shot_recent_list.png")

    # 悬停第 2 行
    h1 = locate(1)
    if not h1:
        print("FAIL: 探测不到列表行几何（TP_LIST_LOG 没生效？）")
        raise SystemExit(1)
    time.sleep(0.4)
    grab(dlg, "shot_recent_hover.png")

    # 点第 2 行（b.txt）的「去提词」→ 稿件切到 b.txt，浮层显示
    click_rect(h1["goto"])
    label = text_of(u32.GetDlgItem(dlg, IDC_ST_SCRIPT))
    ov = u32.FindWindowW(None, "芸豆提词")
    ov_vis = bool(ov) and bool(u32.IsWindowVisible(ov))
    print("点「去提词」后稿件行：%s；浮层可见=%s" % (label, ov_vis))
    if "b.txt" not in label:
        print("FAIL: 「去提词」没有载入 b.txt")
        ok = False
    if not ov_vis:
        print("FAIL: 「去提词」后浮层不可见")
        ok = False

    # 「去提词」不应改变最近列表顺序：sidecar 的 recent0 仍应是 a.txt
    store = read_text(os.path.join(DATA, "recent.ini"))
    recent_lines = [ln for ln in store.replace("\r", "").split("\n") if ln.lower().startswith("recent")]
    print("sidecar 去提词后：%s" % (" | ".join(recent_lines) if recent_lines else "(空)"))
    if not any(ln.startswith("recent0=") and ln.endswith("a.txt") for ln in recent_lines):
        print("FAIL: 「去提词」改变了最近列表顺序")
        ok = False

    # 悬停第 1 行的 ×（顺序未变，第 1 行仍是 a.txt）
    h0 = locate(0)
    post(WM_MOUSEMOVE, *center(h0["del"]))
    time.sleep(0.5)
    grab(dlg, "shot_recent_del_hover.png")

    # 点第 1 行的 ×（a.txt）→ sidecar 的 recentN 里不再有 a.txt
    click_rect(h0["del"])
    grab(dlg, "shot_recent_after_delete.png")
    store = read_text(os.path.join(DATA, "recent.ini"))
    recent_lines = [ln for ln in store.replace("\r", "").split("\n") if ln.lower().startswith("recent")]
    print("sidecar 删除后：%s" % (" | ".join(recent_lines) if recent_lines else "(空)"))
    if any("a.txt" in ln for ln in recent_lines):
        print("FAIL: 点 × 后 sidecar 的最近列表里仍然有 a.txt")
        ok = False

    # 点第 2 行（c.txt，缺失）的「打开文件夹」→ 退回打开其所在目录，资源管理器窗口
    # 标题 = 目录名 recent_list；只关闭标题为 recent_list 开头的窗口，不动用户其它窗口
    h1 = locate(1)
    post(WM_MOUSEMOVE, *center(h1["folder"]))
    time.sleep(0.5)
    grab(dlg, "shot_recent_folder_hover.png")   # 应看到第 2 行「打开文件夹」按钮高亮
    click_rect(h1["folder"])
    exp = None
    for _ in range(12):                     # 最多等 6 秒
        time.sleep(0.5)
        # 资源管理器窗口标题 = "recent_list - 文件资源管理器"，按前缀匹配
        found = []
        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def cb(h, l):
            if u32.IsWindowVisible(h):
                n = u32.GetWindowTextLengthW(h)
                if n:
                    b = ctypes.create_unicode_buffer(n + 1)
                    u32.GetWindowTextW(h, b, n + 1)
                    if b.value.startswith("recent_list"):
                        found.append(h)
            return True
        u32.EnumWindows(cb, 0)
        if found:
            exp = found[0]
            break
    print("点「打开文件夹」→ 资源管理器窗口 recent_list 打开=%s" % bool(exp))
    if exp:
        u32.PostMessageW(exp, 0x0010, 0, 0)   # WM_CLOSE，只关测试打开的这个窗口
        time.sleep(0.5)
    else:
        print("FAIL: 「打开文件夹」没有打开资源管理器窗口")
        ok = False

    # 双击第 1 行（删除后是 b.txt）→ 与「去提词」一致：载入 + 浮层显示 + 顺序不变
    h0 = locate(0)
    cx, cy = center(h0["row"])
    post(WM_LBDBLCLK, cx, cy)
    time.sleep(0.8)
    label = text_of(u32.GetDlgItem(dlg, IDC_ST_SCRIPT))
    ov = u32.FindWindowW(None, "芸豆提词")
    ov_vis = bool(ov) and bool(u32.IsWindowVisible(ov))
    print("双击第 1 行后稿件行：%s；浮层可见=%s" % (label, ov_vis))
    if "b.txt" not in label:
        print("FAIL: 双击没有载入 b.txt")
        ok = False
    if not ov_vis:
        print("FAIL: 双击载入后浮层不可见")
        ok = False
    store = read_text(os.path.join(DATA, "recent.ini"))
    recent_lines = [ln for ln in store.replace("\r", "").split("\n") if ln.lower().startswith("recent")]
    if not any(ln.startswith("recent0=") and ln.endswith("b.txt") for ln in recent_lines):
        print("FAIL: 双击载入改变了最近列表顺序")
        ok = False

    if read_text(INI) != ini_before:
        print("FAIL: 操作自绘列表把主 INI 改了")
        ok = False
    else:
        print("主 INI 未被改写")
finally:
    try:
        p.terminate()
    except Exception:
        pass
    time.sleep(0.3)
    kill()

print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
