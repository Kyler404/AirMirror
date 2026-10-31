"""验证窗口联动：启动后隐藏 → 有连接就弹出 → 断开自动收回"""
import json
import socket
import sys
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import win_control as wc


def api(path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request("http://127.0.0.1:8770" + path, data=data,
                                 headers={"Content-Type": "application/json"},
                                 method="POST" if data is not None else "GET")
    with urllib.request.urlopen(req, timeout=25) as r:
        return json.loads(r.read())


def visible():
    hwnd, _ = wc.find_window("AirPlay-Windows", visible_only=False)
    if not hwnd:
        return None
    return wc.is_visible(hwnd)


print("1) 启动接收端:", api("/api/mirror/start", {}))
time.sleep(9)
print("2) 启动后窗口可见 =", visible(), "(期望 False)")

sock = socket.create_connection(("127.0.0.1", 7000), timeout=5)
time.sleep(3)
print("3) 有连接时可见 =", visible(), "(期望 True)")

sock.close()
time.sleep(4)
print("4) 断开后可见 =", visible(), "(期望 False)")
