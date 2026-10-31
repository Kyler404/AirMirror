#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""frame_compositor.py — 边框合成离线预览器（毫秒级迭代，无需编译）

把「边框 + 孔形掩码 + 演示画面 + 壁纸」按运行时完全相同的几何离线
合成，用来在改任何几何参数前先在 Python 里验证效果。几何数据与
运行时同源（frame_manifest.json + 同样的洪泛填充）。

用法：
  python tools/frame_compositor.py                       # 默认预览
  python tools/frame_compositor.py --asset "iPhone 17 Pro Max - Cosmic Orange - Portrait"
  python tools/frame_compositor.py --size 500 1020 --out shots/preview.png
  python tools/frame_compositor.py --emit-masks          # 生成全部掩码 PNG
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "client-src" / "src" / "assets" / "frame"
MANIFEST = ROOT / "frame_manifest.json"


def load_manifest():
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def hole_mask(im: Image.Image) -> np.ndarray:
    """与运行时相同的洪泛孔形掩码（True=孔内）"""
    a = np.array(im)[:, :, 3]
    h, w = a.shape
    transparent = a < 32
    labels, _ = ndimage.label(transparent)
    center = labels[h // 2, w // 2]
    return labels == center


def compose(asset_stem: str, size=(392, 800), wallpaper=None, save=None):
    manifest = load_manifest()
    if asset_stem not in manifest:
        names = [k for k in manifest if k.endswith("Portrait")]
        asset_stem = sorted(names)[0]
    info = manifest[asset_stem]
    frame = Image.open(ASSETS / f"{asset_stem}.png").convert("RGBA")
    W, H = frame.size
    mask = hole_mask(frame)
    sx, sy, sw, sh = info["screen"]

    # 演示画面（与 --demo 相同的灰阶渐变）
    yy, xx = np.mgrid[0:sh, 0:sw]
    grad = (40 + ((xx // 14 + yy // 14) % 10) * 20).astype(np.uint8)
    video = Image.fromarray(np.dstack([grad, grad, grad, np.full_like(grad, 255)]), "RGBA")

    # 孔内画面 = 演示画面 × 孔形掩码
    m = mask[sy:sy + sh, sx:sx + sw]
    va = np.array(video)
    va[:, :, 3] = (m * 255).astype(np.uint8)
    video = Image.fromarray(va, "RGBA")

    # 壁纸底（默认用亮灰渐变模拟，肉眼即可检查透明/圆角）
    tw, th = size
    if wallpaper:
        bg = Image.open(wallpaper).convert("RGBA").resize((tw, th))
    else:
        g = np.linspace(200, 90, th, dtype=np.uint8)
        bg_arr = np.repeat(g[:, None], tw, 1)
        bg = Image.fromarray(np.dstack([bg_arr] * 3 + [np.full((th, tw), 255, np.uint8)]), "RGBA")

    # 边框缩放到目标尺寸，画面贴入孔位
    fr = frame.resize((tw, th), Image.LANCZOS)
    sc = tw / W
    hole = (int(sx * sc), int(sy * sc), int(sw * sc), int(sh * sc))
    vd = video.resize((max(1, hole[2]), max(1, hole[3])), Image.LANCZOS)
    bg.paste(vd, (hole[0], hole[1]), vd)
    out = Image.alpha_composite(bg, fr).convert("RGB")
    if save:
        Path(save).parent.mkdir(exist_ok=True, parents=True)
        out.save(save)
        print(f"preview -> {save}  hole={hole}")
    return out, hole


def emit_masks():
    manifest = load_manifest()
    n = 0
    for stem in manifest:
        im = Image.open(ASSETS / f"{stem}.png").convert("RGBA")
        m = hole_mask(im)
        sx, sy, sw, sh = manifest[stem]["screen"]
        sub = m[sy:sy + sh, sx:sx + sw]
        img = Image.fromarray(np.dstack([
            np.full(sub.shape, 255, np.uint8)] * 3 +
            [(sub * 255).astype(np.uint8)]), "RGBA")
        img.save(ASSETS / f"{stem}.mask.png")
        n += 1
    print(f"masks emitted: {n} -> {ASSETS}\\<stem>.mask.png")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--asset")
    ap.add_argument("--size", type=int, nargs=2, default=[392, 800])
    ap.add_argument("--wallpaper")
    ap.add_argument("--out", default="shots/compositor_preview.png")
    ap.add_argument("--emit-masks", action="store_true")
    a = ap.parse_args()
    if a.emit_masks:
        emit_masks()
        sys.exit(0)
    compose(a.asset, tuple(a.size), a.wallpaper, a.out)
