#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
win_capture.py — 抓 UxPlay 窗口做低频预览（仅 Windows，纯 ctypes，无第三方库）

用途：启动台上能看到「镜像确实起来了」的静态画面，不用 Alt+Tab 切窗口。
帧率不做高（前端 2fps 轮询），只当调试指示灯。

已知限制：
  - D3D11 硬件渲染的窗口可能抓到黑屏；遇到就改 UxPlay 的 -vs 为软件 sink
    （如 `-vs autovideosink` 或 `d3d11videosink` 视构建而定）
  - 最小化窗口抓不到内容（PrintWindow 对最小化窗口无解）
"""
import ctypes
import struct
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

PW_RENDERFULLCONTENT = 0x00000002


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def find_window(title_substr):
    """按标题子串找第一个可见窗口，返回 (hwnd, title)"""
    hits = []

    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
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


def grab_bmp(title_substr="UxPlay", max_width=640):
    """抓窗口客户区，返回 BMP 字节；失败返回 None"""
    hwnd, _title = find_window(title_substr)
    if not hwnd:
        return None

    rect = wintypes.RECT()
    if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
        return None
    w = rect.right - rect.left
    h = rect.bottom - rect.top
    if w <= 0 or h <= 0:
        return None

    hdc_win = user32.GetDC(hwnd)
    if not hdc_win:
        return None
    hdc_mem = gdi32.CreateCompatibleDC(hdc_win)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_win, w, h)
    old = gdi32.SelectObject(hdc_mem, hbmp)

    try:
        user32.PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT)

        bi = BITMAPINFOHEADER()
        bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bi.biWidth = w
        bi.biHeight = h          # 正数 = 自下而上，正好是 BMP 的存储顺序
        bi.biPlanes = 1
        bi.biBitCount = 32
        bi.biCompression = 0     # BI_RGB
        bi.biSizeImage = w * h * 4

        buf = ctypes.create_string_buffer(w * h * 4)
        got = gdi32.GetDIBits(hdc_mem, hbmp, 0, h, buf, ctypes.byref(bi), 0)
        if got == 0:
            return None

        pixels = buf.raw
        header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
        info = struct.pack(
            "<IiiHHIIiiII", 40, w, h, 1, 32, 0, len(pixels), 0, 0, 0, 0
        )
        return header + info + pixels
    finally:
        gdi32.SelectObject(hdc_mem, old)
        gdi32.DeleteObject(hbmp)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(hwnd, hdc_win)


if __name__ == "__main__":
    import sys
    data = grab_bmp(sys.argv[1] if len(sys.argv) > 1 else "UxPlay")
    if not data:
        print("没抓到窗口")
    else:
        out = "preview.bmp"
        open(out, "wb").write(data)
        print("已保存", out, len(data), "字节")
