# -*- coding: utf-8 -*-
"""端到端验证「AI 语音跟随（智能提词）」：

  按真实流式识别的节奏喂入（每 250ms 一个带上下文的中间结果）：
    ① 连续念 ~7 秒 → 画面平滑向下滚（不是一句一跳）
    ② 停 4 秒不说话 → 速度归零、画面完全停住
    ③ 再开口接着念 → 从原位置平滑续上

判定数据来自 TP_SPEECH_LOG 里的 FOLLOW 采样行：
  FOLLOW  t=<tick>  y=<滚动位置>  v=<速度 px/s>  paused=0/1
  EVT     t=<tick>  <本次喂入的文本>
"""
import ctypes, os, re, subprocess, time
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

LOG = os.path.join(ROOT, "build", "follow_e2e.log")

# 与「示例文稿」一致（标点会被匹配器归一化掉）
SCRIPT = (
    "同学们，上课！"
    "上一节课我们讨论了物体在液体中的沉浮条件，今天这节课，我们来学习浮力的大小究竟与什么有关。"
    "先看一个生活中的现象：把一个空矿泉水瓶按进水里，手会感觉到一个向上的力，而且按得越深，这个力越大。"
    "这说明，浸在液体中的物体受到向上的浮力，浮力的大小与物体排开液体的体积有关。"
    "那么，浮力到底等于多少呢？我们用弹簧测力计做一个小实验。"
)

FEED_START = 4000       # 开始念
PACE_MS = 250           # 每 250ms 一个中间结果（≈ 真实流式识别）
CHARS_PER_TICK = 2.5    # 每个 tick 前进的字数（≈ 10 字/秒，语速偏快）
PAUSE_AT = 11000        # 停读
RESUME_AT = 15000       # 续读
END = 20000
WINDOW = 28             # 中间结果带上下文长度


def build_spec():
    items = []
    pos = 0.0
    t = FEED_START
    while t < PAUSE_AT:
        items.append((t, SCRIPT[int(pos):int(pos) + WINDOW]))
        pos += CHARS_PER_TICK
        t += PACE_MS
    pos = int(pos)
    t = RESUME_AT
    while t < END - 1000 and pos < len(SCRIPT) - 4:
        items.append((t, SCRIPT[int(pos):int(pos) + WINDOW]))
        pos += CHARS_PER_TICK
        t += PACE_MS
    return items


