#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_dpi.py — 修糊：让窗口按系统 DPI 真实渲染，而不是被 Windows 位图拉伸

三处改动：
  1. main.cpp 开头声明 Per-Monitor-V2 DPI 感知
  2. video_renderer.cpp 里按窗口 DPI 算缩放系数 g_ui_scale
  3. ImGui 字体 / SDL_ttf 字号 / ImGui 样式尺寸 全部乘以该系数
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent / "AirPlay-Windows" / "src"
MAIN = ROOT / "main.cpp"
VR = ROOT / "video" / "video_renderer.cpp"

SCALE_BLOCK = r'''
// ---- DPI 缩放：窗口按物理像素渲染，字体和控件按比例放大 ----
static float compute_ui_scale(SDL_Window* w) {
    float s = 1.0f;
#ifdef _WIN32
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (w && SDL_GetWindowWMInfo(w, &info)) {
        UINT dpi = GetDpiForWindow(info.info.win.window);
        if (dpi >= 96) s = dpi / 96.0f;
    }
#endif
    if (s < 1.0f) s = 1.0f;
    return s;
}
static float g_ui_scale = 1.0f;
static inline float S(float v) { return v * g_ui_scale; }
'''


def patch_main():
    t = MAIN.read_text(encoding="utf-8", errors="surrogateescape")
    if "SetProcessDpiAwarenessContext" in t:
        return "main.cpp 已打过补丁"
    anchor = "int main(int argc, char** argv) {"
    if anchor not in t:
        return "main.cpp 找不到入口，跳过"
    inject = (anchor + "\n"
              "#if defined(_WIN32)\n"
              "    // 必须在 SDL 建窗口之前：否则 Windows 会把窗口按位图拉伸，界面发虚\n"
              "    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);\n"
              "#endif\n")
    t = t.replace(anchor, inject, 1)
    # 需要 windows.h
    if "#include <windows.h>" not in t and "#include <Windows.h>" not in t:
        t = t.replace("#include", "#if defined(_WIN32)\n#include <windows.h>\n#endif\n#include", 1)
    MAIN.write_text(t, encoding="utf-8", errors="surrogateescape")
    return "main.cpp 已加入 DPI 感知"


def patch_renderer():
    t = VR.read_text(encoding="utf-8", errors="surrogateescape")
    if "compute_ui_scale" in t:
        return "video_renderer.cpp 已打过补丁"

    # 1) 在 ImGui 初始化之前插入缩放函数与全局系数
    anchor = "    IMGUI_CHECKVERSION();"
    if anchor not in t:
        return "找不到 IMGUI_CHECKVERSION 锚点"
    t = t.replace(anchor, SCALE_BLOCK + "\n    g_ui_scale = compute_ui_scale(window);\n\n" + anchor, 1)

    # 2) 字体字号乘以缩放（ImGui 与 SDL_ttf 两处）
    def wrap(m):
        return "%s, S(%s" % (m.group(1), m.group(2))

    t, n1 = re.subn(r'(AddFontFromFileTTF\("[^"]*")\s*,\s*([0-9]+\.0f)', wrap, t)

    # 3) SDL_ttf 的字体链按缩放打开
    t, n2 = re.subn(r'(TTF_OpenFont\(path,\s*)(pt_size)\)', r'\1(int)(pt_size * g_ui_scale))', t)

    # 4) ImGui 样式尺寸同步缩放（内边距、圆角、行高）
    t = t.replace("    apply_overlay_theme();",
                  "    apply_overlay_theme();\n    if (g_ui_scale > 1.01f) ImGui::GetStyle().ScaleAllSizes(g_ui_scale);", 1)

    VR.write_text(t, encoding="utf-8", errors="surrogateescape")
    return "video_renderer.cpp: 字体 %d 处、TTF 链 %d 处、样式尺寸已缩放" % (n1, n2)


if __name__ == "__main__":
    if sys.argv[1:] and sys.argv[1] == "--check":
        print(("OK " if "compute_ui_scale" in VR.read_text(encoding='utf-8', errors='surrogateescape') else "未打 ") + "renderer")
        sys.exit(0)
    print(patch_main())
    print(patch_renderer())
