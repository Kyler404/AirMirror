#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""窗口区域（SetWindowRgn）方案：H 模式把主窗口裁成机身精确轮廓。
删除整套覆盖窗口 + ULW 机制（本机 ULW 被系统性禁用，gle=31）。"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# A) 覆盖窗口成员 → 区域缓存 + 机身轮廓存储
old_start = t.find("    // ---- 隐藏界面：独立分层覆盖窗口")
old_end = t.find("overlay_last_main_w_ = 0, overlay_last_main_h_ = 0;")
assert old_start > 0 and old_end > old_start
old_end = t.find("\n", old_end)
t = t[:old_start] + """    // ---- 隐藏界面：窗口区域（SetWindowRgn）------------------------
    // 按机身轮廓把主窗口裁成手机形状：区域外不属于窗口 → 桌面直接透出。
    // （ULW/分层窗口在本机被系统性禁用 gle=31，色键/blur-behind 在
    // 交换链窗口上一律无效——全部废弃。）
    HRGN body_rgn_ = nullptr;
    int body_rgn_key_w_ = 0, body_rgn_key_h_ = 0, body_rgn_key_style_ = -9;
""" + t[old_end:]

# B) DeviceFrame 增加逐行机身轮廓
old = """    // 屏幕孔形状掩码（白 + alpha=孔内255/孔外0，尺寸=屏幕区）：绘制时
    // 把画面 alpha 逐像素裁成孔形 —— 与素材圆角严丝合缝，边框零修改。
    SDL_Texture* mask = nullptr;
    int mw = 0, mh = 0;
};"""
new = """    // 屏幕孔形状掩码（白 + alpha=孔内255/孔外0，尺寸=屏幕区）：绘制时
    // 把画面 alpha 逐像素裁成孔形 —— 与素材圆角严丝合缝，边框零修改。
    SDL_Texture* mask = nullptr;
    int mw = 0, mh = 0;
    // 机身轮廓（每纹理行的不透明 x 范围，含侧键凸起）：窗口区域用。
    std::vector<int> row_x0, row_x1;
};"""
assert old in t, "B"
t = t.replace(old, new)

# C) load_frame_texture：填轮廓
old = """    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return;
    if (SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4) != 0) {
        SDL_DestroyTexture(tex); return;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    out.tex = tex; out.w = w; out.h = h;"""
new = """    // 机身轮廓：每行首个/末个不透明像素（含侧键凸起与圆角）——
    // 隐藏界面下按此把窗口裁成手机形状（SetWindowRgn）。
    out.row_x0.assign(h, -1);
    out.row_x1.assign(h, -1);
    for (int y = 0; y < h; ++y) {
        int x0 = -1, x1 = -1;
        for (int x = 0; x < w; ++x)
            if (alpha(x, y) >= 250) { if (x0 < 0) x0 = x; x1 = x; }
        out.row_x0[y] = x0;
        out.row_x1[y] = x1;
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return;
    if (SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4) != 0) {
        SDL_DestroyTexture(tex); return;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    out.tex = tex; out.w = w; out.h = h;"""
assert old in t, "C"
t = t.replace(old, new)

# D) 覆盖窗口生命周期块 → 区域应用/清除
old_start = t.find("        // ---- 分层覆盖窗口生命周期（隐藏界面）")
old_end = t.find("        // Honor live VSYNC toggle")
assert old_start > 0 and old_end > old_start
t = t[:old_start] + """        // ---- 窗口区域（隐藏界面）：把主窗口裁成机身精确轮廓 ------
        // 轮廓逐行来自素材的不透明范围（含侧键/圆角），区域外不属于
        // 窗口 → 桌面直接透出。尺寸/样式变化时重建；退出时清除。
#ifdef _WIN32
        {
            SDL_SysWMinfo wm;
            SDL_VERSION(&wm.version);
            const bool have = SDL_GetWindowWMInfo(window, &wm) != 0;
            const DeviceFrame* shell = nullptr;
            if (mirror_shell_available()) shell = current_mirror_shell();
            const bool want = ui.chromeless && !fullscreen && shell && shell->tex;
            if (!want) {
                if (body_rgn_) {
                    SetWindowRgn(wm.info.win.window, nullptr, TRUE);
                    body_rgn_ = nullptr;
                    body_rgn_key_style_ = -9;
                }
            } else if (have) {
                int ww = 0, wh = 0;
                SDL_GetWindowSize(window, &ww, &wh);
                const int style_key = frame_loaded_style;
                if (ww != body_rgn_key_w_ || wh != body_rgn_key_h_ ||
                    style_key != body_rgn_key_style_) {
                    if (body_rgn_) { DeleteObject(body_rgn_); body_rgn_ = nullptr; }
                    HRGN rgn = CreateRectRgn(0, 0, 0, 0);
                    const int th = static_cast<int>(shell->row_x0.size());
                    for (int y = 0; y < wh && th > 0; ++y) {
                        const int ty = y * th / wh;
                        const int rx0 = shell->row_x0[ty];
                        if (rx0 < 0) continue;
                        const int wx0 = rx0 * ww / shell->w;
                        const int wx1 = shell->row_x1[ty] * ww / shell->w;
                        if (wx1 <= wx0) continue;
                        HRGN row = CreateRectRgn(wx0, y, wx1 + 1, y + 1);
                        CombineRgn(rgn, rgn, row, RGN_OR);
                        DeleteObject(row);
                    }
                    SetWindowRgn(wm.info.win.window, rgn, TRUE);
                    body_rgn_ = rgn;
                    body_rgn_key_w_ = ww; body_rgn_key_h_ = wh;
                    body_rgn_key_style_ = style_key;
                }
            }
        }
#endif

""" + t[old_end:]

# E) 删除帧尾 ULW 读回块
old_start = t.find("        // 隐藏界面：读回后备缓冲 → 预乘 alpha → UpdateLayeredWindow")
old_end = t.find("        SDL_RenderPresent(renderer);", old_start)
assert old_start > 0 and old_end > old_start
t = t[:old_start] + t[old_end:]

# F) shell 获取辅助（区域块引用）：直接内联取，删掉对不存在函数的引用
old = """            const bool have = SDL_GetWindowWMInfo(window, &wm) != 0;
            const DeviceFrame* shell = nullptr;
            if (mirror_shell_available()) shell = current_mirror_shell();
            const bool want = ui.chromeless && !fullscreen && shell && shell->tex;"""
new = """            const bool have = SDL_GetWindowWMInfo(window, &wm) != 0;
            const DeviceFrame* shell = &(display_width() >= display_height()
                                         ? frame_landscape : frame_portrait);
            const bool want = ui.chromeless && !fullscreen && have && shell->tex;"""
assert old in t, "F"
t = t.replace(old, new)

# G) 清理：覆盖窗口销毁 → 区域删除
old = """    if (overlay_window_) SDL_DestroyWindow(overlay_window_);
    if (overlay_dib_)  DeleteObject(overlay_dib_);"""
new = """    if (body_rgn_) DeleteObject(body_rgn_);"""
assert old in t, "G"
t = t.replace(old, new)

# H) 过渡块中的主窗口隐藏/焦点转移残留检查（overlay 创建块已随 D 删除；
#    焦点转移块独立存在则删）
focus_start = t.find("                // 焦点转移到覆盖窗口：否则隐藏主窗口后焦点落到其他")
if focus_start > 0:
    focus_end = t.find("SDL_SetWindowInputFocus(overlay_window_);", focus_start)
    focus_end = t.find("\n", focus_end)
    t = t[:focus_start] + t[focus_end + 1:]

p.write_text(t, encoding="utf-8")
print("window-region migration applied")
