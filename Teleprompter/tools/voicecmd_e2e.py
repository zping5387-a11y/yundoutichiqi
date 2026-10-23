# -*- coding: utf-8 -*-
"""端到端验证「语音命令 / 语音跟读」完全独立：
  场景 1：只开语音命令（跟读关）→ 说命令能翻页，且识别文本不进跟读匹配（日志无 MATCH/MISS）
  场景 2：只开跟读（命令关）→ 说「下一页」不翻页（无 CMD），但文本进入跟读匹配（日志有 MISS）
  场景 3：两个都开 → 命令照常翻页，普通句子仍走跟读
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


def run_case(name, script, cmd_on, follow_on, wait_s, shot=None):
    log = os.path.join(ROOT, "build", "voicecmd_%s.log" % name)
    for f in (log,):
        if os.path.exists(f):
            os.remove(f)

    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    env = dict(os.environ,
               TP_CAPTURE_ALLOWED="1",
               TP_SPEECH_LOG=log,
               TP_VOICE_TEST=script,
               TP_VOICE_TEST_CMD="1" if cmd_on else "0",
               TP_VOICE_TEST_FOLLOW="1" if follow_on else "0")
    p = subprocess.Popen([EXE], env=env)
    time.sleep(3.0)

    dlg = u32.FindWindowW(None, DLG)
    if not dlg:
        print("FAIL: 找不到主窗口")
        p.terminate()
        return None

    btn = find_child(dlg, "示例文稿")
    if btn:
        u32.SendMessageW(btn, 0x00F5, 0, 0)   # BM_CLICK
        time.sleep(0.4)

    # 面板字段：两个按钮的文字就是各自独立的状态
    b_sp = find_child(dlg, "开始跟读") or find_child(dlg, "停止跟读")
    b_cmd = find_child(dlg, "打开语音命令") or find_child(dlg, "关闭语音命令")
    print("  [%s] 跟读按钮=%s  命令按钮=%s" % (
        name,
        text(b_sp) if b_sp else "(无)",
        text(b_cmd) if b_cmd else "(无)"))

    time.sleep(wait_s)

    if shot:
        ov = find_top(OVERLAY)
        if ov:
            l, t, r, b = rect(ov)
            ImageGrab.grab(bbox=(l - 6, t - 6, r + 6, b + 6), all_screens=True).save(
                os.path.join(ROOT, "build", shot))

    time.sleep(0.6)
    p.terminate()
    time.sleep(0.3)

    cmds, hits, misses = [], 0, 0
    if os.path.exists(log):
        with open(log, encoding="utf-8") as f:
            for ln in f:
                if ln.startswith("CMD"):
                    cmds.append(ln.rstrip("\r\n"))
                elif "\tMATCH" in ln:
                    hits += 1
                elif "\tMISS " in ln:
                    misses += 1

    print("  [%s] 命令=%d 条 %s | 跟读 MATCH=%d MISS=%d" % (name, len(cmds), cmds, hits, misses))
    return {"cmds": cmds, "match": hits, "miss": misses}


ok = True

# 「下一页」的步长断言和浮层高度强相关：用户把浮层调大后，
# 示例文稿不够两页，第二次翻页会被夹在页尾。测试统一用固定小窗，测完还原。
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
            ln = "rect=1955,172,2557,584"     # 小窗：保证示例文稿有足够页数
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


print("场景 1：只开语音命令（跟读关），说 下一页 → 下一页 → 上一页")
r1 = run_case("cmd_only", "4000:下一页;6500:下一页;9000:上一页", True, False, 7.2, "shot_indep_cmdonly.png")
if not r1:
    raise SystemExit(1)
ys = [float(x.split("\ty=")[1].split("\t")[0]) for x in r1["cmds"]] if r1["cmds"] else []
print("  滚动位置:", ys)
if len(ys) != 3:
    print("  FAIL: 期望 3 条命令（每句只翻一次）")
    ok = False
else:
    if not (ys[0] > 50 and abs(ys[1] - 2 * ys[0]) < 3 and abs(ys[2] - ys[0]) < 3):
        print("  FAIL: 翻页步长不对")
        ok = False
if r1["match"] or r1["miss"]:
    print("  FAIL: 跟读关闭时不应把识别文本送进匹配器")
    ok = False

print("\n场景 2：只开跟读（命令关），说 下一页 → 上一页（不应翻页）")
r2 = run_case("follow_only", "4000:下一页;6500:上一页", False, True, 6.0)
if not r2:
    raise SystemExit(1)
if r2["cmds"]:
    print("  FAIL: 命令关闭时仍然执行了翻页")
    ok = False
if r2["miss"] + r2["match"] == 0:
    print("  FAIL: 跟读开启时识别文本应进入匹配器")
    ok = False

print("\n场景 3：两个都开，说 下一页 → 然后一段正文")
r3 = run_case("both", "4000:下一页;6500:同学们上课上一节课我们讨论了物体在液体中的沉浮条件",
              True, True, 7.0, "shot_indep_both.png")
if not r3:
    raise SystemExit(1)
if len(r3["cmds"]) != 1:
    print("  FAIL: 期望 1 条翻页命令")
    ok = False
if r3["match"] + r3["miss"] == 0:
    print("  FAIL: 正文应继续走跟读匹配")
    ok = False

restore_rect()
print("\nRESULT:", "PASS" if ok else "FAIL")
raise SystemExit(0 if ok else 1)
