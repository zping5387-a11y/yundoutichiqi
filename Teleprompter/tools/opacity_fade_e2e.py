# -*- coding: utf-8 -*-
"""浮层不透明度/边缘渐隐回归。

背景：浮层渲染时有「上下边缘渐隐」后处理，让半透明浮层融进桌面。
修复前：完全不透明（不透明度 100%）时上下各约 64px 仍被渐隐到透明，
       看起来像透明度设置不生效。
断言：
  1. bgAlpha=255 -> 整个浮层（含上下边缘）必须是均匀实色，不允许透出桌面
  2. bgAlpha=70  -> 上边缘保持渐隐（背景差分法：与隐藏浮层时的底图逐行比差异，
     顶部渐隐行差异≈0，往下 20px 差异明显增大 —— 不受桌面内容干扰）
"""
import ctypes, os, re, statistics, subprocess, time
from ctypes import wintypes

u32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass
from PIL import ImageGrab

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
INI = os.path.join(ROOT, "build", "TeacherPrompter.ini")
DATA = os.path.join(ROOT, "build", "testdata_fade")
OVERLAY = "芸豆提词"


def window_text(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def window_rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def find_overlay():
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _):
        if u32.IsWindowVisible(h) and window_text(h) == OVERLAY:
            hit.append(h)
        return True

    u32.EnumWindows(cb, 0)
    return hit[0] if hit else None


def kill():
    subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.6)


def launch_with_alpha(alpha):
    txt = open(INI, "rb").read().decode("gbk", "surrogateescape")
    txt = re.sub(r"(?im)^\s*bgAlpha\s*=.*$", "", txt)
    out, done = [], False
    for ln in txt.splitlines():
        out.append(ln)
        if not done and ln.strip().lower() == "[view]":
            out.append("bgAlpha=%d" % alpha)
            done = True
    open(INI, "wb").write(("\r\n".join(out) + "\r\n").encode("gbk", "surrogateescape"))

    os.makedirs(DATA, exist_ok=True)
    env = dict(os.environ, TP_CAPTURE_ALLOWED="1", TP_DATA_DIR=DATA)
    subprocess.Popen([EXE], env=env)
    time.sleep(3.4)
    return find_overlay()


def show(h, on):
    # 64 位下 HWND 必须显式声明参数类型，否则句柄被截断、ShowWindow 静默失败
    u32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    u32.ShowWindow.restype = wintypes.BOOL
    u32.ShowWindow(h, 5 if on else 0)      # SW_SHOW / SW_HIDE
    time.sleep(0.9)                        # 分层窗口重新合成要时间，0.45s 会截到旧画面
    u32.IsWindowVisible.argtypes = [wintypes.HWND]
    u32.IsWindowVisible.restype = wintypes.BOOL
    return bool(u32.IsWindowVisible(h))


# ---- 纯黑垫底窗口：bgAlpha=70 时浮层叠在明亮壁纸上整体只差 ~4 个亮度级，
# 屏幕中位数永远测不出渐隐。垫一块黑色窗口后：渐隐行≈0（黑）、
# 正常行≈68（250*70/255），差异一目了然，与桌面内容无关。 ----
g32 = ctypes.windll.gdi32
k32 = ctypes.windll.kernel32
BACKDROP_CLASS = "TPFadeBackdrop"


class WNDCLASSW(ctypes.Structure):
    _fields_ = [("style", wintypes.UINT), ("lpfnWndProc", ctypes.c_void_p),
                ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                ("hInstance", wintypes.HINSTANCE), ("hIcon", wintypes.HANDLE),
                ("hCursor", wintypes.HANDLE), ("hbrBackground", wintypes.HANDLE),
                ("lpszMenuName", wintypes.LPCWSTR), ("lpszClassName", wintypes.LPCWSTR)]


class MSG(ctypes.Structure):
    _fields_ = [("hwnd", wintypes.HWND), ("message", wintypes.UINT),
                ("wParam", wintypes.WPARAM), ("lParam", wintypes.LPARAM),
                ("time", wintypes.DWORD), ("pt", wintypes.POINT)]


def make_backdrop(x, y, w, h):
    u32.RegisterClassW.argtypes = [ctypes.POINTER(WNDCLASSW)]
    u32.CreateWindowExW.argtypes = [wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                    wintypes.DWORD, ctypes.c_int, ctypes.c_int,
                                    ctypes.c_int, ctypes.c_int, wintypes.HWND,
                                    wintypes.HANDLE, wintypes.HINSTANCE, wintypes.LPVOID]
    wc = WNDCLASSW()
    wc.lpfnWndProc = ctypes.cast(u32.DefWindowProcW, ctypes.c_void_p).value
    wc.hInstance = k32.GetModuleHandleW(None)
    wc.hbrBackground = g32.CreateSolidBrush(0x00000000)      # 纯黑
    wc.lpszClassName = BACKDROP_CLASS
    if not u32.RegisterClassW(ctypes.byref(wc)):
        return None
    # WS_POPUP|WS_VISIBLE + 不激活
    return u32.CreateWindowExW(0x08000000 | 0x08000000, BACKDROP_CLASS, "",
                               0x80000000 | 0x10000000, x, y, w, h,
                               None, None, wc.hInstance, None)


