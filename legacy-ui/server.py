#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
server.py — 投屏启动台后端（纯标准库，零依赖）

职责：
  1. 管 UxPlay 进程：启动 / 停止 / 参数拼装 / 输出捕获
  2. 把日志以 SSE 推给浏览器控制台
  3. 接浏览器输入（指针 / 键盘）→ 归一化成 HID 事件 → 交给传输层
  4. 提供可选的低频预览（截 UxPlay 窗口），仅用于调试确认链路

启动：python server.py            → http://127.0.0.1:8770
      python server.py --port 9000
"""
import argparse
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

import hid_transport

ROOT = Path(__file__).resolve().parent
STATIC = ROOT / "static"
CONFIG_PATH = ROOT / "config.json"
LOG_PATH = ROOT / "logs" / "server.log"

# Windows 控制台默认 GBK，中文会变乱码；同时落一份 UTF-8 日志文件，方便回看
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass
LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
_LOG_FP = open(LOG_PATH, "a", encoding="utf-8", buffering=1)

DEFAULT_CONFIG = {
    # 0.0.0.0 = 允许手机浏览器访问启动台（方便调试）；只在本机用可以改回 127.0.0.1
    "server": {"host": "0.0.0.0", "port": 8770},
    # 默认用项目内自带的原生 Windows 接收端；换 UxPlay 时改成它的 exe + 参数模板
    "uxplay": {
        "exe": "receiver/airplay-windows/airplay-windows.exe",
        "args": ["--log"],
        "name": "WebMirror",
        "fps": 60,
        "fullscreen": False,
        "video_sink": "",
        "extra_args": "",
    },
    "hid": {"mode": "mock", "port": "COM3", "baud": 115200, "path": "hid_events.jsonl"},
    # 窗口联动：接收端启动后先隐藏，手机连上才弹画面
    "window": {"auto_show": True, "title": "AirPlay-Windows", "size": "1280x720",
               "pure_video": True},
    "preview": {"enabled": True, "window_title": "AirPlay", "max_width": 640},
}

# 界面上「预设」下拉用的两套参数，避免用户手抄 flag
PRESETS = {
    "win": {
        "label": "原生接收端（AirPlay-Windows）",
        "uxplay": {
            "exe": "receiver/airplay-windows/airplay-windows.exe",
            "args": ["--log"], "fullscreen": False, "video_sink": "", "extra_args": "",
        },
        "preview_title": "AirPlay",
    },
    "uxplay": {
        "label": "UxPlay（需自备 GStreamer 构建）",
        "uxplay": {
            "exe": "uxplay", "args": ["-n", "{name}", "-nh", "-fps", "{fps}"],
            "fullscreen": False, "video_sink": "", "extra_args": "",
        },
        "preview_title": "UxPlay",
    },
}


def resolve_exe(exe: str) -> str:
    """相对路径按项目根目录解析，避免依赖启动时的工作目录"""
    if not exe:
        return exe
    p = Path(exe)
    if p.is_absolute():
        return str(p)
    cand = ROOT / exe
    return str(cand) if cand.exists() else exe


def _port_states(port=7000):
    """返回该本地端口上出现过的连接状态集合（ESTABLISHED / LISTENING / ...）"""
    try:
        raw = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, timeout=5,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)).stdout
        out = raw.decode("oem", errors="replace")
    except Exception:
        return set()
    states = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 5 and parts[1].endswith(":" + str(port)):
            states.add(parts[3].upper())
    return states


# ------------------------------------------------------------------ 日志中枢
class LogHub:
    def __init__(self, cap=800):
        self._buf = []
        self._subs = set()
        self._lock = threading.Lock()
        self._cap = cap
        # 连打的同一条日志（比如每秒一次的错误告警）折叠掉，否则控制台会被刷屏
        self._last_raw = None
        self._last_cnt = 0

    def _emit(self, msg):
        line = "%s  %s" % (time.strftime("%H:%M:%S"), msg.rstrip())
        with self._lock:
            self._buf.append(line)
            if len(self._buf) > self._cap:
                del self._buf[: len(self._buf) - self._cap]
            subs = list(self._subs)
        try:
            print(line, flush=True)
        except Exception:
            pass
        try:
            _LOG_FP.write(line + "\n")
        except Exception:
            pass
        for q in subs:
            try:
                q.put_nowait(line)
            except Exception:
                pass

    def write(self, msg):
        raw = msg.rstrip()
        # 折叠时忽略行内时间戳，否则每条都被当成"不同日志"
        key = re.sub(r"\[\d{2}:\d{2}:\d{2}(\.\d+)?\]\s*", "", raw)
        with self._lock:
            if key == self._last_raw:
                self._last_cnt += 1
                return
            folded = self._last_cnt - 1 if self._last_cnt > 1 else 0
            self._last_raw = key
            self._last_cnt = 1
        if folded:
            self._emit("（上一条日志重复 %d 次，已折叠）" % folded)
        self._emit(raw)

    def tail(self, n=200):
        with self._lock:
            return self._buf[-n:]

    def subscribe(self):
        q = queue.Queue(maxsize=500)
        with self._lock:
            self._subs.add(q)
        return q

    def unsubscribe(self, q):
        with self._lock:
            self._subs.discard(q)


HUB = LogHub()


# ------------------------------------------------------------------ 配置管理
def load_config():
    cfg = json.loads(json.dumps(DEFAULT_CONFIG))
    if CONFIG_PATH.exists():
        try:
            user = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
            for k, v in user.items():
                if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                    cfg[k].update(v)
                else:
                    cfg[k] = v
        except Exception as e:
            HUB.write("配置读取失败，用默认值：%s" % e)
    return cfg


CONFIG = load_config()
CONFIG_LOCK = threading.Lock()


def save_config(new_cfg):
    global CONFIG
    with CONFIG_LOCK:
        CONFIG = new_cfg
        CONFIG_PATH.write_text(json.dumps(new_cfg, indent=2, ensure_ascii=False), encoding="utf-8")


# ------------------------------------------------------------------ UxPlay 进程
class UxPlay:
    def __init__(self):
        self.proc = None
        self.started_at = None
        self.last_error = ""
        self._lock = threading.Lock()

    # ---- 外部实例检测：端口被占但不是我们拉起来的 ----
    @staticmethod
    def _port_owner(port=7000):
        """返回监听该端口的 pid；没有则 None。用 netstat，避免依赖 psutil"""
        try:
            raw = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True,
                                 timeout=5,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)).stdout
            # netstat 输出走 OEM 代码页（中文系统是 GBK），不能按 UTF-8 解
            try:
                out = raw.decode("oem", errors="replace")
            except LookupError:
                out = raw.decode("mbcs", errors="replace")
        except Exception:
            return None
        for line in out.splitlines():
            parts = line.split()
            if len(parts) < 5 or parts[3].upper() != "LISTENING":
                continue
            if parts[1].endswith(":" + str(port)):
                try:
                    return int(parts[4])
                except ValueError:
                    return None
        return None

    def _external_pid(self):
        """我们没在管，但有进程占着 7000 → 说明是手动起的实例"""
        pid = self._port_owner(7000)
        if pid and (not self.proc or self.proc.pid != pid):
            return pid
        return None

    # ---- 窗口联动：手机连上才弹画面 ----
    def _start_window_watcher(self, cfg):
        if not cfg.get("window", {}).get("auto_show", True):
            return
        try:
            import win_control  # 仅 Windows 有意义
        except Exception as e:
            HUB.write("窗口联动不可用：%s" % e)
            return
        title = cfg.get("window", {}).get("title", "AirPlay-Windows")
        size = cfg.get("window", {}).get("size", "1280x720")
        pure = bool(cfg.get("window", {}).get("pure_video", True))
        try:
            w, h = (int(x) for x in size.lower().split("x"))
        except Exception:
            w, h = 1280, 720

        def loop():
            hwnd = win_control.wait_for_window(title, 15)
            if not hwnd:
                HUB.write("没找到接收端窗口，跳过窗口联动")
                return
            win_control.hide_window(hwnd)
            HUB.write("接收端窗口已隐藏，等待手机连接…")
            shown = False
            while True:
                time.sleep(1.0)
                # 注意 visible_only=False：窗口被我们隐藏了，但它还在
                if not win_control.find_window(title, visible_only=False)[0]:
                    HUB.write("接收端窗口已关闭，结束联动")
                    return
                busy = "ESTABLISHED" in _port_states(7000)
                if busy and not shown:
                    win_control.show_window(hwnd, w, h)
                    if pure:
                        # 接收端自带英文 ImGui 界面，按 H 切「Hide UI」只留画面
                        win_control.send_char(hwnd, "H")
                    shown = True
                    HUB.write("检测到手机连接 → 弹出画面窗口")
                elif not busy and shown:
                    win_control.hide_window(hwnd)
                    shown = False
                    HUB.write("连接断开 → 收回画面窗口")

        threading.Thread(target=loop, daemon=True).start()

    def _exe_ok(self, exe):
        exe = resolve_exe(exe)
        if os.sep in exe or "/" in exe:
            return os.path.isfile(exe)
        return shutil.which(exe) is not None

    def build_cmd(self, cfg):
        u = cfg["uxplay"]
        exe = resolve_exe(u.get("exe") or "uxplay")
        tpl = u.get("args")
        if not isinstance(tpl, list):
            tpl = DEFAULT_CONFIG["uxplay"]["args"]
        sub = {"name": u.get("name", "WebMirror"), "fps": str(u.get("fps", 60))}
        args = [exe] + [str(a).format(**sub) for a in tpl]
        # 全屏 / 视频 sink 是 UxPlay 的 flag，别的接收端别乱加
        is_uxplay = "uxplay" in exe.lower()
        if is_uxplay and u.get("fullscreen"):
            args.append("-fs")
        if is_uxplay and u.get("video_sink"):
            args += ["-vs", u["video_sink"]]
        extra = (u.get("extra_args") or "").strip()
        if extra:
            args += extra.split()
        return args

    def start(self, cfg):
        with self._lock:
            if self.proc and self.proc.poll() is None:
                return {"ok": False, "error": "UxPlay 已在运行 (pid=%d)" % self.proc.pid}

            ext = self._external_pid()
            if ext:
                return {"ok": False,
                        "error": "端口 7000 已被外部实例占用 (pid=%d)。若要接管，先点「停止」结束它。" % ext}

            u = cfg["uxplay"]
            exe = u.get("exe") or "uxplay"
            if not self._exe_ok(exe):
                msg = ("找不到接收端可执行文件：%s。在页面上选预设，或把 exe 路径改成实际位置。" % exe)
                self.last_error = msg
                return {"ok": False, "error": msg}

            cmd = self.build_cmd(cfg)
            HUB.write("启动：%s" % " ".join(cmd))
            try:
                flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
                self.proc = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    stdin=subprocess.DEVNULL,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    bufsize=1,
                    creationflags=flags,
                )
            except Exception as e:
                self.last_error = str(e)
                HUB.write("启动失败：%s" % e)
                return {"ok": False, "error": str(e)}

            self.started_at = time.time()
            self.last_error = ""
            threading.Thread(target=self._pump, args=(self.proc,), daemon=True).start()
            self._start_window_watcher(cfg)
            HUB.write("UxPlay 已启动 pid=%d" % self.proc.pid)
            return {"ok": True, "pid": self.proc.pid}

    def _pump(self, proc):
        try:
            for line in proc.stdout:
                HUB.write("[uxplay] " + line.rstrip())
        except Exception:
            pass
        code = proc.wait()
        HUB.write("UxPlay 退出，code=%s" % code)

    def stop(self):
        with self._lock:
            if not self.proc or self.proc.poll() is not None:
                # 可能是手动双击启动的外部实例：把占着 7000 的进程收掉
                ext = self._external_pid()
                if ext:
                    HUB.write("结束外部实例 pid=%d（不是启动台拉起的）" % ext)
                    try:
                        subprocess.run(["taskkill", "/PID", str(ext), "/F"],
                                       capture_output=True, timeout=10,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                    except Exception as e:
                        HUB.write("结束外部实例失败：%s" % e)
                    self.proc = None
                    return {"ok": True, "killed_external": ext}
                self.proc = None
                return {"ok": False, "error": "UxPlay 未在运行"}
            HUB.write("停止 UxPlay pid=%d" % self.proc.pid)
            try:
                self.proc.terminate()
                for _ in range(30):
                    if self.proc.poll() is not None:
                        break
                    time.sleep(0.1)
                if self.proc.poll() is None:
                    self.proc.kill()
            except Exception as e:
                HUB.write("停止异常：%s" % e)
            self.proc = None
            return {"ok": True}

    def status(self):
        running = bool(self.proc and self.proc.poll() is None)
        connected = "ESTABLISHED" in _port_states(7000)
        if running:
            return {
                "running": True,
                "external": False,
                "connected": connected,
                "pid": self.proc.pid,
                "uptime": round(time.time() - self.started_at, 1) if self.started_at else 0,
                "last_error": self.last_error,
            }
        ext = self._external_pid()
        if ext:
            # 端口被别人占着：如实报告，别谎报"未运行"
            return {"running": True, "external": True, "connected": connected,
                    "pid": ext, "uptime": 0, "last_error": ""}
        return {"running": False, "external": False, "connected": False, "pid": None,
                "uptime": 0, "last_error": self.last_error}


UX = UxPlay()
HID = {"transport": hid_transport.make_transport(CONFIG.get("hid"), HUB.write)}


def rebuild_hid(cfg=None):
    try:
        HID["transport"].close()
    except Exception:
        pass
    HID["transport"] = hid_transport.make_transport((cfg or CONFIG).get("hid"), HUB.write)


# ------------------------------------------------------------------ HTTP 层
class Handler(BaseHTTPRequestHandler):
    server_version = "MirrorLauncher/0.1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass  # 请求日志太吵，错误才走 HUB

    # ---- 工具
    def _json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode()
        try:
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
            # 客户端窗口关了，正在写的响应没人收——不是错误
            pass

    def _read_json(self):
        n = int(self.headers.get("Content-Length") or 0)
        if not n:
            return {}
        return json.loads(self.rfile.read(n).decode("utf-8"))

    def _file(self, path: Path, ctype: str):
        if not path.is_file():
            self._json({"ok": False, "error": "not found"}, 404)
            return
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    # ---- 路由
    def do_GET(self):
        p = urlparse(self.path).path
        try:
            if p in ("/", "/index.html"):
                return self._file(STATIC / "index.html", "text/html; charset=utf-8")
            if p.startswith("/static/"):
                fp = STATIC / p[len("/static/"):]
                ct = {
                    ".js": "application/javascript; charset=utf-8",
                    ".css": "text/css; charset=utf-8",
                    ".html": "text/html; charset=utf-8",
                }.get(fp.suffix, "application/octet-stream")
                return self._file(fp, ct)
            if p == "/api/state":
                return self._json({
                    "uxplay": UX.status(),
                    "hid": dict(HID["transport"].describe(), mode=CONFIG["hid"]["mode"]),
                    "config": CONFIG,
                    "presets": PRESETS,
                })
            if p == "/api/logs":
                return self._sse()
            if p == "/api/preview.bmp":
                return self._preview()
            return self._json({"ok": False, "error": "not found"}, 404)
        except Exception:
            HUB.write("GET %s 异常：%s" % (p, traceback.format_exc(limit=2)))
            self._json({"ok": False, "error": "internal"}, 500)

    def do_POST(self):
        p = urlparse(self.path).path
        try:
            body = self._read_json()
        except Exception as e:
            return self._json({"ok": False, "error": "bad json: %s" % e}, 400)

        try:
            if p == "/api/config":
                cfg = body.get("config") if isinstance(body, dict) else None
                if not isinstance(cfg, dict):
                    return self._json({"ok": False, "error": "缺少 config"}, 400)
                # 以当前配置为底，避免前端没提交的字段（preview / args 模板）被重置
                merged = json.loads(json.dumps(CONFIG))
                for k, v in cfg.items():
                    if isinstance(v, dict) and isinstance(merged.get(k), dict):
                        merged[k].update(v)
                    else:
                        merged[k] = v
                save_config(merged)
                rebuild_hid(merged)
                HUB.write("配置已保存")
                return self._json({"ok": True, "config": merged})

            if p == "/api/mirror/start":
                return self._json(UX.start(CONFIG))
            if p == "/api/mirror/stop":
                return self._json(UX.stop())

            if p == "/api/input":
                events = hid_transport.normalize_input(body)
                sent = 0
                for ev in events:
                    if ev.get("t") == "_unmapped":
                        HUB.write("未映射的键：%s" % ev.get("code"))
                        continue
                    HID["transport"].send(ev)
                    sent += 1
                return self._json({"ok": True, "sent": sent})

            return self._json({"ok": False, "error": "not found"}, 404)
        except Exception as e:
            HUB.write("POST %s 异常：%s" % (p, traceback.format_exc(limit=2)))
            return self._json({"ok": False, "error": str(e)}, 500)

    # ---- SSE
    def _sse(self):
        q = HUB.subscribe()
        self.close_connection = True
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            for line in HUB.tail(120):
                self.wfile.write(("data: " + line.replace("\n", " ") + "\n\n").encode())
            self.wfile.flush()
            last = time.time()
            while True:
                try:
                    line = q.get(timeout=10)
                    self.wfile.write(("data: " + line.replace("\n", " ") + "\n\n").encode())
                except queue.Empty:
                    if time.time() - last > 15:
                        self.wfile.write(b": ping\n\n")
                        last = time.time()
                    else:
                        continue
                self.wfile.flush()
        except Exception:
            pass
        finally:
            HUB.unsubscribe(q)

    # ---- 预览
    def _preview(self):
        pv = CONFIG.get("preview") or {}
        if not pv.get("enabled"):
            return self._json({"ok": False, "error": "preview disabled"}, 403)
        try:
            import win_capture
            data = win_capture.grab_bmp(
                pv.get("window_title", "UxPlay"),
                int(pv.get("max_width", 640)),
            )
            if not data:
                return self._json({"ok": False, "error": "未找到窗口"}, 503)
            self.send_response(200)
            self.send_header("Content-Type", "image/bmp")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)
        except Exception as e:
            self._json({"ok": False, "error": str(e)}, 500)


class Server(ThreadingHTTPServer):
    """浏览器关页面时 SSE 连接会被重置，这类噪音不打印"""
    daemon_threads = True

    def handle_error(self, request, client_address):
        exc = sys.exc_info()[1]
        if isinstance(exc, (ConnectionResetError, ConnectionAbortedError, BrokenPipeError)):
            return
        super().handle_error(request, client_address)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--host", default=None)
    a = ap.parse_args()

    host = a.host or CONFIG["server"]["host"]
    port = a.port or CONFIG["server"]["port"]

    # Windows 上 SO_REUSEADDR 允许同一端口被重复绑定，两个实例会互相抢请求，
    # 表现是「一会儿正常一会儿空响应」，所以启动前先显式探一次端口。
    import socket
    probe = socket.socket()
    probe.settimeout(0.4)
    try:
        probe.connect((host, port))
        probe.close()
        print("端口 %d 已被占用（可能已有一个启动台在跑）。换个端口：python server.py --port %d"
              % (port, port + 1))
        return 2
    except OSError:
        pass

    HUB.write("启动台就绪 → http://%s:%d" % (host, port))
    HUB.write("HID 传输层：%s" % json.dumps(HID["transport"].describe(), ensure_ascii=False))
    if not UX._exe_ok(CONFIG["uxplay"].get("exe", "uxplay")):
        HUB.write("提示：未找到 UxPlay，镜像功能不可用；在页面上配置 uxplay.exe 路径后可启动。")

    httpd = Server((host, port), Handler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        HUB.write("收到 Ctrl+C，关闭")
    finally:
        UX.stop()
        try:
            HID["transport"].close()
        except Exception:
            pass
        httpd.server_close()


if __name__ == "__main__":
    main()
