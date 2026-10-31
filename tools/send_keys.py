# Send real key input (SendInput) to the foreground airplay window:
#   tools/send_keys.py alt+h | alt+plus | alt+minus | h
import ctypes
import sys
import time

user32 = ctypes.windll.user32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))

# 先把应用窗口置前
import subprocess
from ctypes import wintypes
out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq airplay-windows.exe"],
                     capture_output=True, text=True, encoding="gbk", errors="replace").stdout
pid_ = int([l for l in out.splitlines() if l.lower().startswith("airplay-windows")][0].split()[1])
hwnd = None
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def cb(h, l):
    global hwnd
    p2 = wintypes.DWORD()
    user32.GetWindowThreadProcessId(h, ctypes.byref(p2))
    if p2.value == pid_ and user32.IsWindowVisible(h):
        buf = ctypes.create_unicode_buffer(64)
        user32.GetWindowTextW(h, buf, 64)
        if "AirPlay" in buf.value:
            hwnd = h
    return True


user32.EnumWindows(EnumProc(cb), 0)
assert hwnd, "window not found"
# 提升置前权限的经典技巧：先注入一次 ALT 键
user32.keybd_event(0x12, 0x38, 0, 0)
user32.keybd_event(0x12, 0x38, 2, 0)
time.sleep(0.15)
user32.SetForegroundWindow(hwnd)
time.sleep(0.3)


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", ctypes.c_ushort), ("wScan", ctypes.c_ushort),
                ("dwFlags", ctypes.c_ulong), ("time", ctypes.c_ulong),
                ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong))]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT)]
    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_ulong), ("u", _U)]


def send(vk, up=False):
    sc = user32.MapVirtualKeyW(vk, 0)
    flags = 0x0008  # KEYEVENTF_SCANCODE
    if up:
        flags |= 0x0002  # KEYEVENTF_KEYUP
    inp = INPUT()
    inp.type = 1
    inp.ki = KEYBDINPUT(0, sc, flags, 0, None)
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
    time.sleep(0.05)


def combo(arg):
    if arg == "alt+h":
        send(0x12); send(0x48); send(0x48, True); send(0x12, True)
    elif arg == "alt+plus":
        send(0x12); send(0xBB); send(0xBB, True); send(0x12, True)
    elif arg == "alt+minus":
        send(0x12); send(0xBD); send(0xBD, True); send(0x12, True)
    elif arg == "alt+enter":
        send(0x12); send(0x0D); send(0x0D, True); send(0x12, True)
    elif arg == "h":
        send(0x48); send(0x48, True)
    elif arg == "esc":
        send(0x1B); send(0x1B, True)
    else:
        print("unknown:", arg)


combo(sys.argv[1])
time.sleep(0.3)


class R(ctypes.Structure):
    _fields_ = [("L", ctypes.c_long), ("T", ctypes.c_long),
                ("R", ctypes.c_long), ("B", ctypes.c_long)]


r = R()
user32.GetWindowRect(hwnd, ctypes.byref(r))
print(f"{sys.argv[1]} -> window {r.R - r.L}x{r.B - r.T}")
