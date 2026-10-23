# -*- coding: utf-8 -*-
"""端到端：提词器直接打开 .docx / .pptx 当稿件

用 TP_SCRIPT=<路径> 让程序启动即载入，然后读面板「稿件」行的文字
（形如 "demo.docx · 137 字"），和 offdoc_test.exe 抽出的字数比对，
再确认浮层窗口已经显示内容。

同时检查旧版格式 .doc 会被拒（此时会弹消息框，测试先不点它，
只验证 .docx/.pptx 路径 —— 消息框留给人工确认）。
"""
import ctypes, os, re, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
OFFDOC = os.path.join(ROOT, "build", "offdoc_test.exe")
SAMPLES = os.path.join(ROOT, "build", "samples")
DLG = "芸豆提词器 · 教学模式"
OVERLAY = "芸豆提词"
PY = sys.executable

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


def child_text(dlg, cid):
    """按控件 ID 取文字（GetDlgItem）"""
    h = u32.GetDlgItem(dlg, cid)
    return text_of(h) if h else ""


def expected_chars(path):
    p = subprocess.run([OFFDOC, path], capture_output=True)
    out = p.stdout.decode("utf-8", "replace")
    body = "\n".join(out.split("\n")[1:])
    # offdoc_test 走控制台文本模式，换行是 \r\n；程序里只有 \n
    return len(body.replace("\r\n", "\n").rstrip("\n"))


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)


def check(name, kind):
    global ok
    path = os.path.join(SAMPLES, name)
    want = expected_chars(path)
    print("---- %s（抽取 %d 字）" % (name, want))

    kill()
    env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_SCRIPT=path)
    p = subprocess.Popen([EXE], env=env)
    time.sleep(3.0)

    dlg = u32.FindWindowW(None, DLG)
    if not dlg:
        print("FAIL: 找不到主窗口")
        p.terminate()
        ok = False
        return

    label = child_text(dlg, 1004)      # IDC_ST_SCRIPT
    print("   稿件行：%s" % label)
    m = re.search(r"·\s*(\d+)\s*字", label)
    got = int(m.group(1)) if m else -1
    if name not in label:
        print("FAIL: 稿件行里没有文件名")
        ok = False
    if got != want:
        print("FAIL: 字数不符（界面 %d，抽取 %d）" % (got, want))
        ok = False

    ov = find_top(OVERLAY)
    if not ov:
        print("FAIL: 浮层没有显示（%s 可能没载入成功）" % kind)
        ok = False

    p.terminate()
    time.sleep(0.3)


kill()
subprocess.run([PY, os.path.join(ROOT, "tools", "gen_office_samples.py")], check=True,
               stdout=subprocess.DEVNULL)

check("demo.docx", "Word")
check("demo.pptx", "PPT")

kill()
print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
