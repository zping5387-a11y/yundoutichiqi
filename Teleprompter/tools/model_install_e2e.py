# -*- coding: utf-8 -*-
"""离线语音模型「整包 tar.bz2」兜底通道的端到端测试。

默认通道已改成 HF 镜像逐文件下载（见 model_mirror_e2e.py）。本测试专门盯
TP_MODEL_URL 指定的整包通道 —— 用户自建单文件镜像、或需要退回 GitHub 时走这条。
测试用本地 HTTP 服务提供一个同结构的小 tar.bz2（几 MB，内容随便），验证：

  1. 缺模型时状态行提示「点这里下载」，点击后能弹确认、下载、解压、装到 exe\\models
  2. 断点续传：预置一半的 .part，点击后只请求剩余字节（服务端收到 Range）
  3. 下载中点状态行可以暂停，.part 保留、状态显示「已暂停」

注意：exe 必须放在一个「向上几层都没有 models 目录」的位置，否则
SherpaEngine::FindModelDir 会找到工程里的真模型，测不到下载流程。
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
TAR = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "tar.exe")

DLG = "芸豆提词器 · 教学模式"
IDC_ST_SPEECH = 1012
IDC_TAB_VOICE = 1071
BN_CLICKED = 0
STN_CLICKED = 0
WM_COMMAND = 0x0111
IDOK, IDYES = 1, 6

u32 = ctypes.windll.user32
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
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


# ---------------------------------------------------------------- 本地下载服务
class Handler(http.server.BaseHTTPRequestHandler):
    data = b""
    ranges = []          # 收到的 Range 头，用于断言续传
    throttle = 0.0       # 每块之间的 sleep，制造可观察的下载过程
    cut_at = None        # 非 None 时只发这么多字节就断开（模拟网络中断）

    def log_message(self, *_a):
        pass

    def do_GET(self):
        rng = self.headers.get("Range")
        Handler.ranges.append(rng or "")
        total = len(Handler.data)
        start = 0
        if rng:
            m = re.match(r"bytes=(\d+)-", rng)
            if m:
                start = int(m.group(1))
        body = Handler.data[start:]
        if start:
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, total - 1, total))
        else:
            self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Type", "application/octet-stream")
        self.end_headers()
        if Handler.cut_at is not None:
            self.wfile.write(body[:Handler.cut_at])     # 声明全长却只发一半 → 客户端读到头
            self.close_connection = True
            return
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
def build_fake_package(tmp):
    """造一个结构一致的假模型包（小文件，只验证下载/解压/安装链路）"""
    src = os.path.join(tmp, "pkg", MODEL_NAME)
    os.makedirs(src, exist_ok=True)
    sizes = {"encoder.int8.onnx": 6 * 1024 * 1024, "decoder.onnx": 2 * 1024 * 1024,
             "joiner.int8.onnx": 1024 * 1024}
    for name, size in sizes.items():
        with open(os.path.join(src, name), "wb") as f:
            f.write(os.urandom(size))
    with open(os.path.join(src, "tokens.txt"), "w", encoding="utf-8") as f:
        f.write("a 1\nb 2\n")

    tar_path = os.path.join(tmp, "model.tar.bz2")
    subprocess.run([TAR, "-cjf", tar_path, "-C", os.path.dirname(src), MODEL_NAME], check=True)
    return tar_path


def prepare_app(tmp):
    """exe 放到多层目录里，保证 FindModelDir 找不到工程的 models"""
    app = os.path.join(tmp, "deep", "app")
    os.makedirs(app, exist_ok=True)
    for name in ["TeacherPrompter.exe", "sherpa-onnx-c-api.dll", "onnxruntime.dll",
                 "onnxruntime_providers_shared.dll"]:
        shutil.copy2(os.path.join(BUILD, name), os.path.join(app, name))
    return app


def pump(dialog_titles):
    """关掉程序弹出来的模态对话框（确认下载 / 暂停 / 安装完成）

    MessageBox 对 WM_COMMAND 不一定响应，直接点按钮（按钮 ID 就是 IDYES/IDOK）最稳。
    """
    for title, cmd in dialog_titles:
        h = u32.FindWindowW(None, title)
        if not h:
            continue
        btn = u32.GetDlgItem(h, cmd)
        if btn:
            u32.SendMessageW(btn, 0x00F5, 0, 0)      # BM_CLICK
        else:
            u32.SendMessageW(h, WM_COMMAND, cmd, 0)
        time.sleep(0.4)


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


def launch(app, url):
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
    time.sleep(0.4)
    env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_MODEL_URL=url)
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
    # 必须 PostMessage：点状态行会触发模态确认框，SendMessage 会等处理函数返回，
    # 处理函数又被模态框卡住 -> 测试线程死锁，pump 没机会去点框上的按钮。
    u32.PostMessageW(dlg, WM_COMMAND, (BN_CLICKED << 16) | cid, 0)
    return True


def stop(p):
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
    try:
        p.wait(timeout=15)
    except Exception:
        pass


def main():
    tmp = tempfile.mkdtemp(prefix="ydtp_model_")
    print("临时目录:", tmp)
    ok = True

    tar_path = build_fake_package(tmp)
    Handler.data = open(tar_path, "rb").read()
    Handler.throttle = 0.02
    srv = start_server()
    url = "http://127.0.0.1:%d/model.tar.bz2" % srv.server_address[1]
    print("下载地址:", url, "| 包大小: %.2f MB" % (len(Handler.data) / 1048576))

    app = prepare_app(tmp)
    model_dir = os.path.join(app, "models", MODEL_NAME)
    dialogs = [("下载语音模型", IDYES), ("语音模型安装完成", IDOK), ("暂停下载", IDYES),
               ("还没有稿件", IDOK)]

    # ---------- 用例 1：干净安装 ----------
    stop_proc = None
    p, dlg = launch(app, url)
    if not dlg:
        print("FAIL 主窗口没起来")
        return 1
    click(dlg, IDC_TAB_VOICE)
    time.sleep(0.5)

    hint, _ = wait_status(dlg, lambda s: True, 3, dialogs)
    print("初始状态:", hint)
    if "点这里下载" not in hint:
        print("FAIL 缺模型时没有提示下载")

    Handler.ranges.clear()
    click(dlg, IDC_ST_SPEECH)          # 点状态行 → 弹确认 → 是
    st, cost = wait_status(dlg, lambda s: ("已安装" in s) or ("失败" in s), 90, dialogs)
    print("用例1 结束状态: %s（%.1fs）" % (st, cost))
    installed = os.path.isfile(os.path.join(model_dir, "encoder.int8.onnx"))
    print("  模型落盘:", installed, "| 服务端 Range:", Handler.ranges)
    if not (installed and "已安装" in st):
        print("FAIL 用例1")
        ok = False
    stop(p)

    # ---------- 用例 2：断点续传 ----------
    shutil.rmtree(os.path.join(app, "models"), ignore_errors=True)
    dl = os.path.join(app, "models", "_download")
    os.makedirs(dl, exist_ok=True)
    part = os.path.join(dl, MODEL_NAME + ".tar.bz2.part")
    with open(part, "wb") as f:
        f.write(Handler.data[: len(Handler.data) // 3])

    Handler.ranges.clear()
    p, dlg = launch(app, url)
    time.sleep(0.6)
    click(dlg, IDC_TAB_VOICE)
    time.sleep(0.4)
    click(dlg, IDC_ST_SPEECH)
    st, cost = wait_status(dlg, lambda s: ("已安装" in s) or ("失败" in s), 90, dialogs)
    print("用例2 结束状态: %s（%.1fs）" % (st, cost))
    got_range = any(r.startswith("bytes=") for r in Handler.ranges)
    installed2 = os.path.isfile(os.path.join(model_dir, "encoder.int8.onnx"))
    print("  服务端 Range:", Handler.ranges, "| 模型落盘:", installed2)
    if not (installed2 and got_range):
        print("FAIL 用例2（未走续传）")
        ok = False
    stop(p)

    # ---------- 用例 3：下载中可暂停 ----------
    shutil.rmtree(os.path.join(app, "models"), ignore_errors=True)
    Handler.throttle = 0.25           # 放慢，保证来得及取消
    p, dlg = launch(app, url)
    time.sleep(0.6)
    click(dlg, IDC_TAB_VOICE)
    time.sleep(0.4)
    st0, _ = wait_status(dlg, lambda s: ("未安装" in s) or ("已安装" in s), 15, dialogs)
    print("用例3 初始状态: %s | 模型目录存在: %s"
          % (st0, os.path.isdir(os.path.join(app, "models"))))
    if "未安装" not in st0:
        print("FAIL 用例3（启动时模型目录没删干净，测不到下载）")
        ok = False
        stop(p)
    else:
        Handler.ranges.clear()
        click(dlg, IDC_ST_SPEECH)
        wait_status(dlg, lambda s: ("正在下载" in s) or ("连接" in s) or ("启动" in s), 20, dialogs)
        time.sleep(2.0)               # 保证已进入下载中段
        click(dlg, IDC_ST_SPEECH)         # 点状态行 = 暂停
        st, _ = wait_status(dlg, lambda s: ("已暂停" in s) or ("失败" in s), 20, dialogs)
        part_exists = os.path.isfile(part) and os.path.getsize(part) > 0
        print("用例3 结束状态: %s | .part 保留: %s" % (st, part_exists))
        if "已暂停" not in st or not part_exists:
            print("FAIL 用例3")
            ok = False
        stop(p)

    # ---------- 用例 4：下载被掐断 ----------
    # 服务端声明完整长度却只发一半：以前会把 0 字节当成「正常结束」，留下截断的包，
    # 之后 tar 必然报 truncated / 退出码 1。现在必须报「不完整」且保留 .part 以便续传。
    shutil.rmtree(os.path.join(app, "models"), ignore_errors=True)
    Handler.throttle = 0.0
    Handler.cut_at = len(Handler.data) // 2
    p, dlg = launch(app, url)
    time.sleep(0.6)
    click(dlg, IDC_TAB_VOICE)
    time.sleep(0.4)
    click(dlg, IDC_ST_SPEECH)
    st, _ = wait_status(dlg, lambda s: ("失败" in s) or ("不完整" in s), 60, dialogs)
    part_kept = os.path.isfile(part) and os.path.getsize(part) > 0
    print("用例4 结束状态: %s | .part 保留: %s" % (st, part_kept))
    if not ("不完整" in st or "中断" in st) or not part_kept:
        print("FAIL 用例4（下载被掐断后应报不完整并保留进度）")
        ok = False
    stop(p)

    # ---------- 用例 5：包本身是坏的（长度对、内容是乱码）----------
    # 期望：解压失败 → 明确提示「安装包不完整」→ 删掉 .part 让下次从头下
    shutil.rmtree(os.path.join(app, "models"), ignore_errors=True)
    Handler.cut_at = None
    good = Handler.data
    Handler.data = os.urandom(1024 * 1024)
    p, dlg = launch(app, url)
    time.sleep(0.6)
    click(dlg, IDC_TAB_VOICE)
    time.sleep(0.4)
    click(dlg, IDC_ST_SPEECH)
    st, _ = wait_status(dlg, lambda s: ("安装失败" in s) or ("解压失败" in s), 60, dialogs)
    part_left = os.path.isfile(part)
    print("用例5 结束状态: %s | 坏包已清理: %s" % (st, not part_left))
    if "不完整" not in st or part_left:
        print("FAIL 用例5（坏包应提示不完整并删除 .part）")
        ok = False
    stop(p)
    Handler.data = good

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
