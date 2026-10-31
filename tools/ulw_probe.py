#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""外部 ULW 实验：对运行中的 airplay-overlay 窗口注入一张红色测试位图，
隔离判断 ULW 失败是窗口状态问题还是应用内调用问题。"""
import ctypes
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

hwnd = user32.FindWindowW(None, "airplay-overlay")
assert hwnd, "overlay not found"
style = user32.GetWindowLongW(hwnd, -16) & 0xFFFFFFFF
ex = user32.GetWindowLongW(hwnd, -20) & 0xFFFFFFFF
print(f"hwnd={hwnd}")
print(f"STYLE=0x{style:08X} THICKFRAME={bool(style & 0x40000)} POPUP={bool(style & 0x80000000)}")
print(f"EXSTYLE=0x{ex:08X} LAYERED={bool(ex & 0x80000)}")

w, h = 392, 800

class BMPV5(ctypes.Structure):
    _fields_ = [("bV5Size", ctypes.c_uint32), ("bV5Width", ctypes.c_long),
                ("bV5Height", ctypes.c_long), ("bV5Planes", ctypes.c_uint16),
                ("bV5BitCount", ctypes.c_uint16), ("bV5Compression", ctypes.c_uint32),
                ("bV5SizeImage", ctypes.c_uint32), ("bV5XPelsPerMeter", ctypes.c_long),
                ("bV5YPelsPerMeter", ctypes.c_long), ("bV5ClrUsed", ctypes.c_uint32),
                ("bV5ClrImportant", ctypes.c_uint32), ("bV5RedMask", ctypes.c_uint32),
                ("bV5GreenMask", ctypes.c_uint32), ("bV5BlueMask", ctypes.c_uint32),
                ("bV5AlphaMask", ctypes.c_uint32), ("bV5CSType", ctypes.c_uint32),
                ("bV5Endpoints", ctypes.c_ubyte * 36), ("bV5GammaRed", ctypes.c_uint32),
                ("bV5GammaGreen", ctypes.c_uint32), ("bV5GammaBlue", ctypes.c_uint32)]

bmih = BMPV5()
bmih.bV5Size = ctypes.sizeof(BMPV5)
bmih.bV5Width, bmih.bV5Height = w, -h
bmih.bV5Planes, bmih.bV5BitCount = 1, 32
bmih.bV5Compression = 3  # BI_BITFIELDS
bmih.bV5RedMask, bmih.bV5GreenMask = 0x00FF0000, 0x0000FF00
bmih.bV5BlueMask, bmih.bV5AlphaMask = 0x000000FF, 0xFF000000
bits = ctypes.c_void_p()
hdc = user32.GetDC(0)
hbmp = gdi32.CreateDIBSection(hdc, ctypes.byref(bmih), 0, ctypes.byref(bits), None, 0)
assert hbmp, "CreateDIBSection failed"
buf = (ctypes.c_ubyte * (w * h * 4)).from_address(bits.value)
for i in range(w * h):
    buf[i * 4 + 0] = 0
    buf[i * 4 + 1] = 0
    buf[i * 4 + 2] = 200
    buf[i * 4 + 3] = 255
memdc = gdi32.CreateCompatibleDC(hdc)
gdi32.SelectObject(memdc, hbmp)

class SIZE(ctypes.Structure):
    _fields_ = [("cx", ctypes.c_long), ("cy", ctypes.c_long)]

class PT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

psz, pss = SIZE(392, 800), PT(0, 0)
rc = user32.UpdateLayeredWindow(
    hwnd, hdc, None, ctypes.byref(psz), memdc, ctypes.byref(pss),
    0, None, 2)  # ULW_ALPHA = 2
print(f"external ULW rc={rc} gle={ctypes.GetLastError()}")
time.sleep(1.2)
gdi32.DeleteObject(hbmp)
gdi32.DeleteDC(memdc)
user32.ReleaseDC(0, hdc)
