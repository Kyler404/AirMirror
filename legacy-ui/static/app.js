// 投屏启动台 — 前端控制逻辑
// 只做三件事：读状态、发输入、看日志。视频画面在 UxPlay 自己的窗口里，不在这里。

const $ = (s) => document.querySelector(s);

let state = null;
let inputCount = 0;
let pendingArgs = null;   // 选了预设后要一并保存的参数模板

// ------------------------------------------------------------------ 基础
async function api(path, method = "GET", body) {
  const opt = { method, headers: {} };
  if (body !== undefined) {
    opt.headers["Content-Type"] = "application/json";
    opt.body = JSON.stringify(body);
  }
  const r = await fetch(path, opt);
  const ct = r.headers.get("content-type") || "";
  if (!ct.includes("application/json")) throw new Error("HTTP " + r.status);
  return r.json();
}

function chip(el, text, cls) {
  const base = el.classList.contains("small") ? "pill small" : "pill";
  el.textContent = text;
  el.className = base + (cls ? " " + cls : "");
}

function setHero(title, sub, hint, dotCls) {
  const t = $("#hero-title");
  if (t) t.textContent = title;
  const s = $("#hero-sub");
  if (s) s.textContent = sub;
  const h = $("#hero-hint");
  if (h) h.textContent = hint || "";
  const d = $("#dot");
  if (d) d.className = "dot" + (dotCls ? " " + dotCls : "");
}

function appendLog(line) {
  const log = $("#log");
  const atBottom = log.scrollTop + log.clientHeight >= log.scrollHeight - 30;
  log.textContent += line + "\n";
  if (log.textContent.length > 60000) log.textContent = log.textContent.slice(-40000);
  if (atBottom) log.scrollTop = log.scrollHeight;
}

// ------------------------------------------------------------------ 状态轮询
async function refresh() {
  try {
    state = await api("/api/state");
  } catch (e) {
    chip($("#chip-ux"), "后端失联", "err");
    return;
  }
  const ux = state.uxplay;
  let pillText, pillCls;
  if (ux.connected) {
    pillText = "正在投屏";
    pillCls = "on";
    setHero("正在投屏", "手机画面已连接，画面窗口已弹出。", "断开手机端的屏幕镜像即可结束。", "on");
  } else if (ux.running && ux.external) {
    pillText = `外部实例 ${ux.pid}`;
    pillCls = "on";
    setHero("服务已在运行", "检测到一个不是本客户端启动的接收端进程。", `pid ${ux.pid} · 点「停止」可接管`, "on");
  } else if (ux.running) {
    pillText = `待连接 · ${ux.uptime}s`;
    pillCls = "on";
    setHero("等待手机连接", "在 iPhone 控制中心点「屏幕镜像」，选择 AirPlay-Windows。",
            "连接成功后会自动弹出画面窗口。", "on");
  } else if (ux.last_error) {
    pillText = "启动失败";
    pillCls = "err";
    setHero("服务未启动", ux.last_error, "检查「高级设置」里的接收端路径。", "err");
  } else {
    pillText = "未启动";
    pillCls = "";
    setHero("服务未启动", "点击「启动服务」让接收端在后台待命，手机连上后自动弹出画面。", "", "");
  }
  chip($("#chip-ux"), pillText, pillCls);
  chip($("#chip-hid"), `HID: ${state.hid.mode}` + (state.hid.events !== undefined ? ` (${state.hid.events})` : ""),
       state.hid.mode !== "mock" ? "on" : "");
  chip($("#chip-in"), `输入 ${inputCount}`);
}

// ------------------------------------------------------------------ 配置
function fillConfig(cfg) {
  const u = cfg.uxplay || {};
  const h = cfg.hid || {};
  const pv = cfg.preview || {};
  $("#cfg-exe").value = u.exe || "";
  $("#cfg-name").value = u.name || "";
  $("#cfg-fps").value = u.fps || 60;
  $("#cfg-sink").value = u.video_sink || "";
  $("#cfg-fs").checked = !!u.fullscreen;
  $("#cfg-extra").value = u.extra_args || "";
  $("#cfg-hid-mode").value = h.mode || "mock";
  $("#cfg-hid-port").value = h.port || "";
  $("#cfg-preview-title").value = pv.window_title || "AirPlay";
  $("#cfg-preview").checked = pv.enabled !== false;
}

