# -*- coding: utf-8 -*-
"""离线语音模型「镜像逐文件下载」端到端测试（v1.1.2 默认通道）。

背景：GitHub 上只有一个 570MB 的 tar.bz2，国内下到一半被掐断就整包作废，还要再赌
一次 bz2 解压（就是用户遇到的「安装包不完整」）。现在默认改成从 HF 镜像逐文件取，
不用 tar。本测试用本地 HTTP 服务模拟镜像，验证：

  1. 干净安装：5 个文件全部下完 → 状态「已安装」→ models\\<模型> 里文件齐全
  2. 增量修复：已存在的文件不重复下载，只补缺的那个
  3. 断点续传：预置一半的 .part，服务端应收到 Range 头
  4. 某个文件 404：状态要明确报「下载失败」并指出是哪个文件

exe 必须放在「向上几层都没有 models 目录」的位置，否则会被 FindModelDir 找到真模型。
"""
import ctypes
import http.server
import os
import re
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
MODEL_NAME = "sherpa-onnx-streaming-zipformer-zh-xlarge-int8-2025-06-30"

DLG = "芸豆提词器 · 教学模式"
IDC_ST_SPEECH = 1012
IDC_TAB_VOICE = 1071
BN_CLICKED = 0
WM_COMMAND = 0x0111
IDOK, IDYES = 1, 6

u32 = ctypes.windll.user32
u32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
u32.FindWindowW.restype = wintypes.HWND
u32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
u32.GetDlgItem.restype = wintypes.HWND
u32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
u32.GetWindowTextLengthW.restype = ctypes.c_int
u32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
u32.GetWindowTextW.restype = ctypes.c_int
u32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
u32.SendMessageW.restype = wintypes.LPARAM
u32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
u32.PostMessageW.restype = wintypes.BOOL

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass


def wtext(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


# ---------------------------------------------------------------- 假镜像服务
class Handler(http.server.BaseHTTPRequestHandler):
    files = {}          # 文件名 -> 内容
    missing = set()     # 故意 404 的文件名
    requests = []       # 请求过的文件名（按顺序）
    ranges = []         # (文件名, Range 头)
    throttle = 0.0

    def log_message(self, *_a):
        pass

    def do_GET(self):
        name = os.path.basename(self.path.split("?")[0])
        Handler.requests.append(name)
        Handler.ranges.append((name, self.headers.get("Range") or ""))

        if name in Handler.missing or name not in Handler.files:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        data = Handler.files[name]
        total = len(data)
        start = 0
        rng = self.headers.get("Range")
        if rng:
            m = re.match(r"bytes=(\d+)-", rng)
            if m:
                start = int(m.group(1))
        body = data[start:]
        if start:
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, total - 1, total))
        else:
            self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Type", "application/octet-stream")
        self.end_headers()

        step = 128 * 1024
        for i in range(0, len(body), step):
            self.wfile.write(body[i:i + step])
            if Handler.throttle:
                time.sleep(Handler.throttle)


def start_server():
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
    srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


# ---------------------------------------------------------------- 测试辅助
def build_fake_files():
    """小文件即可：自定义镜像不校验大小，只验证下载/落盘链路"""
    return {
        "tokens.txt": b"a 1\nb 2\n",
        "bpe.model": os.urandom(4096),
        "joiner.int8.onnx": os.urandom(64 * 1024),
        "decoder.onnx": os.urandom(256 * 1024),
        "encoder.int8.onnx": os.urandom(3 * 1024 * 1024),
    }


def prepare_app(tmp):
    app = os.path.join(tmp, "deep", "app")
    os.makedirs(app, exist_ok=True)
    for name in ["TeacherPrompter.exe", "sherpa-onnx-c-api.dll", "onnxruntime.dll",
                 "onnxruntime_providers_shared.dll"]:
        shutil.copy2(os.path.join(BUILD, name), os.path.join(app, name))
    return app


def pump(dialog_titles):
    for title, cmd in dialog_titles:
        h = u32.FindWindowW(None, title)
        if not h:
            continue
        btn = u32.GetDlgItem(h, cmd)
        if btn:
            u32.SendMessageW(btn, 0x00F5, 0, 0)      # BM_CLICK
        else:
            u32.SendMessageW(h, WM_COMMAND, cmd, 0)
        time.sleep(0.3)


def wait_status(dlg, pred, timeout, dialogs):
    t0 = time.time()
    last = ""
    while time.time() - t0 < timeout:
        pump(dialogs)
        h = u32.GetDlgItem(dlg, IDC_ST_SPEECH)
        last = wtext(h) if h else ""
        if pred(last):
            return last, time.time() - t0
        time.sleep(0.3)
    return last, time.time() - t0


def launch(app, mirror):
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
    time.sleep(0.4)
    env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_MODEL_MIRROR=mirror)
    env.pop("TP_MODEL_URL", None)          # 确保走镜像逐文件通道
    p = subprocess.Popen([os.path.join(app, "TeacherPrompter.exe")], cwd=app, env=env)
    dlg = None
    for _ in range(40):
        time.sleep(0.3)
        dlg = u32.FindWindowW(None, DLG)
        if dlg:
            break
    return p, dlg


