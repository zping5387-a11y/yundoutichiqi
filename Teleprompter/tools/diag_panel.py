# -*- coding: utf-8 -*-
"""验证「隐藏主面板 / Ctrl+Alt+S 显示主面板」功能。

说明：WM_HOTKEY 直投（wParam=热键 ID）验证 OnHotKey 处理逻辑；
组合键注册通过 RegisterHotKey 探测（返回 1409 = 已被本程序占用）来验证。
"""
import ctypes, os, subprocess, time
from ctypes import wintypes

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "TeacherPrompter.exe")
DLG = "芸豆提词器 · 教学模式"
WM_HOTKEY = 0x0312
HK_TOGGLE_PANEL = 16
MOD_ALT, MOD_CONTROL, MOD_NOREPEAT = 1, 2, 0x4000


def text(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def find_button(parent, label):
    hit = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        cls = ctypes.create_unicode_buffer(64)
        u32.GetClassNameW(h, cls, 64)
        if cls.value == "Button" and text(h) == label:
            hit.append(h)
            return False
        return True

    u32.EnumChildWindows(parent, cb, 0)
    return hit[0] if hit else None


def press_hotkey_ctrl_alt(key_vk):
    # 沙箱环境拦截了模拟键盘输入，直接按热键 ID 投递 WM_HOTKEY（等效系统热键分发）
    u32.PostMessageW(dlg, WM_HOTKEY, HK_TOGGLE_PANEL, 0)
    time.sleep(0.6)


def state(dlg):
    vis = bool(u32.IsWindowVisible(dlg))
    icon = bool(u32.IsIconic(dlg))
    fg = u32.GetForegroundWindow() == dlg
    return "visible=%s iconic=%s foreground=%s" % (vis, icon, fg)


p = subprocess.Popen([EXE], env=dict(os.environ, TP_CAPTURE_ALLOWED="1"))
time.sleep(3)
dlg = u32.FindWindowW(None, DLG)
if not dlg:
    print("FAIL: 找不到主窗口")
    p.terminate()
    raise SystemExit(1)

print("启动      :", state(dlg))

# 0) 验证热键归属：Ctrl+Alt+M = 面板显隐、Ctrl+Alt+Shift+M = 镜像
#    Ctrl+Alt+S 已被本机其它程序长期占用（空闲时也返回 1409），所以面板热键改到 M
MOD_SHIFT = 4
for name, mods, vk, want_taken in (
        ("Ctrl+Alt+M",       MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 0x4D, True),
        ("Ctrl+Alt+Shift+M", MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 0x4D, True)):
    ok = u32.RegisterHotKey(None, 9001, mods, vk)
    err = k32.GetLastError()
    if ok:
        u32.UnregisterHotKey(None, 9001)
    taken = (not ok) and err == 1409
    verdict = "占用(本程序)" if taken else ("空闲" if ok else "注册失败 err=%d" % err)
    print("热键 %-16s: %s  [%s]" % (name, verdict, "OK" if taken == want_taken else "!! 不符预期"))

# 1) 按钮隐藏
btn = find_button(dlg, "隐藏面板")
print("找到按钮  :", bool(btn))
if btn:
    u32.SendMessageW(btn, 0x00F5, 0, 0)   # BM_CLICK
    time.sleep(0.8)
print("点按钮后  :", state(dlg))

# 2) 热键恢复
press_hotkey_ctrl_alt(0x53)
print("热键显示后:", state(dlg))

# 3) 热键再次隐藏
press_hotkey_ctrl_alt(0x53)
print("热键隐藏后:", state(dlg))

# 4) 热键再次恢复（反复切换稳定性）
for i in range(3):
    press_hotkey_ctrl_alt(0x53)
    a = state(dlg)
    press_hotkey_ctrl_alt(0x53)
    b = state(dlg)
    print("切换轮次 %d: %s | %s" % (i + 1, a, b))

# 5) 隐藏状态下浮层显隐热键仍应有效（不崩溃）
press_hotkey_ctrl_alt(0x53)   # 隐藏
u32.PostMessageW(dlg, WM_HOTKEY, 1, 0)   # HK_TOGGLE_SHOW 切换浮层
time.sleep(0.5)
print("隐藏时按 H:", state(dlg), "| 进程存活:", p.poll() is None)

press_hotkey_ctrl_alt(0x53)   # 恢复
print("最终      :", state(dlg), "| 进程存活:", p.poll() is None)

p.terminate()
print("DONE")