function readConfig() {
  return {
    uxplay: {
      exe: $("#cfg-exe").value.trim() || "uxplay",
      name: $("#cfg-name").value.trim() || "WebMirror",
      fps: parseInt($("#cfg-fps").value, 10) || 60,
      video_sink: $("#cfg-sink").value.trim(),
      fullscreen: $("#cfg-fs").checked,
      extra_args: $("#cfg-extra").value.trim(),
    },
    hid: {
      mode: $("#cfg-hid-mode").value,
      port: $("#cfg-hid-port").value.trim() || "COM3",
    },
    preview: {
      window_title: $("#cfg-preview-title").value.trim() || "AirPlay",
      enabled: $("#cfg-preview").checked,
    },
  };
}

// 预设：一键把 exe / 参数模板切到某个接收端（模板本身存在服务端）
function buildPresets(presets) {
  const sel = $("#cfg-preset");
  sel.innerHTML = '<option value="">自定义</option>';
  Object.entries(presets || {}).forEach(([key, p]) => {
    const o = document.createElement("option");
    o.value = key;
    o.textContent = p.label || key;
    sel.appendChild(o);
  });
  sel.onchange = () => {
    const p = (presets || {})[sel.value];
    if (!p || !p.uxplay) return;
    $("#cfg-exe").value = p.uxplay.exe || "";
    $("#cfg-fs").checked = !!p.uxplay.fullscreen;
    $("#cfg-sink").value = p.uxplay.video_sink || "";
    $("#cfg-extra").value = p.uxplay.extra_args || "";
    $("#cfg-preview-title").value = p.preview_title || "AirPlay";
    pendingArgs = p.uxplay.args || null;
    appendLog("已套用预设：" + (p.label || sel.value) + "（记得点保存配置）");
  };
}

$("#btn-save").onclick = async () => {
  const cfg = readConfig();
  if (pendingArgs) cfg.uxplay.args = pendingArgs;
  const r = await api("/api/config", "POST", { config: cfg });
  appendLog(r.ok ? "配置已保存" : "保存失败：" + r.error);
  refresh();
};

$("#btn-start").onclick = async () => {
  const r = await api("/api/mirror/start", "POST", {});
  if (!r.ok) appendLog("启动失败：" + r.error);
  refresh();
};

$("#btn-stop").onclick = async () => {
  const r = await api("/api/mirror/stop", "POST", {});
  if (!r.ok) appendLog("停止失败：" + r.error);
  refresh();
};

// ------------------------------------------------------------------ 输入：指针
const pad = $("#pad");
let down = false;
let last = null;
let acc = { x: 0, y: 0 };
let rafPending = false;

function send(ev) {
  inputCount++;
  fetch("/api/input", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(ev),
  }).catch(() => {});
}

function flushMove() {
  rafPending = false;
  const dx = Math.trunc(acc.x);
  const dy = Math.trunc(acc.y);
  acc.x -= dx;
  acc.y -= dy;
  if (dx || dy) send({ type: "move", dx, dy });
  if (Math.abs(acc.x) >= 1 || Math.abs(acc.y) >= 1) scheduleMove();
}

function scheduleMove() {
  if (rafPending) return;
  rafPending = true;
  requestAnimationFrame(flushMove);
}

pad.addEventListener("pointermove", (e) => {
  const hover = $("#cfg-hover").checked;
  if (!hover && !down) { last = { x: e.clientX, y: e.clientY }; return; }
  if (!last) last = { x: e.clientX, y: e.clientY };
  // 指针加速度：慢速精细、快速放大，接近系统指针手感
  const raw = { x: e.clientX - last.x, y: e.clientY - last.y };
  const speed = Math.hypot(raw.x, raw.y);
  const gain = speed > 40 ? 1.8 : speed > 12 ? 1.25 : 1;
  acc.x += raw.x * gain;
  acc.y += raw.y * gain;
  last = { x: e.clientX, y: e.clientY };
  scheduleMove();
});

pad.addEventListener("pointerleave", () => { last = null; });

pad.addEventListener("pointerdown", (e) => {
  pad.focus();
  down = true;
  last = { x: e.clientX, y: e.clientY };
  pad.setPointerCapture(e.pointerId);
  const btn = e.button === 2 ? "right" : e.button === 1 ? "middle" : "left";
  send({ type: "button", button: btn, down: true });
  e.preventDefault();
});

pad.addEventListener("pointerup", (e) => {
  down = false;
  const btn = e.button === 2 ? "right" : e.button === 1 ? "middle" : "left";
  send({ type: "button", button: btn, down: false });
});

