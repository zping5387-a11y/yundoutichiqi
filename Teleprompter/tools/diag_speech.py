import ctypes, subprocess, time, os
from ctypes import wintypes

u32 = ctypes.WinDLL('user32', use_last_error=True)
u32.FindWindowW.restype = wintypes.HWND
u32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]

def text_of(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value

def find_child(parent, txt):
    res = []
    EP = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if text_of(h) == txt:
            res.append(h)
            return False
        return True
    u32.EnumChildWindows(parent, EP(cb), 0)
    return res[0] if res else None

def click(h, wait=0.8):
    u32.SendMessageW(h, 0x00F5, 0, 0)
    time.sleep(wait)

def child_text(hwnd, cid):
    h = u32.GetDlgItem(hwnd, cid)
    return text_of(h) if h else None

exe = r"C:\Users\54881\WorkBuddy\2026-10-20-19-29-44\Teleprompter\build\TeacherPrompter.exe"
p = subprocess.Popen([exe])
time.sleep(3)

hwnd = u32.FindWindowW(None, "芸豆提词器 · 教学模式")
print("main:", hwnd)
click(find_child(hwnd, "示例文稿"))
click(find_child(hwnd, "开始跟读"), 2.0)

print("status  :", child_text(hwnd, 1012))
print("btn     :", child_text(hwnd, 1010))
mb = u32.FindWindowW("#32770", None)
print("msgbox  :", mb, text_of(mb) if mb else None)
print("alive   :", p.poll() is None)

# 停止跟读，检查能否正常停止
btn = u32.GetDlgItem(hwnd, 1010)
if btn and text_of(btn) == "停止跟读":
    click(btn, 1.0)
    print("after stop:", child_text(hwnd, 1012))

print("alive2  :", p.poll() is None)
