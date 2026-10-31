#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
client.py — 桌面客户端外壳（WebView2 + 本地后端）

架构：
    client.py  ── 起一个隐藏的 server.py 子进程（本地 HTTP/SSE 后端）
               ── 开一个原生窗口，把 UI 用 WebView2 渲染出来
    视频仍然是接收端自己的窗口（双窗口方案），客户端只做控制面板。

用法：
    python client.py            正常启动
    python client.py --dev      开发模式：改 static/ 下的文件，窗口自动刷新
    python client.py --port 8771

打包成 exe 用 PyInstaller：
    pyinstaller --noconsole --onefile client.py
"""

import argparse
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STATIC = ROOT / "static"

# 控制台默认 GBK，中文日志会乱码
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


# ------------------------------------------------------------------ 后端
def port_open(port, host="127.0.0.1"):
    s = socket.socket()
    s.settimeout(0.3)
    try:
        s.connect((host, port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def ensure_backend(port):
    """没后端就起一个；已经有（比如调试时手动跑的）就直接用"""
    if port_open(port):
        return None
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    proc = subprocess.Popen(
        [sys.executable, str(ROOT / "server.py"), "--port", str(port)],
        cwd=str(ROOT),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=flags,
    )
    for _ in range(60):
        if port_open(port):
            return proc
        time.sleep(0.2)
    raise RuntimeError("后端启动超时（端口 %d）" % port)


# ------------------------------------------------------------------ 热重载
def watch_static(window, interval=0.6):
    """static/ 下任何文件变动 → 刷新窗口。后端 .py 改动需要重启客户端。"""
    def snapshot():
        snap = {}
        for f in STATIC.rglob("*"):
            if f.is_file():
                try:
                    snap[str(f)] = f.stat().st_mtime
                except OSError:
                    pass
        return snap

    last = snapshot()
    while True:
        time.sleep(interval)
        cur = snapshot()
        if cur != last:
            changed = [Path(k).name for k in cur if last.get(k) != cur[k]]
            last = cur
            print("[hot] %s 变更 → 刷新" % ", ".join(changed[:3]), flush=True)
            try:
                window.evaluate_js("location.reload()")
            except Exception as e:
                print("[hot] 刷新失败：%s" % e, flush=True)


# ------------------------------------------------------------------ 主流程
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8770)
    ap.add_argument("--dev", action="store_true", help="开发模式：改前端文件自动刷新")
    a = ap.parse_args()

    import webview

    backend = ensure_backend(a.port)
    url = "http://127.0.0.1:%d/" % a.port

    window = webview.create_window(
        "投屏客户端",
        url,
        width=1180,
        height=820,
        min_size=(960, 640),
        background_color="#0d1117",
    )

    def on_loaded():
        if a.dev:
            print("[hot] 监听 %s" % STATIC, flush=True)
            threading.Thread(target=watch_static, args=(window,), daemon=True).start()

    window.events.loaded += on_loaded

    def stop_mirror():
        """关客户端 = 结束投屏。只在后端是我们拉起来的时候做，避免误伤别人开的会话"""
        import urllib.request
        try:
            req = urllib.request.Request(
                "http://127.0.0.1:%d/api/mirror/stop" % a.port,
                data=b"{}", headers={"Content-Type": "application/json"}, method="POST")
            urllib.request.urlopen(req, timeout=3).read()
            print("[bye] 已请求停止接收端", flush=True)
        except Exception as e:
            print("[bye] 停止接收端失败：%s" % e, flush=True)

    try:
        webview.start(debug=a.dev)
    finally:
        if backend and backend.poll() is None:
            stop_mirror()
            backend.terminate()


if __name__ == "__main__":
    main()
