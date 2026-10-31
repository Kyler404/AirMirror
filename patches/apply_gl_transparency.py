#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""透明窗口最终迁移：
1. 渲染器切 OpenGL（GL 的 alpha 通道 + DWM blur-behind = 验证过的
   逐像素透明组合；D3D11 翻转模型会绕过 DWM 分层/alpha，不可用）
2. 删除 ULW 分层窗口机制（与交换链呈现冲突）
3. 隐藏界面/全屏底色写全透明
4. GPU 解码随 D3D11 互操作不可用自动回退 CPU 解码
"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# 1) 窗口标志 + 渲染驱动
old = """    SDL_Window*   window   = SDL_CreateWindow(
        title.c_str(), initial_x, initial_y, initial_w, initial_h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS);"""
new = """    SDL_Window*   window   = SDL_CreateWindow(
        title.c_str(), initial_x, initial_y, initial_w, initial_h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_OPENGL);"""
assert old in t, "window"
t = t.replace(old, new)

old = """#if defined(_WIN32)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif
    SDL_Renderer* renderer = window
        ? SDL_CreateRenderer(window, -1, renderer_flags)
        : nullptr;"""
new = """#if defined(_WIN32)
    // OpenGL 渲染器：alpha 通道 + DWM blur-behind 才能实现逐像素
    // 透明窗口（D3D11 翻转模型会绕过 DWM 合成，透明失效）。
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
    SDL_Renderer* renderer = window
        ? SDL_CreateRenderer(window, -1, renderer_flags)
        : nullptr;"""
assert old in t, "driver"
t = t.replace(old, new)

# 2) 删除 ULW 每帧块
old = """        // 隐藏界面：渲染结果读回 + 预乘 alpha → UpdateLayeredWindow
        // （机身外逐像素透明，手机浮在桌面上）。
        if (layered_on) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(window, &ww, &wh);
            if (ww > 0 && wh > 0) {
                if (layered_w != ww || layered_h != wh || !layered_readback) {
                    if (layered_readback) SDL_DestroyTexture(layered_readback);
                    layered_readback = SDL_CreateTexture(renderer,
                        SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, ww, wh);
                    if (layered_dib) { DeleteObject(layered_dib); layered_dib = nullptr; }
                    BITMAPV5HEADER bi = {};
                    bi.bV5Size = sizeof(BITMAPV5HEADER);
                    bi.bV5Width = ww; bi.bV5Height = -wh;   // top-down
                    bi.bV5Planes = 1; bi.bV5BitCount = 32;
                    bi.bV5Compression = BI_BITFIELDS;
                    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00;
                    bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
                    layered_dib = CreateDIBSection(nullptr,
                        reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                        &layered_bits, nullptr, 0);
                    layered_w = ww; layered_h = wh;
                }
            }
            if (layered_readback && layered_bits) {
                SDL_RenderReadPixels(renderer, nullptr,
                    SDL_PIXELFORMAT_ABGR8888, layered_bits, layered_w * 4);
                auto* p = static_cast<unsigned char*>(layered_bits);
                for (int i = 0; i < layered_w * layered_h; ++i, p += 4) {
                    const unsigned a = p[3];
                    const unsigned t = p[0];                     // R
                    p[0] = static_cast<unsigned char>(p[2] * a / 255);  // B
                    p[1] = static_cast<unsigned char>(p[1] * a / 255);  // G
                    p[2] = static_cast<unsigned char>(t * a / 255);     // R
                    p[3] = static_cast<unsigned char>(a);
                }
                SDL_SysWMinfo wm;
                SDL_VERSION(&wm.version);
                if (SDL_GetWindowWMInfo(window, &wm)) {
                    HWND hwnd = wm.info.win.window;
                    HDC hdcScreen = GetDC(hwnd);
                    HDC hdcMem = CreateCompatibleDC(hdcScreen);
                    HGDIOBJ old_bmp = SelectObject(hdcMem, layered_dib);
                    SIZE size = {layered_w, layered_h};
                    POINT ptSrc = {0, 0};
                    UpdateLayeredWindow(hwnd, hdcScreen, nullptr, &size,
                                        hdcMem, &ptSrc, 0, nullptr, ULW_ALPHA);
                    SelectObject(hdcMem, old_bmp);
                    DeleteDC(hdcMem);
                    ReleaseDC(hwnd, hdcScreen);
                }
            }
        }

        // 分层窗口模式下 ULW 是唯一呈现源；跳过交换链翻转，
        // 否则翻转模型会覆盖分层位图、透明失效。
        if (!layered_on) SDL_RenderPresent(renderer);"""
