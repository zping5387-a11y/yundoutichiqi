# -*- coding: utf-8 -*-
"""端到端验证浮层顶部按钮条：返回页首 / 到页尾 / 滚动 / 停止滚动

  点击「滚动」   → 画面开始匀速向下滚
  点击「停止滚动」→ 画面停住
  点击「到页尾」  → 一步滚到最底
  点击「返回页首」→ 回到顶部
  滚到页尾       → 自动停止滚动（等同点了「停止滚动」，auto 标志回落）

滚动位置来自 TP_SPEECH_LOG 的 FOLLOW 采样（t/y/v/paused/auto）。
"""
import ctypes, os, subprocess, time
from ctypes import wintypes
from PIL import ImageGrab

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"
OVERLAY = "芸豆提词"
TOOLBAR = "提词按钮条"
LOG = os.path.join(ROOT, "build", "toolbar_e2e.log")

WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_HOTKEY = 0x0312
MK_LBUTTON = 0x0001
HK_SCROLL_UP = 5          # framework.h 里的热键 ID
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")


def set_ini_speed(level):
    """临时改「滚动速度」，让自动滚动在测试里跑得快些（返回原值）

    INI 由 GetPrivateProfileStringW 写成 ANSI(GBK)，按 latin-1 读写可原样保留字节
    """
    if not os.path.exists(INI):
        return None
    with open(INI, encoding="latin-1") as f:
        src = f.read()
    m = [x for x in src.splitlines() if x.strip().startswith("autoSpeed=")]
    old = m[0].split("=", 1)[1].strip() if m else None
    out = []
    for ln in src.splitlines():
        if ln.strip().startswith("autoSpeed="):
            ln = "autoSpeed=%d" % level
        out.append(ln)
    if not m:
        out.insert(1, "autoSpeed=%d" % level)
    with open(INI, "w", encoding="latin-1") as f:
        f.write("\n".join(out) + "\n")
    return old


def restore_ini_speed(old):
    if old is None:
        return
    with open(INI, encoding="latin-1") as f:
        src = f.read()
    out = []
    for ln in src.splitlines():
        if ln.strip().startswith("autoSpeed="):
            ln = "autoSpeed=%s" % old
        out.append(ln)
    with open(INI, "w", encoding="latin-1") as f:
        f.write("\n".join(out) + "\n")


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
        if text(h) == label:
            hit.append(h)
            return False
        return True

    u32.EnumChildWindows(parent, cb, 0)
    return hit[0] if hit else None


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r