def pump(sec):
    """派发消息让垫底窗口完成首次绘制。"""
    u32.PeekMessageW.argtypes = [ctypes.POINTER(MSG), wintypes.HWND,
                                 wintypes.UINT, wintypes.UINT, wintypes.UINT]
    msg = MSG()
    end = time.time() + sec
    while time.time() < end:
        while u32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
            u32.TranslateMessage(ctypes.byref(msg))
            u32.DispatchMessageW(ctypes.byref(msg))
        time.sleep(0.02)


def kill_backdrop(h):
    if h:
        u32.DestroyWindow.argtypes = [wintypes.HWND]
        u32.DestroyWindow(h)
    u32.UnregisterClassW.argtypes = [wintypes.LPCWSTR, wintypes.HINSTANCE]
    u32.UnregisterClassW(BACKDROP_CLASS, k32.GetModuleHandleW(None))


def band_stats(im, y0, y1, x0=None, x1=None):
    """取一行带内所有像素的亮度标准差与中位色"""
    w = im.size[0]
    x0 = 8 if x0 is None else x0
    x1 = w - 8 if x1 is None else x1
    px = []
    for y in range(y0, y1):
        for x in range(x0, x1, 3):
            p = im.getpixel((x, y))
            px.append((p[0] + p[1] + p[2]) / 3.0)
    med = statistics.median(px)
    return statistics.pstdev(px), med


def main():
    ok = True
    backup = open(INI, "rb").read()
    try:
        # ---- 1. 不透明度 100%：整块必须实心，上下边缘不许透 ----
        h = launch_with_alpha(255)
        if not h:
            print("FAIL: 找不到浮层")
            return 1
        im = ImageGrab.grab(bbox=window_rect(h), all_screens=True).convert("RGB")
        w, hh = im.size
        im.save(os.path.join(ROOT, "build", "shot_fade_opaque.png"))
        top_std, top_med = band_stats(im, 0, 4)
        # 底部最后 3px 是阅读进度条（正常内容），下边缘采样带往上避开
        bot_std, bot_med = band_stats(im, hh - 12, hh - 8)
        mid_std, mid_med = band_stats(im, hh // 2 - 2, hh // 2 + 2)
        print("bgAlpha=255: 上缘亮度σ=%.1f 中位=%.0f  下缘σ=%.1f 中位=%.0f  中部σ=%.1f 中位=%.0f"
              % (top_std, top_med, bot_std, bot_med, mid_std, mid_med))
        bad = []
        if top_std > 6 or abs(top_med - mid_med) > 12:
            bad.append("上边缘透出背景")
        if bot_std > 6 or abs(bot_med - mid_med) > 12:
            bad.append("下边缘透出背景")
        if bad:
            print("   FAIL: %s（不透明度 100%% 应完全实心）" % "、".join(bad))
            ok = False
        else:
            print("   [OK] 不透明度 100% 时整块实心")
        kill()

        # ---- 2. 半透明（默认 70）：上边缘应保持渐隐 ----
        # 浮层 27% 透明度叠在明亮壁纸上整体只差 ~4 个亮度级，屏幕直接比测不出。
        # 在浮层正下方垫一块纯黑窗口：渐隐行 alpha≈0 -> 接近纯黑，
        # 正常行 alpha=70/255 -> 亮度 ≈ 250*70/255 ≈ 68，与桌面内容无关。
        h = launch_with_alpha(70)
        if not h:
            print("FAIL: 第二次启动找不到浮层")
            return 1
        l, t, r, b = window_rect(h)
        # 保证浮层在垫底窗口之上（TOPMOST 带）
        u32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int,
                                     ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
        u32.SetWindowPos(h, -1, 0, 0, 0, 0, 0x1 | 0x2 | 0x10)   # TOPMOST|NOMOVE|NOSIZE|NOACTIVATE
        bd = make_backdrop(l, t, r - l, b - t)
        if not bd:
            print("FAIL: 垫底窗口创建失败")
            return 1
        pump(0.6)
        im = ImageGrab.grab(bbox=(l, t, r, b), all_screens=True).convert("RGB")
        w, hh = im.size
        im.save(os.path.join(ROOT, "build", "shot_fade_translucent.png"))
        top_std, top_med = band_stats(im, 0, 3)
        deeper_std, deeper_med = band_stats(im, 20, 23)
        mid_std, mid_med = band_stats(im, hh // 2 - 2, hh // 2 + 2)
        kill_backdrop(bd)
        print("bgAlpha=70 黑底: 顶=%.0f  +20px=%.0f  中部=%.0f（期望 顶≈0, 中部≈68）"
              % (top_med, deeper_med, mid_med))
        if top_med > 12:
            print("   FAIL: 上边缘没有渐隐（顶部仍有实体内容，应接近纯黑）")
            ok = False
        elif mid_med < 35:
            print("   FAIL: 中部没有内容（正常行应为 68 亮度左右）")
            ok = False
        else:
            print("   [OK] 半透明时上边缘渐隐正常")
        kill()
    finally:
        kill()
        open(INI, "wb").write(backup)

    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
