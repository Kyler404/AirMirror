#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""独立分层覆盖窗口：H 模式主窗口隐藏，无渲染器的覆盖窗口承接
UpdateLayeredWindow 呈现（读回 + 预乘 alpha）。"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# 1) 删除无效的 blur-behind 块
old = """#ifdef _WIN32
    // 启用 DWM 逐像素透明：空区域 blur-behind 打开窗口的 alpha 合成
    // 通道（配合 GL 后备缓冲的 alpha），隐藏界面模式下机身外像素
    // 透出桌面，手机直接浮在桌面上。
    {
        DWM_BLURBEHIND bb = {};
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = TRUE;
        bb.hRgnBlur = CreateRectRgn(0, 0, -1, -1);   // 空区域
        ::DwmEnableBlurBehindWindow(window, &bb);
    }
#endif
    SDL_SetWindowMinimumSize(window, 960, 720);"""
new = """    SDL_SetWindowMinimumSize(window, 960, 720);"""
assert old in t, "A"
t = t.replace(old, new)

# 2) 成员变量
old = "    DeviceFrame frame_portrait, frame_landscape;"
new = """    DeviceFrame frame_portrait, frame_landscape;
    // ---- 隐藏界面：独立分层覆盖窗口 --------------------------------
    // 主窗口隐藏，场景照常渲染到其后备缓冲；读回 + 预乘 alpha 后由
    // UpdateLayeredWindow 呈现到「无渲染器」的覆盖窗口 —— 机身外逐
    // 像素透明，手机浮在桌面上。D3D11/GL 的交换链翻转呈现会覆盖分
    // 层位图，因此覆盖窗口绝不挂渲染器（此前多次失败的根因）。
    SDL_Window* overlay_window_ = nullptr;
    bool overlay_active_ = false;
    HBITMAP overlay_dib_ = nullptr;
    void* overlay_bits_ = nullptr;
    int overlay_w_ = 0, overlay_h_ = 0;
    int overlay_last_main_w_ = 0, overlay_last_main_h_ = 0;"""
assert old in t, "B"
t = t.replace(old, new)

# 3) 过渡块：删 DWM 圆角设置
old = """            SDL_SetWindowMinimumSize(window, ui.chromeless ? 160 : 960, ui.chromeless ? 120 : 720);
#ifdef _WIN32
            // 隐藏界面：禁用系统窗口圆角（Win11 默认圆角会裁掉手机
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
#endif
            if (ui.chromeless && !fullscreen) {"""
new = """            SDL_SetWindowMinimumSize(window, ui.chromeless ? 160 : 960, ui.chromeless ? 120 : 720);
            if (ui.chromeless && !fullscreen) {"""
assert old in t, "C"
t = t.replace(old, new)

# 4) 覆盖窗口生命周期（每帧，放在 VSYNC 段之前）
old = "        // Honor live VSYNC toggle: SDL 2.0.18+ supports flipping"
new = """        // ---- 分层覆盖窗口生命周期（隐藏界面）------------------------
        // 进入隐藏界面：在主窗口位置创建覆盖窗口并隐藏主窗口；退出
        // 或进入全屏时销毁覆盖窗口、恢复主窗口。双向尺寸同步：吸附
        // 改主窗口 → 覆盖窗口跟随；用户拖拽覆盖窗口 → 主窗口跟随
        // （保证渲染分辨率与覆盖窗口一致）。
#ifdef _WIN32
        {
            const bool want_overlay = ui.chromeless && !fullscreen;
            if (want_overlay && !overlay_window_) {
                int mx = 0, my = 0, mw = 0, mh = 0;
                SDL_GetWindowPosition(window, &mx, &my);
                SDL_GetWindowSize(window, &mw, &mh);
                overlay_window_ = SDL_CreateWindow("airplay-overlay",
                    mx, my, mw, mh,
                    SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE |
                    SDL_WINDOW_SKIP_TASKBAR);
                if (overlay_window_) {
                    SDL_SetWindowMinimumSize(overlay_window_, 160, 120);
                    SDL_SetWindowHitTest(overlay_window_, hit_test_cb, &ui);
                    SDL_SysWMinfo oinfo;
                    SDL_VERSION(&oinfo.version);
                    if (SDL_GetWindowWMInfo(overlay_window_, &oinfo)) {
                        HWND hwnd = oinfo.info.win.window;
                        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                            ex | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
                        int pref = DWMWCP_DONOTROUND;
                        ::DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                                &pref, sizeof(pref));
                        overlay_active_ = true;
                        overlay_last_main_w_ = 0;
                        LOG_INFO << "overlay window created " << mw << "x" << mh;
                    }
                }
                SDL_HideWindow(window);
            }
            if (!want_overlay && overlay_window_) {
                int ox = 0, oy = 0;
                SDL_GetWindowPosition(overlay_window_, &ox, &oy);
                SDL_DestroyWindow(overlay_window_);
                overlay_window_ = nullptr;
                overlay_active_ = false;
                if (overlay_dib_) { DeleteObject(overlay_dib_); overlay_dib_ = nullptr; }
                overlay_bits_ = nullptr;
                overlay_w_ = overlay_h_ = 0;
                SDL_ShowWindow(window);
                SDL_SetWindowPosition(window, ox, oy);
                LOG_INFO << "overlay window destroyed";
            }
            if (overlay_active_) {
                int mw = 0, mh = 0, ow = 0, oh = 0;
                SDL_GetWindowSize(window, &mw, &mh);
                SDL_GetWindowSize(overlay_window_, &ow, &oh);
                if (ow != overlay_last_main_w_ || oh != overlay_last_main_h_) {
                    SDL_SetWindowSize(window, ow, oh);
                } else if (mw != ow || mh != oh) {
                    SDL_SetWindowSize(overlay_window_, mw, mh);
                }
                overlay_last_main_w_ = ow;
                overlay_last_main_h_ = oh;
            }
        }
#endif

        // Honor live VSYNC toggle: SDL 2.0.18+ supports flipping"""