def hover_overlay():
    """按钮条是「鼠标悬停浮层才显示」：把鼠标移到浮层中央唤醒它"""
    ov = find_top(OVERLAY)
    if ov:
        r = rect(ov)
        u32.SetCursorPos((r.left + r.right) // 2, (r.top + r.bottom) // 2)
        time.sleep(0.45)


def click_button(tb, idx):
    hover_overlay()                       # 先唤醒按钮条，否则它隐藏着收不到点击
    r = rect(tb)
    w, h = r.right - r.left, r.bottom - r.top
    lx = int((idx + 0.5) * w / 5)      # 五个按钮均分按钮条宽度（末位是「切换稿件」）
    ly = int(h / 2)
    lp = ctypes.c_long((ly << 16) | (lx & 0xFFFF))
    u32.PostMessageW(tb, WM_LBUTTONDOWN, MK_LBUTTON, lp)
    u32.PostMessageW(tb, WM_LBUTTONUP, 0, lp)
    time.sleep(0.3)


def parse_follow(path):
    """返回 [(t, y, v, paused, auto)]，t 为应用侧 tick（不做归一化）"""
    rows = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for ln in f:
                if ln.startswith("FOLLOW\t"):
                    p = dict(x.split("=", 1) for x in ln.strip().split("\t")[1:])
                    rows.append((int(p["t"]), float(p["y"]), float(p["v"]),
                                 int(p["paused"]), int(p.get("auto", 0))))
    return rows


def post_hotkey(hwnd, hid):
    """投递一个全局热键（HK_SCROLL_UP=5 → 向上 70px），模拟用户手动上滚"""
    u32.PostMessageW(hwnd, WM_HOTKEY, hid, 0)
    time.sleep(0.06)


def seg(rows, a, b):
    return [r for r in rows if a <= r[0] <= b]


def delta(s):
    return s[-1][1] - s[0][1] if len(s) >= 2 else 0.0


ok = True
if os.path.exists(LOG):
    os.remove(LOG)
subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# 用一个永远不会到点的测试脚本把「跟读」打开（这样 FOLLOW 采样才工作）；
# 命令关掉，跟读没有输入 → 不会移动画面，正好隔离出按钮的效果
env = dict(os.environ,
           TP_CAPTURE_ALLOWED="1",
           TP_SPEECH_LOG=LOG,
           TP_VOICE_TEST="99999999:（测试占位，不触发）",
           TP_VOICE_TEST_CMD="0",
           TP_VOICE_TEST_FOLLOW="1")
old_speed = set_ini_speed(10)      # max 档，让「自己滚到底」在几秒内发生
p = subprocess.Popen([EXE], env=env)
time.sleep(3.0)

dlg = u32.FindWindowW(None, DLG)
if not dlg:
    print("FAIL: 找不到主窗口")
    p.terminate()
    raise SystemExit(1)

# 按钮条在有稿件内容后才显示，先加载示例文稿再找它
btn = find_child(dlg, "示例文稿")
if btn:
    u32.SendMessageW(btn, 0x00F5, 0, 0)   # BM_CLICK
    time.sleep(0.6)

# 按钮条现在是「鼠标悬停浮层才显示」：先把鼠标移到浮层上唤醒
hover_overlay()
tb = find_top(TOOLBAR)
if not tb:
    print("FAIL: 找不到按钮条窗口")
    p.terminate()
    raise SystemExit(1)

r = rect(tb)
print("按钮条: %dx%d @ (%d,%d)" % (r.right - r.left, r.bottom - r.top, r.left, r.top))


def take(n_prev, wait):
    """等 wait 秒后返回 (新出现的采样段, 当前采样总数)"""
    time.sleep(wait)
    rows = parse_follow(LOG)
    return rows[n_prev:], len(rows)


n = len(parse_follow(LOG))

click_button(tb, 2)                       # 滚动
n = len(parse_follow(LOG))   # 丢弃点击前悬停等待期间的样本
s_roll, n = take(n, 2.0)
click_button(tb, 3)                       # 停止滚动
n = len(parse_follow(LOG))   # 丢弃点击前悬停等待期间的样本
s_stop, n = take(n, 1.5)
click_button(tb, 1)                       # 到页尾
n = len(parse_follow(LOG))   # 丢弃点击前悬停等待期间的样本
s_bottom, n = take(n, 1.2)
click_button(tb, 0)                       # 返回页首
n = len(parse_follow(LOG))   # 丢弃点击前悬停等待期间的样本
s_top, n = take(n, 1.2)

ov = find_top(OVERLAY)
if not ov:
    print("FAIL: 找不到浮层窗口")
    p.terminate()
    restore_ini_speed(old_speed)
    raise SystemExit(1)

# ---- 场景 D：开着自动滚动一路滚到页尾 → 自动停止（等同点了「停止滚动」）----
click_button(tb, 1)                       # 先到页尾，拿到最大滚动位置
n = len(parse_follow(LOG))   # 丢弃点击前悬停等待期间的样本
s_b2, n = take(n, 1.0)
y_max = max((r[1] for r in s_b2), default=0.0)

for _ in range(12):                       # 用「上滚」热键退 ~840px，留出自己滚下去的距离
    post_hotkey(dlg, HK_SCROLL_UP)
s_up, n = take(n, 0.8)
y_up = s_up[-1][1] if s_up else 0.0

click_button(tb, 2)                       # 滚动
n = len(parse_follow(LOG))   # 丢弃点击前悬停等待期间的样本
y_autooff, autooff_ok = None, False
for _ in range(24):                       # 最多等 12 秒
    s, n = take(n, 0.5)
    if s and s[-1][4] == 0:               # auto 标志回落 = 自己停了
        y_autooff, autooff_ok = s[-1][1], True
        break
s_rest, n = take(n, 1.2)
d_rest = delta(s_rest)

# ---- 场景 E：已经停在页尾时点「滚动」→ 下一拍就自动停，不空转 ----
click_button(tb, 2)
n = len(parse_follow(LOG))   # 丢弃点击前悬停等待期间的样本
s_spin, n = take(n, 1.0)
auto_at_bottom = s_spin[-1][4] if s_spin else 1

if ov:
    ro = rect(ov)
    hover_overlay()                       # 截图前把按钮条亮出来
    ImageGrab.grab(bbox=(ro.left - 8, ro.top - 70, ro.right + 8, ro.top + 220),
                   all_screens=True).save(os.path.join(ROOT, "build", "shot_toolbar.png"))

time.sleep(0.4)
p.terminate()
time.sleep(0.3)
restore_ini_speed(old_speed)

d_roll, d_stop, d_bottom, d_top = (delta(s) for s in (s_roll, s_stop, s_bottom, s_top))
print("采样段：滚动 %d / 停止 %d / 页尾 %d / 页首 %d"
      % (len(s_roll), len(s_stop), len(s_bottom), len(s_top)))
print("位移：滚动段 %+.1f  停止段 %+.1f  页尾段 %+.1f  回页首段 %+.1f"
      % (d_roll, d_stop, d_bottom, d_top))
print("各段末位置：滚动 %.0f → 停止 %.0f → 页尾 %.0f → 页首 %.0f"
      % (s_roll[-1][1] if s_roll else -1, s_stop[-1][1] if s_stop else -1,
         s_bottom[-1][1] if s_bottom else -1, s_top[-1][1] if s_top else -1))
print("自动停：页尾 y=%.0f，退回到 %.0f，自己滚到 %.0f 后 auto=%s，随后 1.2s 位移 %+.1f"
      % (y_max, y_up, y_autooff if y_autooff is not None else -1,
         "0" if autooff_ok else "仍为 1", d_rest))
print("页尾再点「滚动」后 auto=%d（应为 0）" % auto_at_bottom)

if d_roll < 60:
    print("FAIL: 点「滚动」后画面没有滚动")
    ok = False
if d_stop > 2.0:
    print("FAIL: 点「停止滚动」后画面仍在滚")
    ok = False
y_stop = s_stop[-1][1] if s_stop else 0
if not s_bottom or s_bottom[-1][1] <= y_stop + 100:
    print("FAIL: 点「到页尾」没有滚到最底")
    ok = False
if not s_top or s_top[-1][1] > 5.0:
    print("FAIL: 点「返回页首」没有回到顶部")
    ok = False
if y_max <= 0 or y_up >= y_max - 200:
    print("FAIL: 上滚退页尾失败，场景 D 无法验证（y_max=%.0f y_up=%.0f）" % (y_max, y_up))
    ok = False
elif not autooff_ok:
    print("FAIL: 滚到页尾后自动滚动没有停（auto 一直为 1）")
    ok = False
elif abs(y_autooff - y_max) > 3.0:
    print("FAIL: 自动停止的位置不在页尾（%.0f vs %.0f）" % (y_autooff, y_max))
    ok = False
elif abs(d_rest) > 1.5:
    print("FAIL: 自动停止后画面又动了 %+.1f px" % d_rest)
    ok = False

if auto_at_bottom != 0:
    print("FAIL: 已在页尾时点「滚动」，自动滚动没有自动停")
    ok = False

print("RESULT:", "PASS" if ok else "FAIL")
raise SystemExit(0 if ok else 1)
