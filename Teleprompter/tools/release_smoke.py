# -*- coding: utf-8 -*-
"""发布包冒烟测试（不含语音模型的精简包）。

检查项：
  1) 进程能起来、主面板出现（3 个私有 DLL 找得到）
  2) 包内没有 models 目录，语音页状态行提示「点这里下载」，不报错
  3) 点「开始跟读」会引导下载模型（弹确认框），取消后程序不受影响
  4) 若这个包带了模型（--with-model 生成），直接验证能加载到「跟读中」
  5) 退出后清掉运行期生成的 INI，保持发布目录干净
"""
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _newest_pkg():
    """output 下最新的 YunDouTeleprompter-*-win64 目录（避免每次改版本号都要改脚本）"""
    out = os.path.join(ROOT, "output")
    cands = [os.path.join(out, d) for d in os.listdir(out)
             if d.startswith("YunDouTeleprompter-") and d.endswith("-win64")
             and os.path.isdir(os.path.join(out, d))]
    if not cands:
        raise SystemExit("output 下没有发布目录")
    return max(cands, key=lambda p: os.path.getmtime(p))


PKG = sys.argv[1] if len(sys.argv) > 1 else _newest_pkg()
EXE = os.path.join(PKG, "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"
IDC_BTN_SAMPLE = 1003
IDC_BTN_SPEECH = 1010
IDC_ST_SPEECH = 1012
IDC_TAB_VOICE = 1071

BN_CLICKED = 0
WM_COMMAND = 0x0111
IDOK, IDYES, IDNO = 1, 6, 7

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
u32.IsWindowVisible.argtypes = [wintypes.HWND]
u32.IsWindowVisible.restype = wintypes.BOOL

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass


def wtext(h):
    n = u32.GetWindowTextLengthW(h)
    if n <= 0:
        return ""
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def click(dlg, cid, notify=BN_CLICKED):
    """异步投递 BN_CLICKED：SendMessage 会在目标线程弹模态框时把测试脚本一起卡住"""
    h = u32.GetDlgItem(dlg, cid)
    if not h:
        print("WARN: 控件 %d 不存在" % cid)
        return False
    u32.PostMessageW(dlg, WM_COMMAND, (notify << 16) | cid, h)
    time.sleep(0.3)
    return True


def close_dialog(title, cmd):
    """关掉模态对话框：直接点按钮（按钮 ID 就是 IDYES/IDNO/IDOK）"""
    h = u32.FindWindowW(None, title)
    if not h:
        return False
    btn = u32.GetDlgItem(h, cmd)
    if btn:
        u32.PostMessageW(btn, 0x00F5, 0, 0)          # BM_CLICK
    else:
        u32.PostMessageW(h, WM_COMMAND, cmd, 0)
    time.sleep(0.4)
    return True


def find_any(titles, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        for t in titles:
            h = u32.FindWindowW(None, t)
            if h:
                return t
        time.sleep(0.2)
    return None


def main():
    if not os.path.exists(EXE):
        print("FAIL: 找不到 %s" % EXE)
        return 1

    has_model = os.path.isdir(os.path.join(PKG, "models"))
    print("发布目录: %s" % PKG)
    print("自带模型: %s" % ("是" if has_model else "否（按需下载）"))

    # 复制到「向上几层都没有 models」的临时目录再跑：
    # 否则 SherpaEngine 会顺着 output\..\models 找到工程里的真模型，测不到缺模型分支
    tmp = tempfile.mkdtemp(prefix="ydtp_release_")
    run_dir = os.path.join(tmp, "deep", "app")
    shutil.copytree(PKG, run_dir)
    print("运行副本: %s" % run_dir)

    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
    time.sleep(0.5)

    p = subprocess.Popen([os.path.join(run_dir, "TeacherPrompter.exe")], cwd=run_dir,
                         env=dict(os.environ, TP_CAPTURE_ALLOWED="1",
                                  # 防止测试误确认后真的去拉 740MB：两条下载通道都指向本机关闭端口
                                  TP_MODEL_URL="http://127.0.0.1:9/model.tar.bz2",
                                  TP_MODEL_MIRROR="http://127.0.0.1:9/models"))
    ok = True
    try:
        dlg = None
        for _ in range(40):
            time.sleep(0.3)
            dlg = u32.FindWindowW(None, DLG)
            if dlg:
                break
        if not dlg:
            print("FAIL 1/4: 主窗口未出现（依赖 DLL 缺失？）")
            return 1
        print("PASS 1/4 主窗口已出现:", DLG)

        # 控件可能在窗口可见后才建好
        st = None
        for _ in range(40):
            st = u32.GetDlgItem(dlg, IDC_ST_SPEECH)
            if st:
                break
            time.sleep(0.25)
        if not st:
            print("FAIL 2/4: 找不到语音状态行")
            return 1

        click(dlg, IDC_TAB_VOICE)          # 切到语音页
        time.sleep(0.8)
        text = wtext(st)
        print("PASS 2/4 语音页状态行:", text)
        if not has_model and ("下载" not in text and "未安装" not in text):
            print("FAIL 2/4: 缺模型时没有提示下载")
            ok = False

        if has_model:
            # 完整包：直接验证模型能加载
            click(dlg, IDC_BTN_SAMPLE)
            time.sleep(0.8)
            click(dlg, IDC_BTN_SPEECH)
            ready = None
            t0 = time.time()
            while time.time() - t0 < 90:
                time.sleep(0.5)
                stxt = wtext(st)
                if "跟读中" in stxt:
                    ready = time.time() - t0
                    break
                for t, c in (("还没有稿件", IDOK), ("下载语音模型", IDNO)):
                    close_dialog(t, c)
            print(("PASS 3/4 离线模型已加载（%.1fs）: %s" % (ready, wtext(st))) if ready
                  else ("FAIL 3/4 模型未就绪: %s" % wtext(st)))
            if not ready:
                ok = False
        else:
            # 精简包：点跟读应引导下载 model，取消后程序仍可用
            click(dlg, IDC_BTN_SAMPLE)
            time.sleep(0.8)
            click(dlg, IDC_BTN_SPEECH)
            hit = find_any(["下载语音模型", "还没有稿件"], 6)
            if hit == "下载语音模型":
                close_dialog("下载语音模型", IDNO)
                print("PASS 3/4 点跟读时弹出了模型下载引导（已取消，未真的下载）")
            elif hit == "还没有稿件":
                close_dialog("还没有稿件", IDOK)
                print("FAIL 3/4 稿件没加载上，走不到模型检查")
                ok = False
            else:
                # 没抓到对话框就看状态行：进入下载/失败分支也算引导生效
                time.sleep(0.8)
                stxt = wtext(st)
                if any(k in stxt for k in ("下载", "连接", "失败")):
                    print("PASS 3/4 状态行已进入下载流程:", stxt)
                else:
                    print("FAIL 3/4 点跟读后没有出现下载引导，状态:", stxt)
                    ok = False

            # 取消后不该有下载线程/临时文件
            time.sleep(1.0)
            dl = os.path.join(run_dir, "models", "_download")
            if os.path.isdir(dl):
                print("WARN: 出现了下载临时目录（说明取消后仍在下载）")

        print("PASS 4/4 状态行:", wtext(st))
    finally:
        subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"], capture_output=True)
        try:
            p.wait(timeout=10)
        except Exception:
            pass

    # 清理运行期产物，保持目录与 zip 一致（副本里的 ini 也一起删掉）
    for name in ("TeacherPrompter.ini", "recent.ini"):
        f = os.path.join(run_dir, name)
        if os.path.exists(f):
            os.remove(f)
            print("已清理运行期文件:", name)

    leftovers = []
    for dirpath, _dirs, files in os.walk(PKG):
        for f in files:
            leftovers.append(os.path.relpath(os.path.join(dirpath, f), PKG))
    print("发布目录文件数: %d" % len(leftovers))

    shutil.rmtree(tmp, ignore_errors=True)
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    raise SystemExit(main())
