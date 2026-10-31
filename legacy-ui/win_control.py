#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
win_control.py — 对接收端窗口做最小化的原生操作

用途：接收端自带一套英文 ImGui 界面（Home / Music / Screen Mirroring / Settings）。
     按 H 可以切到「Hide UI」= 无边框纯画面。这里把这件事自动化，
     让客户端在启动后自动把接收端切成纯视频窗口。

只做三件事：
  find_window(title)      按标题子串找顶层窗口
  send_char(hwnd, ch)     往窗口投一个按键（PostMessage，不抢焦点）
  window_info(hwnd)       读窗口样式/尺寸，用来判断"是否已进入无边框模式"
"""
import ctypes
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)

WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CHAR = 0x0102

GWL_STYLE = -16
WS_CAPTION = 0x00C00000
WS_THICKFRAME = 0x00040000
WS_BORDER = 0x00800000

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def find_window(title_substr, visible_only=True):
    hits = []

    def cb(hwnd, _):
        if visible_only and not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n <= 0:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        if title_substr.lower() in buf.value.lower():
            hits.append((hwnd, buf.value))
            return False
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return hits[0] if hits else (None, None)


def send_char(hwnd, ch):
    """PostMessage 投一个按键。SDL2 从 lParam 的 scancode 推导键值，所以必须填对。"""
    vk = ord(ch.upper())
    # 常用字符的扫描码（只用得到很少几个）
    scancode = {"H": 0x23, "F": 0x21, "R": 0x13, "ESC": 0x01}.get(
        "ESC" if ch == "\x1b" else ch.upper(), 0)
    down = 1 | (scancode << 16)
    up = 1 | (scancode << 16) | (1 << 30) | (1 << 31)
    user32.PostMessageW(hwnd, WM_KEYDOWN, vk, down)
    user32.PostMessageW(hwnd, WM_CHAR, ord(ch), down)
    user32.PostMessageW(hwnd, WM_KEYUP, vk, up)


def window_info(hwnd):
    style = user32.GetWindowLongW(hwnd, GWL_STYLE)
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return {
        "has_caption": bool(style & WS_CAPTION),
        "resizable": bool(style & WS_THICKFRAME),
        "width": r.right - r.left,
        "height": r.bottom - r.top,
        "style": hex(style & 0xFFFFFFFF),
    }


def set_pure_video(title="AirPlay-Windows", timeout=8.0):
    """
    切到无边框纯画面。返回 dict(ok, before, after)。
    已经处于无边框状态就直接返回，不重复发按键。
    """
    deadline = time.time() + timeout
    hwnd, name = None, None
    while time.time() < deadline:
        hwnd, name = find_window(title)
        if hwnd:
            break
        time.sleep(0.4)
    if not hwnd:
        return {"ok": False, "error": "找不到接收端窗口（标题含 %r）" % title}

    before = window_info(hwnd)
    if not before["has_caption"]:
        return {"ok": True, "already": True, "before": before, "after": before, "hwnd": hwnd}

    send_char(hwnd, "H")
    time.sleep(1.2)
    after = window_info(hwnd)
    return {
        "ok": not after["has_caption"],
        "before": before,
        "after": after,
        "hwnd": hwnd,
        "title": name,
    }


def toggle_ui(title="AirPlay-Windows"):
    hwnd, _ = find_window(title)
    if not hwnd:
        return {"ok": False, "error": "找不到接收端窗口"}
    send_char(hwnd, "H")
    time.sleep(1.0)
    return {"ok": True, "after": window_info(hwnd)}


# ------------------------------------------------------------------ 显示/隐藏
SW_HIDE = 0
SW_SHOW = 5
SW_RESTORE = 9


def is_visible(hwnd):
    return bool(user32.IsWindowVisible(hwnd))


def hide_window(hwnd):
    user32.ShowWindow(hwnd, SW_HIDE)


def show_window(hwnd, width=None, height=None, center=True):
    """把接收端窗口显示出来；给了尺寸就顺带摆到屏幕中央"""
    user32.ShowWindow(hwnd, SW_SHOW)
    if width and height:
        sw = user32.GetSystemMetrics(0)
        sh = user32.GetSystemMetrics(1)
        x = (sw - width) // 2 if center else 100
        y = (sh - height) // 2 if center else 100
        user32.SetWindowPos(hwnd, 0, x, y, width, height, 0x0040)  # SWP_SHOWWINDOW
    user32.SetForegroundWindow(hwnd)
    user32.BringWindowToTop(hwnd)


def wait_for_window(title="AirPlay-Windows", timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd, _ = find_window(title)
        if hwnd:
            return hwnd
        time.sleep(0.3)
    return None


if __name__ == "__main__":
    print(set_pure_video())