def text(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


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


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def parse_log(path):
    """返回 evts(喂入时刻) 与 rows(t, y, v, paused, 最近喂入序号)"""
    evts, rows = [], []
    if not os.path.exists(path):
        return evts, rows
    with open(path, encoding="utf-8") as f:
        for ln in f:
            if ln.startswith("EVT\t"):
                part = ln.strip().split("\t")[1]
                evts.append(int(part.split("=", 1)[1]))
            elif ln.startswith("FOLLOW\t"):
                parts = dict(p.split("=", 1) for p in ln.strip().split("\t")[1:])
                rows.append((int(parts["t"]), float(parts["y"]),
                             float(parts["v"]), int(parts["paused"]), len(evts) - 1))
    return evts, rows


ok = True
if os.path.exists(LOG):
    os.remove(LOG)
subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# 测试依赖「读到 40% 窗口高就开始滚」：浮层太高（用户调大过）会让朗读段
# 一直停在目标 0 上。这里临时把浮层设成固定小窗，测完还原。
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")
OLD_RECT = None
if os.path.exists(INI):
    with open(INI, encoding="latin-1") as f:
        src = f.read()
    m = re.search(r"(?m)^rect=(.*)$", src)
    OLD_RECT = m.group(1).strip() if m else None
    out = []
    for ln in src.splitlines():
        if ln.startswith("rect="):
            ln = "rect=1955,172,2557,584"     # 小窗：让朗读段尽快进入滚动区
        out.append(ln)
    with open(INI, "w", encoding="latin-1") as f:
        f.write("\n".join(out) + "\n")


def restore_rect():
    if OLD_RECT is None:
        return
    with open(INI, encoding="latin-1") as f:
        src = f.read()
    out = []
    for ln in src.splitlines():
        if ln.startswith("rect="):
            ln = "rect=%s" % OLD_RECT
        out.append(ln)
    with open(INI, "w", encoding="latin-1") as f:
        f.write("\n".join(out) + "\n")


spec_items = build_spec()
spec_file = os.path.join(ROOT, "build", "follow_spec.txt")
with open(spec_file, "w", encoding="utf-8") as f:
    f.write("\n".join("%d:%s" % (t, s) for t, s in spec_items))
env = dict(os.environ,
           TP_CAPTURE_ALLOWED="1",
           TP_SPEECH_LOG=LOG,
           TP_VOICE_TEST_FILE=spec_file,
           TP_VOICE_TEST_CMD="0",     # 只看跟读
           TP_VOICE_TEST_FOLLOW="1")
p = subprocess.Popen([EXE], env=env)
time.sleep(3.0)

dlg = u32.FindWindowW(None, DLG)
if not dlg:
    print("FAIL: 找不到主窗口")
    p.terminate()
    raise SystemExit(1)

btn = find_child(dlg, "示例文稿")
if btn:
    u32.SendMessageW(btn, 0x00F5, 0, 0)   # BM_CLICK
    time.sleep(0.4)

btn_sp = find_child(dlg, "停止跟读") or find_child(dlg, "开始跟读")
print("跟读按钮:", text(btn_sp) if btn_sp else "(无)")

time.sleep(END / 1000.0 + 3.0)

ov = find_top(OVERLAY)
if ov:
    l, t, r, b = rect(ov)
    ImageGrab.grab(bbox=(l - 6, t - 6, r + 6, b + 6), all_screens=True).save(
        os.path.join(ROOT, "build", "shot_follow_end.png"))
if dlg:
    l, t, r, b = rect(dlg)
    ImageGrab.grab(bbox=(l, t, r, b), all_screens=True).save(
        os.path.join(ROOT, "build", "shot_follow_panel.png"))

time.sleep(0.4)
p.terminate()
time.sleep(0.3)

evts, rows = parse_log(LOG)
if not rows or len(evts) < 20:
    print("FAIL: 日志不完整（EVT=%d, FOLLOW=%d）" % (len(evts), len(rows)))
    raise SystemExit(1)

# 用 EVT 之间的大间隔（>3s，即静音段）把喂入切成两段
gap_at = max(range(1, len(evts)), key=lambda i: evts[i] - evts[i - 1])
if evts[gap_at] - evts[gap_at - 1] < 3000:
    print("FAIL: 没找到静音段")
    raise SystemExit(1)

A_END, B_START = gap_at - 1, gap_at          # EVT 序号分界

read_seg = [r for r in rows if 0 <= r[4] <= A_END and r[0] - evts[r[4]] < 400]
pause_seg = [r for r in rows if r[4] == A_END and r[0] - evts[A_END] > 1500]
resume_seg = [r for r in rows if r[4] >= B_START and r[0] - evts[r[4]] < 400]


def delta(seg):
    return seg[-1][1] - seg[0][1] if len(seg) >= 2 else 0.0


d_read, d_pause, d_resume = delta(read_seg), delta(pause_seg), delta(resume_seg)
print("采样 %d 个（朗读 %d / 静音 %d / 续读 %d）；静音时长 %.1fs" %
      (len(rows), len(read_seg), len(pause_seg), len(resume_seg),
       (evts[B_START] - evts[A_END]) / 1000.0))
print("位移：朗读段 %+.1fpx  静音段 %+.1fpx  续读段 %+.1fpx" % (d_read, d_pause, d_resume))

if d_read < 200:
    print("FAIL: 朗读时画面没有持续向下滚")
    ok = False

# 滚动必须连续：几乎每个采样都在动，而不是「滑一下停一下」。
# 稿件开头那几秒锚点还没到 40% 视口位置，画面本来就该停在顶部，只统计动起来之后的部分。
started = next((i for i, r in enumerate(read_seg) if r[1] > 5.0), 0)
moving_seg = read_seg[started:]
moving_read = sum(1 for a, b in zip(moving_seg, moving_seg[1:]) if b[1] - a[1] > 0.3)
ratio = moving_read / max(1, len(moving_seg) - 1)
print("朗读段持续移动占比 %.0f%%（%d/%d）" % (ratio * 100, moving_read, max(0, len(moving_seg) - 1)))
if ratio < 0.85:
    print("FAIL: 滚动不够连续（像一句一跳）")
    ok = False

speeds = sorted(r[2] for r in moving_seg if r[2] > 0)
if speeds:
    med = speeds[len(speeds) // 2]
    print("朗读段速度中位数 %.0f px/s" % med)
    if not (20 <= med <= 400):
        print("FAIL: 跟随速度超出合理区间（应随语速，约 60~200 px/s）")
        ok = False

# 停读：位置彻底停住、速度归零、paused 置位
moving = [r for r in pause_seg if abs(r[2]) > 1.0]
paused_flags = sum(1 for r in pause_seg if r[3] == 1)
if not pause_seg:
    print("FAIL: 静音段没有采样")
    ok = False
else:
    if d_pause > 1.0 or moving:
        print("FAIL: 静音后画面仍在滚（位移 %.1fpx，%d 个采样仍有速度）" % (d_pause, len(moving)))
        ok = False
    if paused_flags < len(pause_seg) * 0.9:
        print("FAIL: 静音段 paused 标记没有置位（%d/%d）" % (paused_flags, len(pause_seg)))
        ok = False

if d_resume < 100:
    print("FAIL: 重新开口后没有续上滚动")
    ok = False

print("RESULT:", "PASS" if ok else "FAIL")
restore_rect()
raise SystemExit(0 if ok else 1)
