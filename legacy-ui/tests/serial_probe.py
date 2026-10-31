#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
serial_probe.py — 不经过浏览器，直接往 ESP32 发 HID 事件，用来台架验证固件。

用法：
    python tests/serial_probe.py COM5              # 完整演示动作
    python tests/serial_probe.py COM5 --move-only  # 只画方框
    python tests/serial_probe.py --list            # 列出串口

动作序列：指针画方框 → 左键单击 → 滚轮 → 输入 "hello"
板子连上 iPhone 时应该能看到：指针动、点开一个 app、页面滚动、文本出现。
"""
import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("缺少 pyserial：pip install pyserial")
    sys.exit(1)


def send(ser, obj):
    line = (obj if isinstance(obj, str) else __import__("json").dumps(obj, separators=(",", ":"))) + "\n"
    ser.write(line.encode())
    ser.flush()


def square(ser, step=25, rounds=2):
    """指针画一圈方框，肉眼确认移动方向对不对"""
    edges = [(step, 0), (0, step), (-step, 0), (0, -step)]
    for _ in range(rounds):
        for dx, dy in edges:
            send(ser, {"t": "m", "dx": dx, "dy": dy})
            time.sleep(0.35)


def click(ser, button=1):
    send(ser, {"t": "b", "b": button, "d": 1})
    time.sleep(0.06)
    send(ser, {"t": "b", "b": button, "d": 0})
    time.sleep(0.3)


def scroll(ser, amount=3):
    for _ in range(amount):
        send(ser, {"t": "w", "d": 1})
        time.sleep(0.12)
    time.sleep(0.3)


KEYMAP = {
    "h": 0x0B, "e": 0x08, "l": 0x0F, "o": 0x12,
}


def type_text(ser, text):
    for ch in text.lower():
        usage = KEYMAP.get(ch)
        if usage is None:
            continue
        send(ser, {"t": "k", "u": usage, "m": 0, "d": 1})
        time.sleep(0.05)
        send(ser, {"t": "k", "u": usage, "m": 0, "d": 0})
        time.sleep(0.08)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", help="串口，如 COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--move-only", action="store_true")
    ap.add_argument("--list", action="store_true", help="列出可用串口")
    a = ap.parse_args()

    if a.list or not a.port:
        ports = list(list_ports.comports())
        if not ports:
            print("没有发现串口。板子插上了吗？驱动装了吗？")
            return 1
        for p in ports:
            print("%-8s %s" % (p.device, p.description))
        return 0

    try:
        ser = serial.Serial(a.port, a.baud, timeout=0.2)
    except Exception as e:
        print("打不开 %s：%s" % (a.port, e))
        print("常见原因：串口被 Arduino 串口监视器 / 启动台占着，或者没装 CH340/CP210x 驱动。")
        return 1

    time.sleep(0.3)
    try:
        print("→ 画方框")
        square(ser)
        if not a.move_only:
            print("→ 单击")
            click(ser)
            print("→ 滚轮")
            scroll(ser)
            print("→ 输入 hello")
            type_text(ser, "hello")
        print("完成。若 iPhone 上没反应，先确认蓝牙里已连接 " + "iOS Bridge HID")
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
