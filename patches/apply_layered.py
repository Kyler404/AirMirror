#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把隐藏界面切换为 UpdateLayeredWindow 分层窗口（一次性代码迁移脚本）"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# 1) 色键块 → 分层窗口（ULW）
old = '''            // 隐藏界面：① 禁用系统窗口圆角（Win11 默认圆角会裁掉手机
            // 外壳的四个角）；② 色键窗口透明 —— 键色 (1,2,3) 的像素由
            // 系统透明化，手机浮在桌面上、无窗口背景。恢复界面时还原。
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
                        SetLayeredWindowAttributes(hwnd, RGB(1, 2, 3), 0,
                                                   LWA_COLORKEY);
                    } else {
                        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                                          ex & ~WS_EX_LAYERED);
                    }
                }
            }
#endif'''
new = '''            // 隐藏界面：① 禁用系统窗口圆角（Win11 默认圆角会裁掉手机
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
#endif'''
assert old in t, "B"
t = t.replace(old, new)

# 2) 成员变量
old = '''    int frame_loaded_style = -2;   // -2 强制首次加载
    std::string frame_last_label;  // 上一个生效的分组，自动模式借此延续颜色
    std::string frame_last_model;  // 自动模式最近一次匹配的设备型号标识
    DeviceFrame frame_portrait, frame_landscape;'''
new = '''    int frame_loaded_style = -2;   // -2 强制首次加载
    std::string frame_last_label;  // 上一个生效的分组，自动模式借此延续颜色
    std::string frame_last_model;  // 自动模式最近一次匹配的设备型号标识
    DeviceFrame frame_portrait, frame_landscape;
    // ---- 隐藏界面的分层窗口呈现 ------------------------------------
    // WS_EX_LAYERED + UpdateLayeredWindow：每帧把渲染结果读回、预乘
    // alpha 后交给 DWM —— 机身外像素逐像素透明，手机直接浮在桌面
    // 上（圆角、投影全保留，无色键硬边）。
    SDL_Texture* layered_readback = nullptr;
    int layered_w = 0, layered_h = 0;
    HBITMAP layered_dib = nullptr;
    void* layered_bits = nullptr;
    bool layered_on = false;'''
assert old in t, "members"
t = t.replace(old, new)

# 3) 底色：分层模式写透明黑（全局清屏 + show_video 底两处）
old = '''        SDL_SetRenderDrawColor(renderer, no_chrome ? 1 : 245, no_chrome ? 2 : 246, no_chrome ? 3 : 244, 255);
        SDL_RenderClear(renderer);'''
new = '''        if (no_chrome && layered_on) {
            // 分层窗口：清屏写成透明黑（ULW 的 alpha 来自渲染结果）
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        } else {
            SDL_SetRenderDrawColor(renderer, no_chrome ? 1 : 245, no_chrome ? 2 : 246, no_chrome ? 3 : 244, 255);
        }
        SDL_RenderClear(renderer);'''
assert old in t, "fill"
t = t.replace(old, new)

old = '''        if (show_video) {
            SDL_SetRenderDrawColor(renderer, no_chrome ? 1 : 23, no_chrome ? 2 : 30, no_chrome ? 3 : 34, 255);
            SDL_RenderFillRect(renderer, nullptr);'''
new = '''        if (show_video) {
            if (no_chrome && layered_on) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            } else {
                SDL_SetRenderDrawColor(renderer, no_chrome ? 1 : 23, no_chrome ? 2 : 30, no_chrome ? 3 : 34, 255);
            }
            SDL_RenderFillRect(renderer, nullptr);'''
assert old in t, "fill2"
t = t.replace(old, new)

# 4) 每帧 ULW（Present 前）
old = '''        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);'''
new = '''        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

        // 隐藏界面：渲染结果读回 + 预乘 alpha → UpdateLayeredWindow
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

        SDL_RenderPresent(renderer);'''
if old in t:
    t = t.replace(old, new)   # present 块可能已由手工编辑插入，幂等跳过

# 5) 退出清理
old = '''    gpu_video.reset_target();
    if (video_tex)     SDL_DestroyTexture(video_tex);
    if (cover_tex)     SDL_DestroyTexture(cover_tex);'''
new = '''    gpu_video.reset_target();
    if (video_tex)     SDL_DestroyTexture(video_tex);
    if (cover_tex)     SDL_DestroyTexture(cover_tex);
    if (layered_readback) SDL_DestroyTexture(layered_readback);
    if (layered_dib)   DeleteObject(layered_dib);'''
assert old in t, "cleanup"
t = t.replace(old, new)

p.write_text(t, encoding="utf-8")
print("layered window applied")