def click(dlg, cid):
    h = u32.GetDlgItem(dlg, cid)
    if not h:
        return False
    # 必须 PostMessage：处理函数里会弹模态确认框
    u32.PostMessageW(dlg, WM_COMMAND, (BN_CLICKED << 16) | cid, 0)
    return True


def stop(p):
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
    try:
        p.wait(timeout=15)
    except Exception:
        pass


def main():
    tmp = tempfile.mkdtemp(prefix="ydtp_mirror_")
    print("临时目录:", tmp)
    ok = True

    Handler.files = build_fake_files()
    Handler.throttle = 0.01
    srv = start_server()
    mirror = "http://127.0.0.1:%d/repo/resolve/main" % srv.server_address[1]
    print("镜像地址:", mirror)

    app = prepare_app(tmp)
    model_dir = os.path.join(app, "models", MODEL_NAME)
    dialogs = [("下载语音模型", IDYES), ("语音模型安装完成", IDOK), ("还没有稿件", IDOK)]

    # ---------- 用例 1：干净安装 ----------
    p, dlg = launch(app, mirror)
    if not dlg:
        print("FAIL 主窗口没起来")
        return 1
    click(dlg, IDC_TAB_VOICE)
    time.sleep(0.5)
    hint, _ = wait_status(dlg, lambda s: True, 3, dialogs)
    print("初始状态:", hint)
    if "点这里下载" not in hint:
        print("   FAIL 缺模型时没有提示下载")
        ok = False

    Handler.requests.clear()
    Handler.ranges.clear()
    click(dlg, IDC_ST_SPEECH)
    st, cost = wait_status(dlg, lambda s: ("已安装" in s) or ("失败" in s), 90, dialogs)
    files = sorted(os.listdir(model_dir)) if os.path.isdir(model_dir) else []
    print("用例1 结束状态: %s（%.1fs）" % (st, cost))
    print("  落地文件:", files)
    if "已安装" not in st or len(files) < 5:
        print("FAIL 用例1")
        ok = False
    # 不该再碰 tar 通道：一次都不能请求 tar.bz2
    if any(".tar.bz2" in n for n in Handler.requests):
        print("   FAIL 仍在走 tar.bz2 通道")
        ok = False
    stop(p)

    # ---------- 用例 2：增量修复，只补缺的文件 ----------
    if os.path.isdir(model_dir):
        os.remove(os.path.join(model_dir, "encoder.int8.onnx"))
    Handler.requests.clear()
    p, dlg = launch(app, mirror)
    time.sleep(0.6)
    click(dlg, IDC_TAB_VOICE)
    time.sleep(0.4)
    click(dlg, IDC_ST_SPEECH)
    st, cost = wait_status(dlg, lambda s: ("已安装" in s) or ("失败" in s), 90, dialogs)
    print("用例2 结束状态: %s（%.1fs）| 服务端请求: %s" % (st, cost, Handler.requests))
    if "已安装" not in st or Handler.requests != ["encoder.int8.onnx"]:
        print("FAIL 用例2（应只重下缺失的 encoder）")
        ok = False
    stop(p)

    # ---------- 用例 3：断点续传 ----------
    part = os.path.join(model_dir, "encoder.int8.onnx.part")
    data = Handler.files["encoder.int8.onnx"]
    if os.path.isfile(os.path.join(model_dir, "encoder.int8.onnx")):
        os.remove(os.path.join(model_dir, "encoder.int8.onnx"))
    with open(part, "wb") as f:
        f.write(data[: len(data) // 2])

    Handler.ranges.clear()
    p, dlg = launch(app, mirror)
    time.sleep(0.6)
    click(dlg, IDC_TAB_VOICE)
    time.sleep(0.4)
    click(dlg, IDC_ST_SPEECH)
    st, cost = wait_status(dlg, lambda s: ("已安装" in s) or ("失败" in s), 90, dialogs)
    got_range = any(n == "encoder.int8.onnx" and r.startswith("bytes=") for n, r in Handler.ranges)
    same = (os.path.getsize(os.path.join(model_dir, "encoder.int8.onnx")) == len(data)
            if os.path.isfile(os.path.join(model_dir, "encoder.int8.onnx")) else False)
    print("用例3 结束状态: %s（%.1fs）| Range: %s | 内容完整: %s"
          % (st, cost, [r for n, r in Handler.ranges if n == "encoder.int8.onnx"], same))
    if "已安装" not in st or not got_range or not same:
        print("FAIL 用例3（没走续传或续传后内容不对）")
        ok = False
    stop(p)

    # ---------- 用例 4：某个文件 404 ----------
    shutil.rmtree(os.path.join(app, "models"), ignore_errors=True)
    Handler.missing = {"encoder.int8.onnx"}
    p, dlg = launch(app, mirror)
    time.sleep(0.6)
    click(dlg, IDC_TAB_VOICE)
    time.sleep(0.4)
    click(dlg, IDC_ST_SPEECH)
    st, _ = wait_status(dlg, lambda s: ("失败" in s), 60, dialogs)
    print("用例4 结束状态:", st)
    if "失败" not in st or "encoder" not in st:
        print("FAIL 用例4（应提示下载失败并指出文件）")
        ok = False
    stop(p)
    Handler.missing = set()

    srv.shutdown()
    shutil.rmtree(tmp, ignore_errors=True)
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    raise SystemExit(main())
