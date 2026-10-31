#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""patch_service.py — 加「开始/停止接收」开关 + UI 版式微调"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent / "AirPlay-Windows" / "src"
H = SRC / "video" / "video_renderer.h"
CPP = SRC / "video" / "video_renderer.cpp"
MAIN = SRC / "main.cpp"


def patch_header():
    t = H.read_text(encoding="utf-8", errors="surrogateescape")
    if "set_service_toggle" in t:
        return "header 已有"
    anchor = "    void set_disconnect_handler(std::function<void()> h);"
    if anchor not in t:
        return "header 锚点没找到"
    add = anchor + """
    // 服务开关：true=开始接收 false=停止接收，返回是否成功
    void set_service_toggle(std::function<bool(bool)> h) { service_toggle_ = std::move(h); }
    void set_service_running(bool on) { service_running_.store(on); }
    bool service_running() const { return service_running_.load(); }"""
    t = t.replace(anchor, add, 1)

    # 成员变量：放在 closed_ 附近
    m = re.search(r'\n(\s*)std::atomic<bool>\s+closed_\{false\};', t)
    if m:
        t = t[:m.end()] + ("\n%sstd::function<bool(bool)> service_toggle_;" % m.group(1)
                           + "\n%sstd::atomic<bool> service_running_{true};" % m.group(1)) + t[m.end():]
    H.write_text(t, encoding="utf-8", errors="surrogateescape")
    return "header 已加接口"


def patch_cpp():
    t = CPP.read_text(encoding="utf-8", errors="surrogateescape")
    if "##service_toggle" in t:
        return "renderer 已有开关按钮"
    # 侧栏状态文字之后插入
    anchor = '        ImGui::PopFont(); ImGui::End(); ImGui::PopStyleVar();'
    idx = t.find(anchor)
    if idx < 0:
        return "renderer 锚点没找到"
    block = '''
        // ---- 服务开关 ----
        ImGui::Dummy(ImVec2(0, 6));
        {
            const bool on = service_running();
            const char* label = on ? "\\u505c\\u6b62\\u63a5\\u6536" : "\\u5f00\\u59cb\\u63a5\\u6536";
            ImGui::SetCursorPosX(25);
            if (ui_draw::nav("##service_toggle", label,
                             on ? ui_draw::Icon::Close : ui_draw::Icon::Cast,
                             false, 162, io.FontDefault)) {
                if (service_toggle_) service_toggle_(!on);
            }
        }
'''
    t = t[:idx] + block + t[idx:]
    CPP.write_text(t, encoding="utf-8", errors="surrogateescape")
    return "renderer 已加开关按钮"


def patch_main():
    t = MAIN.read_text(encoding="utf-8", errors="surrogateescape")
    if "set_service_toggle" in t:
        return "main 已接线"
    anchor = '''    if (!mdns.start(ctx, server.port())) {
        LOG_WARN << "mDNS not available \u2014 receiver won't auto-appear on iOS";
    }'''
    idx = t.find(anchor)
    if idx < 0:
        # 用更宽松的锚点
        idx = t.find("ap::mdns::MdnsService mdns;")
        if idx < 0:
            return "main 锚点没找到"
        idx = t.find("\n", t.find("}", t.find("mdns.start", idx))) + 1
    else:
        idx += len(anchor)

    block = '''

    // ---- \u670d\u52a1\u5f00\u5173\uff1a\u505c\u6b62\u4f1a\u5173\u6389 mDNS \u5e7f\u64ad + 7000 \u76d1\u542c\uff0c\u5f00\u542f\u91cd\u65b0\u5efa\u7acb ----
    renderer.set_service_running(true);
    renderer.set_service_toggle([&](bool enable) -> bool {
        static std::mutex svc_mutex;
        std::lock_guard<std::mutex> lock(svc_mutex);
        if (enable) {
            if (!server.start(ctx, 7000)) {
                LOG_ERROR << "(\u91cd)\u542f\u52a8 AirPlay \u670d\u52a1\u5931\u8d25";
                return false;
            }
            if (!mdns.start(ctx, server.port())) {
                LOG_WARN << "mDNS \u91cd\u65b0\u5e7f\u64ad\u5931\u8d25";
            }
            renderer.set_service_running(true);
            LOG_INFO << "=== \u63a5\u6536\u670d\u52a1\u5df2\u5f00\u542f ===";
            return true;
        }
        mdns.stop();
        server.disconnect_clients();
        server.stop();
        renderer.set_service_running(false);
        LOG_INFO << "=== \u63a5\u6536\u670d\u52a1\u5df2\u505c\u6b62 ===";
        return true;
    });
'''
    t = t[:idx] + block + t[idx:]
    MAIN.write_text(t, encoding="utf-8", errors="surrogateescape")
    return "main 已接线"


if __name__ == "__main__":
    print(patch_header())
    print(patch_cpp())
    print(patch_main())
