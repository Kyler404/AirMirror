#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""隐藏界面最终迁移：独立分层覆盖窗口（无交换链冲突）
- H 模式：主窗口隐藏，创建纯分层覆盖窗口（无渲染器/无交换链）
- 每帧：主渲染器渲染场景 → 读回 → 预乘 alpha → ULW 呈现到覆盖窗口
- 退出：销毁覆盖窗口，显示主窗口
- 画面：RGBA 转绘 + SDL_RenderGeometry 圆角裁剪（内容区圆角）
"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# 1) 成员：layered_* → overlay_* + rgba 中间纹理
old = '''    // ---- 隐藏界面的分层窗口呈现 ------------------------------------
    // WS_EX_LAYERED + UpdateLayeredWindow：每帧把渲染结果读回、预乘
    // alpha 后交给 DWM —— 机身外像素逐像素透明，手机直接浮在桌面
    // 上（圆角、投影全保留，无色键硬边）。
    SDL_Texture* layered_readback = nullptr;
    int layered_w = 0, layered_h = 0;
    HBITMAP layered_dib = nullptr;
    void* layered_bits = nullptr;
    bool layered_on = false;'''
new = '''    // ---- 隐藏界面：独立分层覆盖窗口 --------------------------------
    // 主窗口隐藏，场景照常渲染到其后备缓冲，读回后经预乘 alpha 由
    // UpdateLayeredWindow 呈现到「无交换链」的覆盖窗口 —— 机身外逐
    // 像素透明，手机浮在桌面上。D3D11 交换链的翻转呈现会覆盖分层
    // 位图，因此覆盖窗口绝不挂渲染器。
    SDL_Window* overlay_window_ = nullptr;
    bool overlay_active_ = false;
    HBITMAP overlay_dib_ = nullptr;
    void* overlay_bits_ = nullptr;
    int overlay_w_ = 0, overlay_h_ = 0;
    // 圆角绘制用的 RGBA 中间纹理：NV12/YUV 视频纹理不能直接走
    // SDL_RenderGeometry（D3D11 后端的几何管线采样不到 UV 平面），
    // 每帧先把当前帧转绘到这张离屏纹理，再对它做圆角网格采样。
    SDL_Texture* video_rgba_tex = nullptr;
    int video_rgba_w = 0, video_rgba_h = 0;'''
assert old in t, "members"
t = t.replace(old, new)

# 2) 隐藏界面切换：色键 → 覆盖窗口创建/销毁
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
new = '''            // 隐藏界面：主窗口隐藏，改用「无交换链」的分层覆盖窗口
            // 呈现（D3D11 交换链的翻转呈现会覆盖分层位图，两者不能
            // 共存）。覆盖窗口禁用系统圆角（否则裁掉手机四角）。
            {
                SDL_SysWMinfo wm;
                SDL_VERSION(&wm.version);
                if (SDL_GetWindowWMInfo(window, &wm)) {
                    int pref = DWMWCP_DONOTROUND;
                    ::DwmSetWindowAttribute(wm.info.win.window,
                                            DWMWA_WINDOW_CORNER_PREFERENCE,
                                            &pref, sizeof(pref));
                }
                int mx = 0, my = 0, mw = 0, mh = 0;
                SDL_GetWindowPosition(window, &mx, &my);
                SDL_GetWindowSize(window, &mw, &mh);
                if (!overlay_window_) {
                    overlay_window_ = SDL_CreateWindow("airplay-overlay",
                        mx, my, mw, mh,
                        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
                        SDL_WINDOW_SKIP_TASKBAR);
                } else {
                    SDL_SetWindowPosition(overlay_window_, mx, my);
                    SDL_SetWindowSize(overlay_window_, mw, mh);
                }
                SDL_HideWindow(window);
                if (overlay_window_) {
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
                        if (!overlay_dib_ || overlay_w_ != mw || overlay_h_ != mh) {
                            if (overlay_dib_) DeleteObject(overlay_dib_);
                            BITMAPV5HEADER bi = {};
                            bi.bV5Size = sizeof(BITMAPV5HEADER);
                            bi.bV5Width = mw; bi.bV5Height = -mh;   // top-down
                            bi.bV5Planes = 1; bi.bV5BitCount = 32;
                            bi.bV5Compression = BI_BITFIELDS;
                            bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00;
                            bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
                            overlay_dib_ = CreateDIBSection(nullptr,
                                reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                                &overlay_bits_, nullptr, 0);
                            overlay_w_ = mw; overlay_h_ = mh;
                        }
                        overlay_active_ = overlay_dib_ != nullptr;
                    }
                }
            }
#endif'''
assert old in t, "transition"
t = t.replace(old, new)

# 3) 隐藏界面退出：销毁覆盖窗口、显示主窗口
old = '''            } else if (!ui.chromeless &&
                       saved_window_w > 0 && saved_window_h > 0) {
                SDL_SetWindowSize(window,
                                  saved_window_w, saved_window_h);
                if (saved_window_maximized) SDL_MaximizeWindow(window);
            }'''
new = '''            } else if (!ui.chromeless &&
                       saved_window_w > 0 && saved_window_h > 0) {
                SDL_SetWindowSize(window,
                                  saved_window_w, saved_window_h);
                if (saved_window_maximized) SDL_MaximizeWindow(window);
            }
            if (!ui.chromeless && overlay_window_) {
                SDL_DestroyWindow(overlay_window_);
                overlay_window_ = nullptr;
            }
            overlay_active_ = false;
            if (overlay_dib_) { DeleteObject(overlay_dib_); overlay_dib_ = nullptr; }
            overlay_bits_ = nullptr;
            if (!ui.chromeless) SDL_ShowWindow(window);'''
