#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""最小分层窗口 ULW 对照实验：自建窗口上 ULW 是否工作。"""
import ctypes
import time

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
user32.DefWindowProcW.restype = ctypes.c_longlong
user32.DefWindowProcW.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                  ctypes.c_size_t, ctypes.c_longlong]

WC = "UlwTestWnd"
hinst = ctypes.windll.kernel32.GetModuleHandleW(None)

WNDPROC = ctypes.WINFUNCTYPE(
    ctypes.c_longlong, ctypes.c_void_p, ctypes.c_uint,
    ctypes.c_size_t, ctypes.c_longlong)


class WNDCLASS(ctypes.Structure):
    _fields_ = [("style", ctypes.c_uint32), ("lpfnWndProc", ctypes.c_void_p),
                ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                ("hInstance", ctypes.c_void_p), ("hIcon", ctypes.c_void_p),
                ("hCursor", ctypes.c_void_p), ("hbrBackground", ctypes.c_void_p),
                ("lpszMenuName", ctypes.c_wchar_p), ("lpszClassName", ctypes.c_wchar_p)]


@WNDPROC
def wndproc(hwnd, msg, wp, lp):
    return user32.DefWindowProcW(hwnd, msg, wp, lp)


wc = WNDCLASS()
wc.style = 3
wc.lpfnWndProc = ctypes.cast(wndproc, ctypes.c_void_p)
wc.hInstance = hinst
wc.lpszClassName = WC
assert user32.RegisterClassW(ctypes.byref(wc))

hwnd = user32.CreateWindowExW(
    0x80000, WC, "ulwtest", 0x80000000 | 0x10000000,
    200, 200, 392, 800, None, None, hinst, None)
print("own window:", hwnd)


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


w, h = 392, 800
bi = BMPV5()
bi.bV5Size = ctypes.sizeof(BMPV5)
bi.bV5Width, bi.bV5Height = w, -h
bi.bV5Planes, bi.bV5BitCount = 1, 32
bi.bV5Compression = 3
bi.bV5RedMask, bi.bV5GreenMask = 0xFF0000, 0xFF00
bi.bV5BlueMask, bi.bV5AlphaMask = 0xFF, 0xFF000000
bits = ctypes.c_void_p()
hdc = user32.GetDC(0)
hbmp = gdi32.CreateDIBSection(hdc, ctypes.byref(bi), 0, ctypes.byref(bits), None, 0)
print("DIB:", hbmp)
buf = (ctypes.c_ubyte * (w * h * 4)).from_address(bits.value)
for i in range(w * h):
    buf[i * 4 + 0] = 0
    buf[i * 4 + 1] = 0
    buf[i * 4 + 2] = 200
    buf[i * 4 + 3] = 255
memdc = gdi32.CreateCompatibleDC(hdc)
gdi32.SelectObject(memdc, hbmp)


class S(ctypes.Structure):
    _fields_ = [("cx", ctypes.c_long), ("cy", ctypes.c_long)]


class P(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


psz, pss = S(392, 800), P(0, 0)
rc = user32.UpdateLayeredWindow(hwnd, hdc, None, ctypes.byref(psz),
                                memdc, ctypes.byref(pss), 0, None, 2)
print("own-window ULW rc=%d gle=%d" % (rc, ctypes.GetLastError()))
time.sleep(2)
user32.DestroyWindow(hwnd)
