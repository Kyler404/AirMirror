#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_fonts.py — 让 ImGui 那层能渲染中文

问题：AddFontFromFileTTF("segoeui.ttf", 18) 默认只烘焙 Latin-1，中文会变成方块。
做法：在每个字体后面用 MergeMode 合并一份微软雅黑，字形范围取中文常用字。
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent / "AirPlay-Windows" / "src" / "video" / "video_renderer.cpp"
MARK = "zh_merge"

CALL = re.compile(r'^(\s*)(.*AddFontFromFileTTF\("([^"]+)",\s*([0-9.]+f)\);)', re.M)


def main():
    text = SRC.read_text(encoding="utf-8", errors="surrogateescape")
    if MARK in text:
        print("已经打过补丁，跳过")
        return 0

    if not CALL.search(text):
        print("没找到 AddFontFromFileTTF 调用，源码结构可能变了")
        return 1

    # 1) 第一次出现之前插入一个共享的 ImFontConfig
    first = CALL.search(text)
    decl = ('%s// 中文回退：把微软雅黑并进每个字体（MergeMode 只影响紧接着添加的那个字体）\n'
            '%sstatic ImFontConfig zh_merge;\n'
            '%szh_merge.MergeMode = true;\n'
            '%szh_merge.PixelSnapH = true;\n' % (
                first.group(1), first.group(1), first.group(1), first.group(1)))
    text = text[:first.start()] + decl + text[first.start():]

    # 2) 每个字体后面追加一份合并字体
    count = [0]

    def repl(m):
        indent, stmt, path, size = m.group(1), m.group(2), m.group(3), m.group(4)
        if "msyh" in path or MARK in stmt:
            return stmt
        count[0] += 1
        return (stmt +
                '\n%sio.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", %s, &zh_merge, '
                'io.Fonts->GetGlyphRangesChineseSimplifiedCommon());' % (indent, size))

    text = CALL.sub(repl, text)
    SRC.write_text(text, encoding="utf-8", errors="surrogateescape")
    print("已为 %d 个字体加上中文回退" % count[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
