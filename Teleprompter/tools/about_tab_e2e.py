# -*- coding: utf-8 -*-
"""关于页验收：点第四个页签「关于」，截图 + 校验作者邮箱 / 复制 / 二维码占位可见。"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
u32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
u32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
u32.FindWindowW.restype = wintypes.HWND
u32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
u32.GetDlgItem.restype = wintypes.HWND
u32.IsWindowVisible.argtypes = [wintypes.HWND]
u32.IsWindowVisible.restype = wintypes.BOOL
u32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
u32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
u32.GetDpiForWindow.argtypes = [wintypes.HWND]
u32.GetDpiForWindow.restype = ctypes.c_uint
u32.SendMessageW.argtypes = [wintypes.HWND, ctypes.c_uint, wintypes.WPARAM, wintypes.LPARAM]
u32.SendMessageW.restype = ctypes.c_void_p
u32.mouse_event.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                            wintypes.DWORD, ctypes.c_size_t]
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"

IDC_TAB_ABOUT = 1075
IDC_ST_CONTACT = 1076
IDC_BTN_COPY_CONTACT = 1077
IDC_ST_QR = 1078

from PIL import ImageGrab

FAIL = []


def check(cond, msg):
    print(("  PASS " if cond else "  FAIL ") + msg)
    if not cond:
        FAIL.append(msg)


def rect(h):
    r = wintypes.RECT()
    u32.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def to_screen(h, x, y):
    pt = wintypes.POINT(int(x), int(y))
    u32.ClientToScreen(h, ctypes.byref(pt))
    return pt.x, pt.y


def click(x, y, settle=0.6):
    u32.SetCursorPos(int(x), int(y))
    time.sleep(0.1)
    u32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.06)
    u32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(settle)


def text_of(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 2)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


subprocess.run(["taskkill", "/F", "/IM", "TeacherPrompter.exe"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)

env = dict(os.environ, TP_CAPTURE_ALLOWED="1")
p = subprocess.Popen([EXE], env=env)
time.sleep(3.0)

dlg = u32.FindWindowW(None, DLG)
if not dlg:
    print("FAIL: 找不到主面板")
    p.terminate()
    sys.exit(1)

dpi = u32.GetDpiForWindow(dlg) or 96
s = dpi / 96.0

# 第四个页签：直接按控件自己的位置点（比按设计坐标算更稳）
tab4 = u32.GetDlgItem(dlg, IDC_TAB_ABOUT)
check(bool(tab4), "找到「关于」页签")
tl, tt, tr, tb = rect(tab4)
print("   关于页签 rect = %s  面板 rect = %s" % ((tl, tt, tr, tb), rect(dlg)))
click((tl + tr) / 2, (tt + tb) / 2)
time.sleep(0.6)

wx = u32.GetDlgItem(dlg, IDC_ST_CONTACT)
btn = u32.GetDlgItem(dlg, IDC_BTN_COPY_CONTACT)
qr = u32.GetDlgItem(dlg, IDC_ST_QR)

check(bool(wx) and bool(u32.IsWindowVisible(wx)), "邮箱控件可见")
check(bool(btn) and bool(u32.IsWindowVisible(btn)), "复制邮箱按钮可见")
check(bool(qr) and bool(u32.IsWindowVisible(qr)), "二维码占位可见")
wxtxt = ""
if wx:
    wxtxt = text_of(wx)
    print("   邮箱文本 = %r" % wxtxt)
    check(wxtxt == "yundou@agent.qq.com", "邮箱地址正确")

l, t, r, b = rect(dlg)
shot = os.path.join(ROOT, "build", "shot_panel_about.png")
ImageGrab.grab(bbox=(l, t, r, b), all_screens=True).save(shot)
print("   截图：%s" % shot)

# 点一下「复制邮箱」，按钮文字应临时变成「已复制」
if btn:
    bl, bt, br, bb = rect(btn)
    click((bl + br) / 2, (bt + bb) / 2, settle=0.35)
    print("   复制后按钮文字 = %r" % text_of(btn))
    check("已复制" in text_of(btn), "复制按钮给出成功反馈")
    clip = ctypes.windll.user32.OpenClipboard(0)
    if clip:
        u32.GetClipboardData.argtypes = [ctypes.c_uint]
        u32.GetClipboardData.restype = ctypes.c_void_p
        h = u32.GetClipboardData(13)  # CF_UNICODETEXT
        if h:
            k32 = ctypes.windll.kernel32
            k32.GlobalLock.argtypes = [ctypes.c_void_p]
            k32.GlobalLock.restype = ctypes.c_void_p
            k32.GlobalUnlock.argtypes = [ctypes.c_void_p]
            ptr = k32.GlobalLock(h)
            got = ctypes.wstring_at(ptr)
            k32.GlobalUnlock(h)
            print("   剪贴板 = %r" % got)
            check(got == wxtxt, "剪贴板内容与邮箱一致")
        ctypes.windll.user32.CloseClipboard()

# 切回稿件页，确认关于页控件被隐藏
click(*to_screen(dlg, (16 + 42) * s, (76 + 26) * s), settle=0.4)
check(not u32.IsWindowVisible(qr), "切走后二维码隐藏")
check(not u32.IsWindowVisible(wx), "切走后邮箱隐藏")

p.terminate()
time.sleep(0.4)
print("RESULT: %s" % ("PASS" if not FAIL else "FAIL %s" % FAIL))
sys.exit(0 if not FAIL else 1)
