#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_frame_manifest.py — 构建期精确测量每张边框素材的「屏幕内容区」

原理（替代运行时的一切猜测）：
  1. 机身包围盒 = 所有不透明像素（alpha ≥ 250）的范围。
  2. 屏幕内容区 = 从图像中心做连通域分析（洪泛填充）：与中心相通的
     透明像素集合 = 屏幕开口（被不透明机身封闭，与机身外的透明背景
     天然隔离，灵动岛/圆角/侧键都不会干扰测量）。
  3. 内容区矩形 = 该连通域的外接矩形。画面 cover 填充它，整张边框
     叠在上层 —— 边框自带的黑边、圆角、灵动岛自然遮盖画面方角。

产出：client-src/src/video/frame_manifest.h（运行时按文件名查表）
      frame_manifest.json（留档/工具用）
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
ASSETS = ROOT / "client-src" / "src" / "assets" / "frame"
HEADER = ROOT / "client-src" / "src" / "video" / "frame_manifest.h"
JSON_OUT = ROOT / "frame_manifest.json"


def measure(path: Path):
    im = Image.open(path).convert("RGBA")
    arr = np.array(im)
    alpha = arr[:, :, 3]
    h, w = alpha.shape

    # 1) 机身包围盒（不透明）
    opaque = alpha >= 250
    if not opaque.any():
        return None
    ys, xs = np.where(opaque)
    body = [int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1),
            int(ys.max() - ys.min() + 1)]

    # 2) 屏幕内容区：中心连通的透明区域
    transparent = alpha < 32
    labels, _ = ndimage.label(transparent)
    center_label = labels[h // 2, w // 2]
    if center_label == 0:
        return None                      # 中心落在机身上（异常素材）
    ys, xs = np.where(labels == center_label)
    screen = [int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1),
              int(ys.max() - ys.min() + 1)]
    return screen, body


def main():
    if not ASSETS.exists():
        print("素材目录不存在:", ASSETS)
        return 1
    entries = {}
    for png in sorted(ASSETS.glob("*.png")):
        r = measure(png)
        if r is None:
            print("跳过（无法测量）:", png.name)
            continue
        screen, body = r
        entries[png.stem] = {"screen": screen, "body": body}
        print("%-52s screen=%s body=%s" % (png.stem, screen, body))
    if not entries:
        print("没有可测量的素材")
        return 1

    JSON_OUT.write_text(json.dumps(entries, ensure_ascii=False, indent=1),
                        encoding="utf-8")

    lines = [
        "// 自动生成（patches/gen_frame_manifest.py）：每张边框素材的",
        "// 屏幕内容区 / 机身包围盒（像素，全图坐标）。运行时按文件名",
        "// 查表绘制，不再做任何像素猜测。重新生成：python patches/gen_frame_manifest.py",
        "#pragma once",
        "#include <cstring>",
        "",
        "struct FrameScreenRect {",
        "    const char* name;",
        "    int sx, sy, sw, sh;   // 屏幕内容区（画面 cover 填充此处）",
        "};",
        "",
        "static const FrameScreenRect kFrameManifest[] = {",
    ]
    for name, v in entries.items():
        s = v["screen"]
        lines.append('    {"%s", %d, %d, %d, %d},' %
                     (name, s[0], s[1], s[2], s[3]))
    lines += [
        "};",
        "",
        "inline const FrameScreenRect* find_frame_screen(const char* stem) {",
        "    for (const auto& e : kFrameManifest)",
        "        if (std::strcmp(e.name, stem) == 0) return &e;",
        "    return nullptr;",
        "}",
        "",
    ]
    HEADER.write_text("\n".join(lines), encoding="utf-8")
    print("生成 %s（%d 项）与 %s" % (HEADER.name, len(entries), JSON_OUT.name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
