# -*- coding: utf-8 -*-
"""端到端：稿件页「最近文稿」列表双击载入（=「去提词」：开浮层、不重排），且不改写主 INI

步骤：
  1. 预先把 INI 的 [file] 段写成 lastScript=a.txt、recent0=a.txt、recent1=b.txt
     （这是「用户手写的稿件定义」，程序只读）
  2. 启动 → 稿件页默认显示，列表应为 a、b 两项
  3. 真实鼠标双击列表第 2 行（b.txt）→ 面板「稿件」行变成 b.txt，浮层显示
  4. 断言主 INI 的 [file] 段一个字节都没变
  5. 断言 sidecar（TP_DATA_DIR/recent.ini）顺序不变（recent0 仍是 a.txt）
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
DATA = os.path.join(ROOT, "build", "testdata_dblclick")
DLG = "芸豆提词器 · 教学模式"
IDC_ST_SCRIPT = 1004
IDC_LST_RECENT = 1074
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004

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


def click(x, y, settle=0.4):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.05)
    u32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.03)
    u32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    time.sleep(settle)


def dblclick(x, y, settle=0.8):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.05)
    for _ in range(2):
        u32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        time.sleep(0.03)
        u32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        time.sleep(0.08)
    time.sleep(settle)


def ini_read(path):
    if not os.path.exists(path):
        return ""
    return open(path, "rb").read().decode("gbk", "surrogateescape")


def ini_write(path, s):
    open(path, "wb").write(s.encode("gbk", "surrogateescape"))


def ini_file_section(path):
    m = re.search(r"(?ims)^\s*\[file\]\s*$(.*?)(?=^\s*\[|\Z)", ini_read(path))
    return m.group(1).strip() if m else ""


def ini_set_file_section(path, last, recents):
    lines = [l for l in ini_read(path).splitlines()
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
    ini_write(path, "\r\n".join(out) + "\r\n")


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


A = ["第一篇文章：今天讲二次函数。", "对称轴是 x 等于负 b 除以 2a。"]
B = ["第二篇文章：今天我们聊聊概率。", "古典概型要求样本空间有限、每个样本点等可能。"]


def make_samples():
    os.makedirs(RECENT, exist_ok=True)
    pa = os.path.join(RECENT, "a.txt")
    pb = os.path.join(RECENT, "b.txt")
    for p, lines in ((pa, A), (pb, B)):
        with open(p, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
    return pa, pb


kill()
pa, pb = make_samples()
orig = ini_read(INI)
ini_set_file_section(INI, pa, [pa, pb])
want_file = ini_file_section(INI)

if os.path.isdir(DATA):
    for f in os.listdir(DATA):
        os.remove(os.path.join(DATA, f))
os.makedirs(DATA, exist_ok=True)

env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_DATA_DIR=DATA)
p = subprocess.Popen([EXE], env=env)
time.sleep(3.0)

try:
    dlg = u32.FindWindowW(None, DLG)
    if not dlg:
        print("FAIL: 找不到主面板")
        raise SystemExit(1)

    st = u32.GetDlgItem(dlg, IDC_ST_SCRIPT)
    lst = u32.GetDlgItem(dlg, IDC_LST_RECENT)
    if not lst:
        print("FAIL: 找不到「最近文稿」列表")
        raise SystemExit(1)
    print("启动稿件行：%s" % text_of(st))

    # 自绘列表：行高 100（逻辑），第 2 行中心 = 列表内 y = 1 + 48*1.5
    dpi = u32.GetDpiForWindow(dlg) or 96
    sc = dpi / 96.0
    l, t, r, b = rect(lst)
    pt = wintypes.POINT(int((l + r) / 2), int(t + (1 + 100 * 1.5) * sc))
    print("双击第 2 行：屏幕坐标 (%d, %d)" % (pt.x, pt.y))
    dblclick(pt.x, pt.y)

    label = text_of(st)
    print("双击后稿件行：%s" % label)
    if "b.txt" not in label:
        print("FAIL: 双击第 2 项没有切到 b.txt")
        ok = False

    # 双击 = 「去提词」：浮层应显示
    ov = u32.FindWindowW(None, "芸豆提词")
    ov_vis = bool(ov) and bool(u32.IsWindowVisible(ov))
    print("双击后浮层可见=%s" % ov_vis)
    if not ov_vis:
        print("FAIL: 双击载入后浮层不可见")
        ok = False

    now = ini_file_section(INI)
    if now != want_file:
        print("FAIL: 双击载入后主 INI 的 [file] 段被改写")
        print("  之前：%r" % want_file)
        print("  之后：%r" % now)
        ok = False
    else:
        print("主 INI 的 [file] 段未被改写（保持用户定义）")

    # 双击不重排：sidecar 若存在，recent0 必须仍是 a.txt
    store = os.path.join(DATA, "recent.ini")
    if os.path.exists(store):
        body = ini_read(store).replace("\r", "")
        recents = [ln for ln in body.split("\n") if ln.lower().startswith("recent")]
        print("sidecar 顺序：%s" % (" | ".join(recents) if recents else "(空)"))
        if any(ln.startswith("recent0=") and "b.txt" in ln for ln in recents):
            print("FAIL: 双击载入把 b.txt 重排到了首位")
            ok = False
    else:
        print("sidecar 不存在（未重排任何记录）")
finally:
    try:
        p.terminate()
    except Exception:
        pass
    time.sleep(0.3)
    kill()
    ini_write(INI, orig)
    print("已还原 INI")

print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
