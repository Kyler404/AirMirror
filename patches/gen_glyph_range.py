#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_glyph_range.py — 按界面实际用到的字生成 ImGui 字形范围

背景：GetGlyphRangesChineseSimplifiedCommon() 只有 ~2500 常用字，
     像「屏」这种界面必用字也可能不在表里，结果渲染成问号/方块。
     这里直接扫描 client-src/src 下所有 .cpp/.h（含注释，宁多勿缺），
     再并入 localize.py 词条与 EXTRA_TEXT，生成精确字形表。
     以后新增中文文案无需手工维护字形表，重跑本脚本即可。

产出：client-src/src/video/zh_glyph_ranges.h
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent                       # mirror-launcher/
SRC = ROOT / "client-src" / "src"
HEADER = SRC / "video" / "zh_glyph_ranges.h"

sys.path.insert(0, str(HERE))
try:
    from localize import MAP  # noqa: E402
except Exception:
    MAP = {}

# 除词条之外，可能由代码拼接出现的字（历史保留，源码扫描为主）
EXTRA_TEXT = (
    "等待连接中已断开设备镜像屏幕主页音乐设置高级网络录制停止开始"
    "接收端解析度分辨率帧率旋转横竖方诊断信息快捷键帮助关闭保存应用名称"
    "允许访问本地局域网云端画面弹出收回窗口最小化全屏退出录制中秒分小时"
    "未就绪空闲播放暂停上一首下一首音量静音版本关于检查恢复默认值"
    "投屏电脑手机管理员授权放行防火墙策略规则状态确认无法读取提示条按钮"
    "键稳空符号转换自动手动跟随顺时针方形上限全高清超清体积兼性好更小"
)

PUNCT = "，。：；！？、（）“”‘’—…·《》「」×°"


def collect():
    chars = set()
    for zh in MAP.values():
        chars.update(ch for ch in zh if ord(ch) > 0x7F)
    chars.update(ch for ch in EXTRA_TEXT if ord(ch) > 0x7F)
    chars.update(ch for ch in PUNCT if ord(ch) > 0x7F)
    # 扫描全部源码：字符串字面量与注释里的中文一并烘进字形表
    for f in SRC.rglob("*"):
        if f.suffix not in (".cpp", ".h") or f.name == HEADER.name:
            continue
        try:
            text = f.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        chars.update(ch for ch in text if ord(ch) > 0x7F)
    return sorted(ord(c) for c in chars)


def to_ranges(cps):
    """把码点列表压成 ImGui 需要的 [起, 止] 对（连续码点合并）"""
    out = []
    i = 0
    while i < len(cps):
        start = cps[i]
        end = start
        while i + 1 < len(cps) and cps[i + 1] == end + 1:
            i += 1
            end = cps[i]
        out.append((start, end))
        i += 1
    return out


def emit(cps):
    pairs = to_ranges(cps)
    lines = [
        "// 自动生成（patches/gen_glyph_range.py），勿手改。",
        "// 扫描 client-src/src 全部源码中的非 ASCII 字符生成。",
        "#pragma once",
        "#include \"imgui.h\"",
        "",
        "// ImWchar 范围是 [起, 止] 成对出现，0 结尾。",
        "// 前半段是 ASCII/标点，后面是界面实际用到的汉字（已按连续合并）",
        "static const ImWchar zh_glyph_ranges[] = {",
        "    0x0020, 0x00FF,",
        "    0x2000, 0x206F,",
        "    0x3000, 0x303F,",
        "    0xFF00, 0xFFEF,",
    ]
    for s, e in pairs:
        if s == e:
            lines.append("    0x%04X, 0x%04X," % (s, s))
        else:
            lines.append("    0x%04X, 0x%04X,   // %d 个连续字" % (s, e, e - s + 1))
    lines += ["    0", "};", ""]
    return "\n".join(lines)


def main():
    cps = collect()
    HEADER.write_text(emit(cps), encoding="utf-8")
    print("生成 %s：%d 个字形码点" % (HEADER, len(cps)))


if __name__ == "__main__":
    main()