new = """        SDL_RenderPresent(renderer);"""
assert old in t, "ulw block"
t = t.replace(old, new)

# 3) 删除分层成员与切换块中的样式设置
old = """    // ---- 隐藏界面的分层窗口呈现 ------------------------------------
    // WS_EX_LAYERED + UpdateLayeredWindow：每帧把渲染结果读回、预乘
    // alpha 后交给 DWM —— 机身外像素逐像素透明，手机直接浮在桌面
    // 上（圆角、投影全保留，无色键硬边）。
    SDL_Texture* layered_readback = nullptr;
    int layered_w = 0, layered_h = 0;
    HBITMAP layered_dib = nullptr;
    void* layered_bits = nullptr;
    bool layered_on = false;"""
new = ""
assert old in t, "members"
t = t.replace(old, new)

old = """            // 隐藏界面：① 禁用系统窗口圆角（Win11 默认圆角会裁掉手机
            // 外壳的四个角）；② 分层窗口 —— 每帧把渲染结果以带 alpha 的
            // 位图交给 DWM（UpdateLayeredWindow），机身外逐像素透明，
            // 手机浮在桌面上。恢复界面时还原普通窗口。
            {
                SDL_SysWMinfo wm;
                SDL_VERSION(&wm.version);
                if (SDL_GetWindowWMInfo(window, &wm)) {
                    HWND hwnd = wm.info.win.window;
                    int pref = ui.chromeless ? DWMWCP_DONOTROUND : DWMWCP_DEFAULT;
                    ::DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                            &pref, sizeof(pref));
                    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                    if (ui.chromeless) {
                        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
                        layered_on = true;
                    } else {
                        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                                          ex & ~WS_EX_LAYERED);
                        layered_on = false;
                    }
                }
            }
#endif"""
new = """            // 隐藏界面：禁用系统窗口圆角（Win11 默认圆角会裁掉手机
            // 外壳的四个角）。逐像素透明由 DWM blur-behind（窗口创建
            // 时启用）+ 全透明底色实现。
            {
                SDL_SysWMinfo wm;
                SDL_VERSION(&wm.version);
                if (SDL_GetWindowWMInfo(window, &wm)) {
                    int pref = ui.chromeless ? DWMWCP_DONOTROUND : DWMWCP_DEFAULT;
                    ::DwmSetWindowAttribute(wm.info.win.window,
                                            DWMWA_WINDOW_CORNER_PREFERENCE,
                                            &pref, sizeof(pref));
                }
            }
#endif"""
assert old in t, "style"
t = t.replace(old, new)

# 4) 底色条件：no_chrome 即全透明（去掉 layered_on 依赖）
old = """        if (no_chrome && layered_on) {
            // 分层窗口：清屏写成透明黑（ULW 的 alpha 来自渲染结果）
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);"""
new = """        if (no_chrome) {
            // 隐藏界面/全屏：清屏写成全透明（DWM 合成桌面背景）
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);"""
assert old in t, "clear"
t = t.replace(old, new)

old = """            if (no_chrome && layered_on) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            } else {
                SDL_SetRenderDrawColor(renderer, no_chrome ? 1 : 23, no_chrome ? 2 : 30, no_chrome ? 3 : 34, 255);
            }"""
new = """            if (no_chrome) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            } else {
                SDL_SetRenderDrawColor(renderer, 23, 30, 34, 255);
            }"""
assert old in t, "showfill"
t = t.replace(old, new)

# 5) 呈现恢复无条件
old = """        // 分层窗口模式下 ULW 是唯一呈现源；跳过交换链翻转，
        // 否则翻转模型会覆盖分层位图、透明失效。
        if (!layered_on) SDL_RenderPresent(renderer);"""
new = """        SDL_RenderPresent(renderer);"""
if old in t:
    t = t.replace(old, new)
elif "if (!layered_on) SDL_RenderPresent(renderer);" in t:
    t = t.replace("if (!layered_on) SDL_RenderPresent(renderer);",
                  "SDL_RenderPresent(renderer);")
else:
    print("present: already plain")

# 6) 退出清理
old = """    if (layered_readback) SDL_DestroyTexture(layered_readback);
    if (layered_dib)   DeleteObject(layered_dib);"""
new = ""
assert old in t, "cleanup"
t = t.replace(old, new)

p.write_text(t, encoding="utf-8")
print("opengl transparency migration applied")