pad.addEventListener("contextmenu", (e) => e.preventDefault());

pad.addEventListener("wheel", (e) => {
  e.preventDefault();
  const dy = Math.sign(e.deltaY) * Math.max(1, Math.min(3, Math.round(Math.abs(e.deltaY) / 100)));
  send({ type: "scroll", dy });
}, { passive: false });

document.querySelectorAll("[data-btn]").forEach((b) => {
  b.onclick = () => {
    const kind = b.dataset.btn;
    if (kind === "scroll-up") return send({ type: "scroll", dy: -2 });
    if (kind === "scroll-down") return send({ type: "scroll", dy: 2 });
    send({ type: "click", button: kind });
  };
});

// ------------------------------------------------------------------ 输入：键盘
function modsOf(e) {
  const m = [];
  if (e.ctrlKey) m.push("Control");
  if (e.shiftKey) m.push("Shift");
  if (e.altKey) m.push("Alt");
  if (e.metaKey) m.push("Meta");
  return m;
}

function typingInField() {
  const a = document.activeElement;
  return a && (a.tagName === "INPUT" || a.tagName === "SELECT" || a.tagName === "TEXTAREA");
}

window.addEventListener("keydown", (e) => {
  if (!$("#cfg-keyboard").checked || typingInField()) return;
  if (e.key === "F5" || e.key === "F12") return;
  send({ type: "key", code: e.code, down: true, mods: modsOf(e) });
  e.preventDefault();
});

window.addEventListener("keyup", (e) => {
  if (!$("#cfg-keyboard").checked || typingInField()) return;
  if (e.key === "F5" || e.key === "F12") return;
  send({ type: "key", code: e.code, down: false, mods: modsOf(e) });
  e.preventDefault();
});

$("#btn-focus").onclick = () => pad.focus();

// ------------------------------------------------------------------ 文本 / 快捷
function sendText() {
  const t = $("#txt").value;
  if (!t) return;
  send({ type: "text", text: t });
  $("#txt").value = "";
}
$("#btn-text").onclick = sendText;
$("#txt").addEventListener("keydown", (e) => {
  if (e.key === "Enter") { sendText(); e.preventDefault(); }
});

const COMBOS = {
  home: { code: "KeyH", mods: ["Meta"] },
  appswitcher: { code: "KeyH", mods: ["Meta", "Shift"] },
  back: { code: "BracketLeft", mods: ["Meta"] },
};

document.querySelectorAll("[data-combo]").forEach((b) => {
  b.onclick = () => {
    const c = COMBOS[b.dataset.combo];
    if (!c) return;
    send({ type: "key", code: c.code, down: true, mods: c.mods });
    send({ type: "key", code: c.code, down: false, mods: c.mods });
  };
});

// ------------------------------------------------------------------ 预览
// 只在镜像真的在跑的时候轮询；抓不到窗口就退避到 3s，避免刷 503 日志
let previewTimer = null;
let previewFailing = false;

function tickPreview() {
  const img = $("#preview");
  if (!state || !state.uxplay.running || !$("#cfg-preview").checked) {
    img.removeAttribute("src");
    return;
  }
  img.src = "/api/preview.bmp?t=" + Date.now();
}

function syncPreview() {
  const want = $("#cfg-preview").checked && state && state.uxplay.running;
  if (!want) {
    if (previewTimer) { clearInterval(previewTimer); previewTimer = null; }
    $("#preview").removeAttribute("src");
    return;
  }
  if (!previewTimer) {
    tickPreview();
    previewTimer = setInterval(tickPreview, 500);
  }
}

$("#preview").onload = () => { previewFailing = false; };
$("#preview").onerror = () => {
  $("#preview").removeAttribute("src");
  if (!previewFailing) {
    previewFailing = true;
    if (previewTimer) {
      clearInterval(previewTimer);
      previewTimer = setInterval(tickPreview, 3000);
    }
  }
};
$("#cfg-preview").onchange = () => syncPreview();

// ------------------------------------------------------------------ 启动
const es = new EventSource("/api/logs");
es.onmessage = (e) => appendLog(e.data);
es.onerror = () => appendLog("（日志流断开，浏览器会自动重连）");

refresh().then(() => {
  if (state && state.config) fillConfig(state.config);
  if (state && state.presets) buildPresets(state.presets);
  syncPreview();
});
setInterval(async () => {
  await refresh();
  syncPreview();
}, 1200);
