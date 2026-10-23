# -*- coding: utf-8 -*-
"""离线识别（sherpa-onnx）端到端自检。

流程：写稿件 -> 配置 INI -> 启动 -> 点「开始跟读」 -> 等模型加载 ->
      用 WAV 回灌音频 -> 统计匹配位置推进 -> 截图浮层。
"""
import ctypes
import os
import re
import subprocess
import time
from ctypes import wintypes

from PIL import ImageGrab

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
WAV = os.path.join(ROOT, "build", "asrprobe.wav")
LOG = os.path.join(ROOT, "build", "speech_sherpa.log")
SCRIPT = os.path.join(ROOT, "build", "e2e.txt")
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")

IDC_BTN_SPEECH = 1010
IDC_ST_SPEECH = 1012

TEXT = (
    "同学们，上课！上一节课我们讨论了物体在液体中的沉浮条件，今天这节课，"
    "我们来学习浮力的大小究竟与什么有关。"
    "先看一个生活中的现象：把一个空矿泉水瓶按进水里，手会感觉到一个向上的力。"
    "于是我们得到阿基米德原理：浸在液体中的物体所受浮力，等于它排开的液体所受的重力。"
)

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass


def wtext(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def child_by_id(parent, cid):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _l):
        if u32.GetDlgCtrlID(h) == cid:
            hit.append(h)
            return False
        return True

    u32.EnumChildWindows(parent, cb, 0)
    return hit[0] if hit else None


def main():
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   capture_output=True)

    # 1) 稿件（UTF-8 BOM，程序两种编码都认）
    with open(SCRIPT, "w", encoding="utf-8-sig", newline="\r\n") as f:
        f.write(TEXT)

    # 2) INI：让它启动时自动加载这份稿件
    if os.path.exists(INI):
        s = open(INI, encoding="gbk", errors="ignore").read()
        s = re.sub(r"lastScript=.*", "lastScript=" + SCRIPT.replace("\\", "/"), s)
        open(INI, "w", encoding="gbk").write(s)

    if os.path.exists(LOG):
        os.remove(LOG)

    env = dict(os.environ,
               TP_CAPTURE_ALLOWED="1",
               TP_SPEECH_WAV=WAV,
               TP_SPEECH_LOG=LOG)

    p = subprocess.Popen([EXE], env=env)
    time.sleep(3)

    dlg = u32.FindWindowW(None, "芸豆提词器 · 教学模式")
    if not dlg:
        print("FAIL: 主窗口未出现（DLL 或依赖有问题）")
        p.terminate()
        return 1

    btn = child_by_id(dlg, IDC_BTN_SPEECH)
    st = child_by_id(dlg, IDC_ST_SPEECH)
    print("开始跟读按钮:", bool(btn), "| 状态框:", bool(st))

    u32.SendMessageW(btn, 0x00F5, 0, 0)   # BM_CLICK

    # 3) 等模型加载完成（后台线程，最多 40 秒）
    ready_at = None
    t0 = time.time()
    while time.time() - t0 < 40:
        time.sleep(0.5)
        txt = wtext(st)
        if "跟读中" in txt:
            ready_at = time.time() - t0
            break
        if "失败" in txt:
            print("FAIL: 引擎状态 =>", txt)
            p.terminate()
            return 1

    print("模型就绪耗时: %.1fs" % (ready_at if ready_at else -1))
    print("状态:", wtext(st))

    # 4) 音频约 31 秒，留足解码与匹配时间
    time.sleep(42)

    # 5) 浮层截图
    ov = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb_ov(h, _l):
        if u32.IsWindowVisible(h) and wtext(h) == "芸豆提词":
            ov.append(h)
            return False
        return True

    u32.EnumWindows(cb_ov, 0)
    if ov:
        r = wintypes.RECT()
        u32.GetWindowRect(ov[0], ctypes.byref(r))
        img = ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True)
        out = os.path.join(ROOT, "build", "shot_sherpa.png")
        img.save(out)
        print("浮层截图:", out, img.size)
    else:
        print("WARN: 没找到浮层")

    print("状态:", wtext(st))

    # 先退出进程，否则日志文件还被占用
    p.terminate()
    time.sleep(0.8)
    print("进程已退出:", p.poll() is not None)

    # 6) 统计识别与匹配
    pos = []
    recos = []
    if os.path.exists(LOG):
        for line in open(LOG, encoding="utf-8", errors="ignore"):
            if "\tMATCH\t" in line:
                m = re.search(r"pos=(\d+)/(\d+)", line)
                if m:
                    pos.append((int(m.group(1)), int(m.group(2))))
            else:
                recos.append(line.strip())

    print("识别文本样本数:", len(recos))
    if recos:
        print("最后一条识别:", recos[-1][:80])
    if pos:
        vals = [p[0] for p in pos]
        backs = [vals[i - 1] - vals[i] for i in range(1, len(vals)) if vals[i] < vals[i - 1]]
        print("匹配次数:", len(pos), "| 起点:", vals[0], "| 峰值:", max(vals),
              "| 末值:", vals[-1], "/", pos[-1][1])
        print("回退次数:", len(backs), "| 最大回退:", max(backs) if backs else 0)
    else:
        print("FAIL: 没有任何匹配记录")

    return 0 if pos and max(p_[0] for p_ in pos) > 40 else 2


if __name__ == "__main__":
    raise SystemExit(main())