assert old in t, "D"
t = t.replace(old, new, 1)

# 5) 帧尾：读回 + 预乘 + ULW
old = """        publish_preferences();
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(
            ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);"""
new = """        publish_preferences();
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(
            ImGui::GetDrawData(), renderer);

        // 隐藏界面：读回后备缓冲 → 预乘 alpha → UpdateLayeredWindow
        // 呈现到覆盖窗口（机身外逐像素透明，手机浮在桌面上）。
        if (overlay_active_ && overlay_window_) {
            int rw = 0, rh = 0;
            SDL_GetWindowSize(overlay_window_, &rw, &rh);
            if (rw > 0 && rh > 0 &&
                (!overlay_dib_ || overlay_w_ != rw || overlay_h_ != rh)) {
                if (overlay_dib_) DeleteObject(overlay_dib_);
                BITMAPV5HEADER bi = {};
                bi.bV5Size = sizeof(BITMAPV5HEADER);
                bi.bV5Width = rw; bi.bV5Height = -rh;      // top-down
                bi.bV5Planes = 1; bi.bV5BitCount = 32;
                bi.bV5Compression = BI_BITFIELDS;
                bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00;
                bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
                overlay_dib_ = CreateDIBSection(nullptr,
                    reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                    &overlay_bits_, nullptr, 0);
                overlay_w_ = rw; overlay_h_ = rh;
            }
            if (overlay_dib_ && overlay_bits_) {
                if (SDL_RenderReadPixels(renderer, nullptr,
                        SDL_PIXELFORMAT_ABGR8888, overlay_bits_,
                        overlay_w_ * 4) == 0) {
                    auto* p = static_cast<unsigned char*>(overlay_bits_);
                    for (int i = 0; i < overlay_w_ * overlay_h_; ++i, p += 4) {
                        const unsigned a = p[3];
                        const unsigned r = p[0];
                        p[0] = static_cast<unsigned char>(p[2] * a / 255);  // B
                        p[1] = static_cast<unsigned char>(p[1] * a / 255);  // G
                        p[2] = static_cast<unsigned char>(r * a / 255);     // R
                        p[3] = static_cast<unsigned char>(a);
                    }
                    SDL_SysWMinfo wm;
                    SDL_VERSION(&wm.version);
                    if (SDL_GetWindowWMInfo(overlay_window_, &wm)) {
                        HWND hwnd = wm.info.win.window;
                        HDC hdcScreen = GetDC(hwnd);
                        HDC hdcMem = CreateCompatibleDC(hdcScreen);
                        HGDIOBJ old_bmp = SelectObject(hdcMem, overlay_dib_);
                        SIZE size = {overlay_w_, overlay_h_};
                        POINT ptSrc = {0, 0};
                        UpdateLayeredWindow(hwnd, hdcScreen, nullptr, &size,
                                            hdcMem, &ptSrc, 0, nullptr, ULW_ALPHA);
                        SelectObject(hdcMem, old_bmp);
                        DeleteDC(hdcMem);
                        ReleaseDC(hwnd, hdcScreen);
                    }
                }
            }
        }

        SDL_RenderPresent(renderer);"""
assert old in t, "E"
t = t.replace(old, new)

# 6) 退出清理
old = "    if (video_rgba_tex) SDL_DestroyTexture(video_rgba_tex);"
new = """    if (video_rgba_tex) SDL_DestroyTexture(video_rgba_tex);
    if (overlay_window_) SDL_DestroyWindow(overlay_window_);
    if (overlay_dib_)  DeleteObject(overlay_dib_);"""
assert old in t, "F"
t = t.replace(old, new)

p.write_text(t, encoding="utf-8")
print("overlay architecture applied")