assert old in t, "exit"
t = t.replace(old, new)

# 4) 每帧：读回主渲染器后备缓冲 → 预乘 → ULW 覆盖窗口
old = '''        // 隐藏界面：渲染结果读回 + 预乘 alpha → UpdateLayeredWindow
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
        if (!layered_on) SDL_RenderPresent(renderer);'''
new = '''        // 隐藏界面：渲染结果读回 + 预乘 alpha → UpdateLayeredWindow
        // 呈现到覆盖窗口（机身外逐像素透明，手机浮在桌面上）。
        if (overlay_active_ && overlay_bits_ && overlay_window_) {
            SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ABGR8888,
                                 overlay_bits_, overlay_w_ * 4);
            auto* p = static_cast<unsigned char*>(overlay_bits_);
            for (int i = 0; i < overlay_w_ * overlay_h_; ++i, p += 4) {
                const unsigned a = p[3];
                const unsigned t = p[0];                     // R
                p[0] = static_cast<unsigned char>(p[2] * a / 255);  // B
                p[1] = static_cast<unsigned char>(p[1] * a / 255);  // G
                p[2] = static_cast<unsigned char>(t * a / 255);     // R
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

        SDL_RenderPresent(renderer);'''
assert old in t, "present"
t = t.replace(old, new)

# 5) 底色条件：隐藏界面即全透明（去掉 layered_on 依赖）
old = '''        if (no_chrome && layered_on) {
            // 分层窗口：清屏写成透明黑（ULW 的 alpha 来自渲染结果）
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);'''
new = '''        if (no_chrome) {
            // 隐藏界面：清屏写成透明黑（ULW 的 alpha 来自渲染结果）
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);'''
assert old in t, "clear"
t = t.replace(old, new)

old = '''            if (no_chrome && layered_on) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            } else {
                SDL_SetRenderDrawColor(renderer, no_chrome ? 1 : 23, no_chrome ? 2 : 30, no_chrome ? 3 : 34, 255);
            }'''
new = '''            if (no_chrome) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            } else {
                SDL_SetRenderDrawColor(renderer, 23, 30, 34, 255);
            }'''
assert old in t, "showfill"
t = t.replace(old, new)

# 6) 清理：覆盖窗口 + rgba
old = '''    if (layered_readback) SDL_DestroyTexture(layered_readback);
    if (layered_dib)   DeleteObject(layered_dib);'''
new = '''    if (overlay_window_) SDL_DestroyWindow(overlay_window_);
    if (overlay_dib_)  DeleteObject(overlay_dib_);
    if (video_rgba_tex) SDL_DestroyTexture(video_rgba_tex);'''
assert old in t, "cleanup"
t = t.replace(old, new)

# 7) 绘制：RenderCopyEx → RGBA 转绘 + 圆角几何（外壳叠上）
old = '''                // 屏幕区比例 = 设备原生屏幕 = 镜像画面比例，1:1 填充，
                // 无需 cover 裁剪；旋转由 RenderCopyEx 的角度完成。
                SDL_RenderCopyEx(renderer, video_tex, nullptr, &hole,
                                 angle, nullptr, SDL_FLIP_NONE);'''
new = '''                // 画面按内容区的圆角形状绘制（三角网格）—— 圆角外的
                // 部分不绘制，外壳的圆角边框完整包住方角画面。NV12/
                // YUV 纹理先转绘到 RGBA 离屏纹理再采样（几何管线采
                // 样不到 UV 平面）。
                if (!video_rgba_tex || video_rgba_w != video_tex_w ||
                    video_rgba_h != video_tex_h) {
                    if (video_rgba_tex) SDL_DestroyTexture(video_rgba_tex);
                    video_rgba_w = video_tex_w;
                    video_rgba_h = video_tex_h;
                    video_rgba_tex = SDL_CreateTexture(
                        renderer, SDL_PIXELFORMAT_ABGR8888,
                        SDL_TEXTUREACCESS_TARGET, video_rgba_w, video_rgba_h);
                    SDL_SetTextureScaleMode(video_rgba_tex, SDL_ScaleModeLinear);
                }
                SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
                if (video_rgba_tex && SDL_SetRenderTarget(renderer, video_rgba_tex) == 0) {
                    SDL_RenderSetViewport(renderer, nullptr);
                    SDL_RenderCopy(renderer, video_tex, nullptr, nullptr);
                    SDL_SetRenderTarget(renderer, prev_target);
                    SDL_RenderSetViewport(renderer, &stage_rect);
                    const SDL_Rect full{0, 0, video_tex_w, video_tex_h};
                    // 圆角半径 = 屏幕孔的真实圆角（≈内容区宽的 3.65%）
                    render_video_rounded(renderer, video_rgba_tex, full, hole,
                                         hole.w * 0.0365f, angle,
                                         static_cast<float>(video_rgba_w),
                                         static_cast<float>(video_rgba_h));
                } else {
                    SDL_RenderCopyEx(renderer, video_tex, nullptr, &hole,
                                     angle, nullptr, SDL_FLIP_NONE);
                }'''
assert old in t, "draw"
t = t.replace(old, new)

p.write_text(t, encoding="utf-8")
print("overlay window migration applied")
