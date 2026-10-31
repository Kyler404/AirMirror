#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_render.py — 一次性数值验证（替代人眼截图调试）

对着正在运行的覆盖窗口抓一帧（GDI，DWM 合成后），用清单里的几何
数据做结构化数值断言：
  1. 孔内有画面（灰阶演示内容，非纯黑/非壁纸）
  2. 边框带（孔沿→机身沿）是黑色边框，无亮色壁纸泄漏（缝隙检测）
  3. 窗口四角与旁边桌面同源（透明窗口生效，非黑底）
全部通过 → PASS。用法：先让 app 处于 H 覆盖态，然后
  python tools/verify_render.py
"""
import ctypes
import json
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "frame_manifest.json"
DEMO_ASSET = "iPhone 17 Pro Max - Cosmic Orange - Portrait"

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32


def find_pid():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq airplay-windows.exe"],
                         capture_output=True, text=True, encoding="gbk", errors="replace").stdout
    for line in out.splitlines():
        if line.lower().startswith("airplay-windows.exe"):
            return int(line.split()[1])
    return None


def find_overlay():
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    return user32.FindWindowW(None, "airplay-overlay")


class R(ctypes.Structure):
    _fields_ = [("L", ctypes.c_long), ("T", ctypes.c_long),
                ("R", ctypes.c_long), ("B", ctypes.c_long)]


def capture(x, y, w, h):
    sdc = user32.GetDC(0)
    mdc = gdi32.CreateCompatibleDC(sdc)
    bmp = gdi32.CreateCompatibleBitmap(sdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    gdi32.BitBlt(mdc, 0, 0, w, h, sdc, x, y, 0x00CC0020)
    bits = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetBitmapBits(bmp, w * h * 4, bits)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(0, sdc)
    return np.array(Image.frombuffer("RGBX", (w, h), bits.raw, "raw", "RGBX", 0, 1)
                    .convert("RGB"), dtype=np.int16)


def luma(px):
    return 0.299 * px[:, :, 0] + 0.587 * px[:, :, 1] + 0.114 * px[:, :, 2]


def main():
    hwnd = find_overlay()
    if not hwnd:
        print("FAIL: overlay window not found (先按 H 进入覆盖模式)")
        return 1
    r = R()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.R - r.L, r.B - r.T
    print(f"overlay {w}x{h} @ {r.L},{r.T}")
    img = capture(r.L, r.T, w, h)
    lum = luma(img)

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    m = manifest[DEMO_ASSET]
    W, H = 1470, 3000                        # 该素材原图尺寸
    sc = w / W
    sx, sy, sw, sh = m["screen"]
    bx, by, bw, bh = m["body"]
    # 窗口铺满整图 → 各区域按比例映射
    hole = (int(sx * sc), int(sy * sc), int(sw * sc), int(sh * sc))
    body = (int(bx * sc), int(by * sc), int(bw * sc), int(bh * sc))

    ok = True

    def check(name, cond, detail):
        nonlocal ok
        print(f"  [{'PASS' if cond else 'FAIL'}] {name}: {detail}")
        ok = ok and cond

    # 1. 孔内有画面
    inner = lum[hole[1] + hole[3] // 4: hole[1] + hole[3] * 3 // 4,
                hole[0] + hole[2] // 4: hole[0] + hole[2] * 3 // 4]
    check("孔内有画面", 30 < inner.mean() < 235, f"mean luma={inner.mean():.0f}")

    # 2. 边框带黑色、无亮色泄漏（上/下/左/右四条中段）
    for name, (x0, y0, x1, y1) in {
        "上边框": (hole[0] + hole[2] // 3, body[1] + 2, hole[0] + hole[2] * 2 // 3, hole[1] - 2),
        "下边框": (hole[0] + hole[2] // 3, hole[1] + hole[3] + 2, hole[0] + hole[2] * 2 // 3, body[1] + body[3] - 2),
        "左边框": (body[0] + 2, hole[1] + hole[3] // 3, hole[0] - 2, hole[1] + hole[3] * 2 // 3),
        "右边框": (hole[0] + hole[2] + 2, hole[1] + hole[3] // 3, body[0] + body[2] - 2, hole[1] + hole[3] * 2 // 3),
    }.items():
        x0, x1 = sorted((max(0, x0), min(w, x1)))
        y0, y1 = sorted((max(0, y0), min(h, y1)))
        band = lum[y0:y1, x0:x1]
        if band.size == 0:
            check(name, False, "空区域")
            continue
        bright = float((band > 150).mean())
        check(f"{name}无泄漏", band.mean() < 60 and bright < 0.02,
              f"mean={band.mean():.0f} 亮像素比={bright:.3f}")

    # 3. 窗口四角 ≈ 旁边桌面（透明生效）
    strip = capture(max(0, r.L - 80), r.T + h // 3, 60, 40)
    corners = [(2, 2), (w - 12, 2), (2, h - 12), (w - 12, h - 12)]
    diffs = [float(np.abs(img[y:y + 10, x:x + 10].mean(axis=(0, 1)) - strip.mean(axis=(0, 1))).mean())
             for x, y in corners]
    check("四角透明", max(diffs) < 60, f"四角与桌面平均色差={diffs}")

    print("OVERALL:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
