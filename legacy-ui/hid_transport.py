#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
hid_transport.py — HID 传输抽象层

设计要点：
  Web UI 只产生「语义事件」（鼠标相对位移 / 按键 / 滚轮），不关心底层怎么送到 iPhone。
  传输层可插拔：
    mock    —— 只写日志，用于无硬件调试（默认）
    serial  —— 串口 JSON Lines，对接 ESP32 BLE HID 桥
    file    —— 写到文件，用于回放/单测
  以后接 Windows BLE GATT Server，只需再加一个 transport 类，上层不动。

事件格式（也是串口协议，一行一条 JSON）：
  {"t":"m","dx":3,"dy":-2}              鼠标相对移动
  {"t":"b","b":1,"d":1}                 按键：b=1 左键 2 右键 4 中键，d=1 按下 0 抬起
  {"t":"w","d":1}                       滚轮（正=下滚）
  {"t":"k","u":4,"m":0,"d":1}           键盘：u=HID usage，m=修饰位，d=按下/抬起
  {"t":"p","on":1}                      指针可见性（部分实现用于点亮 iOS 指针）
"""
import json
import os
import threading
import time

# ---------------------------------------------------------------- HID usage 表
# US 布局，够用即止；需要别的键自己加
KEYMAP = {
    "KeyA": 0x04, "KeyB": 0x05, "KeyC": 0x06, "KeyD": 0x07, "KeyE": 0x08,
    "KeyF": 0x09, "KeyG": 0x0A, "KeyH": 0x0B, "KeyI": 0x0C, "KeyJ": 0x0D,
    "KeyK": 0x0E, "KeyL": 0x0F, "KeyM": 0x10, "KeyN": 0x11, "KeyO": 0x12,
    "KeyP": 0x13, "KeyQ": 0x14, "KeyR": 0x15, "KeyS": 0x16, "KeyT": 0x17,
    "KeyU": 0x18, "KeyV": 0x19, "KeyW": 0x1A, "KeyX": 0x1B, "KeyY": 0x1C,
    "KeyZ": 0x1D,
    "Digit1": 0x1E, "Digit2": 0x1F, "Digit3": 0x20, "Digit4": 0x21,
    "Digit5": 0x22, "Digit6": 0x23, "Digit7": 0x24, "Digit8": 0x25,
    "Digit9": 0x26, "Digit0": 0x27,
    "Enter": 0x28, "Escape": 0x29, "Backspace": 0x2A, "Tab": 0x2B, "Space": 0x2C,
    "Minus": 0x2D, "Equal": 0x2E, "BracketLeft": 0x2F, "BracketRight": 0x30,
    "Backslash": 0x31, "Semicolon": 0x33, "Quote": 0x34, "Backquote": 0x35,
    "Comma": 0x36, "Period": 0x37, "Slash": 0x38, "CapsLock": 0x39,
    "F1": 0x3A, "F2": 0x3B, "F3": 0x3C, "F4": 0x3D, "F5": 0x3E, "F6": 0x3F,
    "F7": 0x40, "F8": 0x41, "F9": 0x42, "F10": 0x43, "F11": 0x44, "F12": 0x45,
    "ArrowRight": 0x4F, "ArrowLeft": 0x50, "ArrowDown": 0x51, "ArrowUp": 0x52,
    "Delete": 0x4C, "Home": 0x4A, "End": 0x4D, "PageUp": 0x4B, "PageDown": 0x4E,
}

MOD_BITS = {"Control": 0x01, "Shift": 0x02, "Alt": 0x04, "Meta": 0x08}


class BaseTransport:
    name = "base"

    def send(self, event: dict):
        raise NotImplementedError

    def close(self):
        pass

    def describe(self) -> dict:
        return {"name": self.name}


class MockTransport(BaseTransport):
    """无硬件调试用：事件只进日志，UI 上能看到链路是否通"""
    name = "mock"

    def __init__(self, log):
        self._log = log
        self._n = 0

    def send(self, event):
        self._n += 1
        # 全量打会淹掉日志，只打前 200 条和每 100 条
        if self._n <= 200 or self._n % 100 == 0:
            self._log("HID[mock] #%d %s" % (self._n, json.dumps(event, ensure_ascii=False)))

    def describe(self):
        return {"name": self.name, "events": self._n}


class SerialTransport(BaseTransport):
    """
    串口 JSON Lines，对接 ESP32 BLE HID 桥。
    Windows 下 COM5 这类名字直接用；需要 pyserial。
    """
    name = "serial"

    def __init__(self, port, baud, log):
        import serial  # 延迟导入，没有硬件时不拖累启动
        self._log = log
        self._ser = serial.Serial(port, baud, timeout=0.05)
        time.sleep(0.2)
        self._lock = threading.Lock()
        self._port = port
        log("HID[serial] 已连接 %s @ %d" % (port, baud))

    def send(self, event):
        line = (json.dumps(event, separators=(",", ":")) + "\n").encode()
        with self._lock:
            self._ser.write(line)

    def close(self):
        try:
            self._ser.close()
        except Exception:
            pass

    def describe(self):
        return {"name": self.name, "port": self._port}


class FileTransport(BaseTransport):
    """写文件，便于回放 / 抓事件做断言"""
    name = "file"

    def __init__(self, path, log):
        self._path = path
        self._f = open(path, "a", encoding="utf-8")
        log("HID[file] 写入 %s" % path)

    def send(self, event):
        self._f.write(json.dumps(event, ensure_ascii=False) + "\n")
        self._f.flush()

    def close(self):
        try:
            self._f.close()
        except Exception:
            pass

    def describe(self):
        return {"name": self.name, "path": self._path}


def make_transport(cfg: dict, log):
    """按配置造传输层；失败时退化为 mock，保证服务不挂"""
    mode = (cfg or {}).get("mode", "mock")
    try:
        if mode == "serial":
            return SerialTransport(cfg.get("port", "COM3"), int(cfg.get("baud", 115200)), log)
        if mode == "file":
            return FileTransport(cfg.get("path", "hid_events.jsonl"), log)
    except Exception as e:
        log("HID 传输层初始化失败（%s），退化为 mock：%s" % (mode, e))
    return MockTransport(log)


# ------------------------------------------------------- 高层语义 → HID 事件
def normalize_input(payload: dict) -> list:
    """
    把前端的一条语义输入翻译成若干 HID 事件。
    前端格式：
      {"type":"move","dx":3.2,"dy":-1}
      {"type":"button","button":"left","down":true}
      {"type":"click","button":"right"}
      {"type":"scroll","dy":-1}
      {"type":"key","code":"KeyA","down":true,"mods":["Shift"]}
      {"type":"text","text":"hello"}   -> 自动展开为按下/抬起序列
    """
    t = payload.get("type")
    out = []

    if t == "move":
        dx = int(round(float(payload.get("dx", 0))))
        dy = int(round(float(payload.get("dy", 0))))
        if dx or dy:
            out.append({"t": "m", "dx": max(-127, min(127, dx)), "dy": max(-127, min(127, dy))})

    elif t == "button":
        bit = {"left": 1, "right": 2, "middle": 4}.get(payload.get("button", "left"), 1)
        out.append({"t": "b", "b": bit, "d": 1 if payload.get("down") else 0})

    elif t == "click":
        bit = {"left": 1, "right": 2, "middle": 4}.get(payload.get("button", "left"), 1)
        out.append({"t": "b", "b": bit, "d": 1})
        out.append({"t": "b", "b": bit, "d": 0})

    elif t == "scroll":
        d = int(payload.get("dy", 0))
        if d:
            out.append({"t": "w", "d": max(-127, min(127, d))})

    elif t == "key":
        code = payload.get("code", "")
        usage = KEYMAP.get(code)
        if usage is None:
            return [{"t": "_unmapped", "code": code}]
        mods = 0
        for m in payload.get("mods") or []:
            mods |= MOD_BITS.get(m, 0)
        out.append({"t": "k", "u": usage, "m": mods, "d": 1 if payload.get("down") else 0})

    elif t == "text":
        for ch in payload.get("text", ""):
            usage, shift = _char_to_usage(ch)
            if usage is None:
                continue
            out.append({"t": "k", "u": usage, "m": 0x02 if shift else 0, "d": 1})
            out.append({"t": "k", "u": usage, "m": 0x02 if shift else 0, "d": 0})

    return out


_CHAR_SHIFT = {
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7",
    "*": "8", "(": "9", ")": "0", "_": "-", "+": "=", "{": "[", "}": "]",
    "|": "\\", ":": ";", '"': "'", "<": ",", ">": ".", "?": "/", "~": "`",
}
_CHAR_BASE = {
    "-": "Minus", "=": "Equal", "[": "BracketLeft", "]": "BracketRight",
    "\\": "Backslash", ";": "Semicolon", "'": "Quote", "`": "Backquote",
    ",": "Comma", ".": "Period", "/": "Slash", " ": "Space", "\n": "Enter",
}


def _char_to_usage(ch):
    """返回 (usage, 是否需要 Shift)"""
    if ch.isalpha() and ch.isascii():
        return KEYMAP["Key" + ch.upper()], ch.isupper()
    if ch.isdigit():
        return KEYMAP["Digit" + ch], False
    if ch in _CHAR_SHIFT:
        return KEYMAP[_CHAR_BASE.get(_CHAR_SHIFT[ch], "Slash")] if _CHAR_SHIFT[ch] in _CHAR_BASE \
            else KEYMAP["Digit" + _CHAR_SHIFT[ch]], True
    if ch in _CHAR_BASE:
        return KEYMAP[_CHAR_BASE[ch]], False
    return None, False
