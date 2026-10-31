#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
localize.py — 把接收端界面文案换成中文（只改 UI 区域，不动日志/路径）

用法：
    python localize.py            # 应用（自动备份 .orig）
    python localize.py --revert   # 还原

只替换第 UI_FROM_LINE 行之后的**整段字符串字面量**，避免误伤日志与字体路径。
格式串（含 %）默认不动，避免占位符错位。
"""
import json
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent / "AirPlay-Windows" / "src" / "video" / "video_renderer.cpp"
UI_FROM_LINE = 1650

# 英文 → 中文。没在这里的保持原样。
MAP = {
    # 导航与页面
    "AirPlay Windows": "AirPlay 投屏",
    "YOUR SPACE": "你的空间",
    "Home": "主页",
    "Music": "音乐",
    "Screen mirroring": "屏幕镜像",
    "Settings": "设置",
    "Receiver settings": "接收端设置",
    "Advanced": "高级",
    "Audio": "音频",
    # 状态
    "Device connected": "设备已连接",
    "Receiver available": "接收端就绪",
    "Connected": "已连接",
    "Ready to receive": "等待接收",
    "Idle": "空闲",
    "Paused": "已暂停",
    "Waiting for AirPlay": "等待 AirPlay 连接",
    "Connected device": "已连接设备",
    "Connected · Audio": "已连接 · 音频",
    "Connected · Screen mirroring": "已连接 · 屏幕镜像",
    "Sending command...": "正在发送指令…",
    "Remote control unavailable": "远程控制不可用",
    "Command failed or unsupported": "指令失败或不受支持",
    # 按钮
    "Disconnect": "断开连接",
    "Check firewall": "检查防火墙",
    "Record": "录制",
    "Stop recording": "停止录制",
    "Start recording": "开始录制",
    "Fullscreen": "全屏",
    "Hide interface": "隐藏界面",
    "Close settings": "关闭设置",
    "Close help": "关闭帮助",
    "Apply name": "应用名称",
    "Browse...": "浏览…",
    "Custom...": "自定义…",
    "Reset all settings": "恢复默认设置",
    "Previous track": "上一首",
    "Next track": "下一首",
    "Play": "播放",
    "Pause": "暂停",
    "Rotation": "旋转",
    # 首页文案
    "Welcome home.": "欢迎回来。",
    "Your music on this PC.": "音乐从这台电脑播放。",
    "Your screen, live.": "屏幕实时投过来。",
    "Your device.": "你的设备。",
    "A little bigger.": "再大一点。",
    "Your music and your screen, right here on this PC.": "音乐和画面，都在这台电脑上。",
    "From your iPhone or iPad.": "来自你的 iPhone 或 iPad。",
    "SELECT ON YOUR DEVICE": "在你的设备上选择",
    "Same Wi-Fi network": "同一 Wi-Fi 网络",
    "Choose your mode": "选择投屏方式",
    "Select your PC": "选择这台电脑",
    "Connect your device and this PC to the same network.": "把设备和这台电脑连到同一个网络。",
    "Use AirPlay for music, or Screen Mirroring from Control Center.": "听音乐用 AirPlay，投屏幕用控制中心的「屏幕镜像」。",
    "Choose this receiver. The connection starts automatically.": "选中这个接收端，连接会自动建立。",
    # 连接帮助
    "My PC isn't showing up": "电脑不在列表里",
    "Connection help": "连接帮助",
    "Can't find your PC?": "找不到这台电脑？",
    "1. Use the same network": "1. 确认在同一网络",
    "Connect your iPhone or iPad and this PC to the same Wi-Fi network.": "让 iPhone / iPad 和这台电脑连同一个 Wi-Fi。",
    "2. Check your VPN": "2. 检查 VPN",
    "A VPN can hide local devices. Check that it allows access to your local network.": "VPN 会隐藏局域网设备，确认它允许访问本地网络。",
    "3. Check Windows Firewall": "3. 检查 Windows 防火墙",
    "If a firewall warning is shown, choose Allow on local network.": "如果弹出防火墙提示，选「允许在专用网络访问」。",
    "Then reopen AirPlay on your device.": "然后在设备上重新打开 AirPlay。",
    "Windows · Local network": "Windows · 本地网络",
    # 防火墙块
    "Allow AirPlay through Windows Firewall": "允许 AirPlay 通过 Windows 防火墙",
    "Windows Firewall blocks incoming connections": "Windows 防火墙正在拦截入站连接",
    "Firewall check unavailable": "无法检测防火墙状态",
    "Allow this app to receive connections from devices on your local network.": "允许本程序接收局域网内设备的连接。",
    "Allow on local network": "允许本地网络访问",
    "Waiting for Windows...": "等待 Windows 确认…",
    # 音乐页
    "NOW PLAYING": "正在播放",
    "Playback position (read-only)": "播放进度（只读）",
    "Playing from your device on this PC": "从你的设备播放到这台电脑",
    "no audio": "无音频",
    "playback paused": "已暂停",
    # 设置项
    "Receiver name": "接收端名称",
    "Use a name without dots or backslashes (max. 50 UTF-8 bytes).": "名称不能包含点和反斜杠（最多 50 字节）。",
    "Shown in AirPlay on your devices. Saved automatically.": "会显示在设备的 AirPlay 列表里，修改自动保存。",
    "Mirror resolution": "镜像分辨率",
    "Maximum video size. Your device chooses the actual stream resolution.": "允许的最大画面尺寸，实际分辨率由设备决定。",
    "Applies on next device connection": "下次连接设备时生效",
    "Range: 320-7680 x 240-4320": "范围：320-7680 × 240-4320",
    "Custom...": "自定义…",
    "Video decoder": "视频解码",
    "Use GPU acceleration": "启用 GPU 加速",
    "Uses your graphics card when available.": "有显卡时用显卡解码。",
    "Uses your processor to decode video.": "用处理器解码。",
    "Synchronize with display": "与显示器同步",
    "Sync presents to monitor refresh.": "按显示器刷新率呈现画面。",
    "Applies live (no reconnect needed)": "实时生效，无需重连",
    "Recording": "录制",
    "Folder": "保存目录",
    "Auto (GPU preferred)": "自动（优先 GPU）",
    "GPU only": "仅 GPU",
    "CPU only": "仅 CPU",
    "Video codec": "视频编码",
    "Encoder": "编码器",
    "Bitrate (Mbps)": "码率（Mbps）",
    "Lower bitrate = smaller file / more compression": "码率越低，文件越小、压缩越狠",
    "Settings apply to the next recording": "以下设置对下一次录制生效",
    "AAC stereo": "AAC 立体声",
    "waiting for PCM": "等待 PCM 数据",
    "MP4 video (decoded AirPlay stream)": "MP4 视频（解码后的 AirPlay 流）",
    "Show diagnostics": "显示诊断信息",
    "Auto follows the orientation sent by the device.": "自动跟随设备发送的方向。",
    "Landscape": "横屏",
    "Portrait": "竖屏",
    "Square": "方形",
    "Frame rate": "帧率",
    "Frame rate cap": "帧率上限",
    "Compression": "压缩",
    "Allow HEVC (H.265)": "允许 HEVC（H.265）",
    "The device may pick HEVC at high resolution": "高分辨率时设备可能选择 HEVC",
    "Force H.264 (more CPU, more bandwidth)": "强制 H.264（更耗 CPU 和带宽）",
    "Network": "网络",
    "Keyboard shortcuts": "快捷键",
    "F / F11 / double-click  Toggle fullscreen": "F / F11 / 双击    切换全屏",
    "H       Hide / show the interface": "H                隐藏 / 显示界面",
    "R       Start / stop recording": "R                开始 / 停止录制",
    "Esc     Exit fullscreen / show UI / quit": "Esc              退出全屏 / 显示界面 / 退出",
    "Restore all app preferences to their defaults.": "把所有偏好设置恢复为默认值。",
    "Default settings restored.": "已恢复默认设置。",
    "GStreamer": "GStreamer",
    "libavformat": "libavformat",
    "Streaming": "串流",
    "mDNS   advertising": "mDNS   广播中",
}


def apply(revert=False):
    text = SRC.read_text(encoding="utf-8", errors="surrogateescape")
    if revert:
        bak = SRC.with_suffix(".cpp.orig")
        if not bak.exists():
            print("没有备份文件，无法还原")
            return 1
        SRC.write_text(bak.read_text(encoding="utf-8", errors="surrogateescape"),
                       encoding="utf-8", errors="surrogateescape")
        print("已还原")
        return 0

    bak = SRC.with_suffix(".cpp.orig")
    if not bak.exists():
        bak.write_text(text, encoding="utf-8", errors="surrogateescape")
        print("已备份 →", bak.name)

    lines = text.splitlines(keepends=True)
    hits = 0
    for idx, line in enumerate(lines):
        if idx + 1 < UI_FROM_LINE:
            continue
        for en, zh in MAP.items():
            lit = '"%s"' % en
            if lit in line:
                line = line.replace(lit, '"%s"' % zh)
                hits += 1
        lines[idx] = line

    SRC.write_text("".join(lines), encoding="utf-8", errors="surrogateescape")
    print("替换 %d 处，共 %d 条词条" % (hits, len(MAP)))
    return 0


if __name__ == "__main__":
    sys.exit(apply("--revert" in sys.argv))
