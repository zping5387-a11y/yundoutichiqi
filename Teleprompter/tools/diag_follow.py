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
            res.append(h); return False
        return True
    u32.EnumChildWindows(parent, EP(cb), 0)
    return res[0] if res else None

def click(h, wait=0.6):
    u32.SendMessageW(h, 0x00F5, 0, 0); time.sleep(wait)

def child_text(hwnd, cid):
    h = u32.GetDlgItem(hwnd, cid)
    return text_of(h) if h else None

root = r"C:\Users\54881\WorkBuddy\2026-10-20-19-29-44\Teleprompter"
env = os.environ.copy()
env["TP_SPEECH_WAV"] = os.path.join(root, "build", "tts_long.wav")
env["TP_CAPTURE_ALLOWED"] = "1"

p = subprocess.Popen([os.path.join(root, "build", "TeacherPrompter.exe")], env=env)
time.sleep(3)
hwnd = u32.FindWindowW(None, "芸豆提词器 · 教学模式")
click(find_child(hwnd, "示例文稿"))
click(find_child(hwnd, "开始跟读"))

for i in range(12):
    time.sleep(1)
    print(f"t+{i+1}s status = {child_text(hwnd, 1012)}")
    print(f"        heard  = {child_text(hwnd, 1013)}")

from PIL import ImageGrab
u32.SetForegroundWindow(hwnd)
time.sleep(0.8)
ImageGrab.grab().save(os.path.join(root, "build", "shot_follow.png"))
print("alive:", p.poll() is None)
