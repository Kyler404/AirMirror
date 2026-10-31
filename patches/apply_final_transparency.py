#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""最终透明方案迁移：
1. 删除加载时的洪泛填键色（ULW 的 alpha 通道天然处理透明+投影）
2. 恢复 render_video_rounded（圆角几何裁剪，半径=屏幕孔真实圆角 3.65%）
3. 恢复 RGBA 中间纹理（NV12 不能直接走几何管线）
4. 绘制改为：视频转绘 RGBA → 圆角几何 → 边框叠上
"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# 1) 删除洪泛填键色块
old = '''    // 机身外的一切像素（透明背景 + 半透明投影）填成不透明「色键」
    // (1,2,3)：隐藏界面模式下窗口对该颜色做透明，手机直接浮在桌面
    // 上，四角和边缘都不露出窗口背景。屏幕开口被不透明边框封闭，
    // 洪泛从图像边缘出发不会进入开口，画面照常显示。
    {
        const unsigned char key[4] = {1, 2, 3, 255};
        std::vector<unsigned char> pass(w * h, 0);
        std::vector<int> stack;
        auto seed = [&](int x, int y) {
            if (x < 0 || x >= w || y < 0 || y >= h) return;
            const int i = y * w + x;
            if (!pass[i] && alpha(x, y) < 250) { pass[i] = 1; stack.push_back(i); }
        };
        for (int x = 0; x < w; ++x) { seed(x, 0); seed(x, h - 1); }
        for (int y = 0; y < h; ++y) { seed(0, y); seed(w - 1, y); }
        while (!stack.empty()) {
            const int i = stack.back(); stack.pop_back();
            const int x = i % w, y = i / w;
            seed(x - 1, y); seed(x + 1, y); seed(x, y - 1); seed(x, y + 1);
        }
        for (int i = 0; i < w * h; ++i)
            if (pass[i]) {
                rgba[i * 4 + 0] = key[0]; rgba[i * 4 + 1] = key[1];
                rgba[i * 4 + 2] = key[2]; rgba[i * 4 + 3] = key[3];
            }
    }
    // 屏幕内容区：来自构建期清单'''
new = '''    // 屏幕内容区：来自构建期清单'''
assert old in t, "A flood fill"
t = t.replace(old, new)

# 2) DeviceFrame 清理已完成；此处确认无需改动

# 3) 恢复 render_video_rounded（放在 ModelAlias 表之前）
old = '''// Apple 设备标识符 → Bezel 素材分组用的营销名。'''
new = '''// 把视频纹理以「圆角矩形」形状绘制到目标区域（SDL_RenderGeometry 三
// 角扇）。圆角外的画面不参与绘制 —— 外壳的圆角边框才能完整包住方角
// 画面。angle 语义与 SDL_RenderCopyEx 的顺时针角度一致（0/90/180/270），
// 旋转通过 UV 映射实现，顶点直接落在最终的可见区域上。
static void render_video_rounded(SDL_Renderer* renderer, SDL_Texture* tex,
                                 const SDL_Rect& src, const SDL_Rect& hole,
                                 float radius, int angle,
                                 float tex_w, float tex_h) {
    const int SEG = 12;
    const float x0 = static_cast<float>(hole.x), y0 = static_cast<float>(hole.y);
    const float x1 = x0 + hole.w, y1 = y0 + hole.h;
    const float r = std::min(radius, std::min(hole.w, hole.h) * 0.5f);
    std::vector<SDL_Vertex> v;
    v.reserve(SEG * 4 + 16);
    auto push = [&](float px, float py) {
        const float dx = (px - x0) / (x1 - x0);
        const float dy = (py - y0) / (y1 - y0);
        float nx, ny;
        switch (angle % 360) {               // RenderCopyEx 顺时针角度的逆映射
            case 90:  nx = 1.f - dy; ny = dx;       break;
            case 180: nx = 1.f - dx; ny = 1.f - dy; break;
            case 270: nx = dy;       ny = 1.f - dx; break;
            default:  nx = dx;       ny = dy;       break;
        }
        SDL_Vertex vert;
        vert.position = {px, py};
        vert.color = {255, 255, 255, 255};
        // SDL_RenderGeometry 的 UV 是 0..1 归一化坐标（不是纹素）
        vert.tex_coord = {(src.x + nx * src.w) / tex_w,
                          (src.y + ny * src.h) / tex_h};
        v.push_back(vert);
    };
    push((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);        // 三角扇中心
    push(x0 + r, y0);
    push(x1 - r, y0);
    for (int i = 0; i <= SEG; ++i) {                  // 右上角弧
        const float a = -1.5707964f + 1.5707964f * i / SEG;
        push((x1 - r) + r * std::cos(a), (y0 + r) + r * std::sin(a));
    }
    push(x1, y1 - r);
    for (int i = 0; i <= SEG; ++i) {                  // 右下角弧
        const float a = 1.5707964f * i / SEG;
        push((x1 - r) + r * std::cos(a), (y1 - r) + r * std::sin(a));
    }
    push(x0 + r, y1);
    for (int i = 0; i <= SEG; ++i) {                  // 左下角弧
        const float a = 1.5707964f + 1.5707964f * i / SEG;
        push((x0 + r) + r * std::cos(a), (y1 - r) + r * std::sin(a));
    }
    push(x0, y0 + r);
    for (int i = 0; i <= SEG; ++i) {                  // 左上角弧
        const float a = 3.1415926f + 1.5707964f * i / SEG;
        push((x0 + r) + r * std::cos(a), (y0 + r) + r * std::sin(a));
    }
    for (std::size_t i = 1; i + 1 < v.size(); ++i) {
        const SDL_Vertex tri[3] = {v[0], v[i], v[i + 1]};
        SDL_RenderGeometry(renderer, tex, tri, 3, nullptr, 0);
    }
}

// Apple 设备标识符 → Bezel 素材分组用的营销名。'''
assert old in t, "C rounded fn"
t = t.replace(old, new)

# 4) 成员变量：RGBA 中间纹理
old = '''    // ---- 隐藏界面的分层窗口呈现 ------------------------------------
    // WS_EX_LAYERED + UpdateLayeredWindow：每帧把渲染结果读回、预乘
    // alpha 后交给 DWM —— 机身外像素逐像素透明，手机直接浮在桌面
    // 上（圆角、投影全保留，无色键硬边）。
    SDL_Texture* layered_readback = nullptr;
    int layered_w = 0, layered_h = 0;
    HBITMAP layered_dib = nullptr;
    void* layered_bits = nullptr;
    bool layered_on = false;'''
new = '''    // ---- 隐藏界面的分层窗口呈现 ------------------------------------
    // WS_EX_LAYERED + UpdateLayeredWindow：每帧把渲染结果读回、预乘
    // alpha 后交给 DWM —— 机身外像素逐像素透明，手机直接浮在桌面
    // 上（圆角、投影全保留，无色键硬边）。
    SDL_Texture* layered_readback = nullptr;
    int layered_w = 0, layered_h = 0;
    HBITMAP layered_dib = nullptr;
    void* layered_bits = nullptr;
    bool layered_on = false;
    // 圆角绘制用的 RGBA 中间纹理：NV12/YUV 视频纹理不能直接走
    // SDL_RenderGeometry（D3D11 后端的几何管线采样不到 UV 平面），
    // 每帧先把当前帧转绘到这张离屏纹理，再对它做圆角网格采样。
    SDL_Texture* video_rgba_tex = nullptr;
    int video_rgba_w = 0, video_rgba_h = 0;'''
assert old in t, "members"
t = t.replace(old, new)

# 5) 绘制：RenderCopyEx → RGBA 转绘 + 圆角几何
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

# 6) 退出清理
old = '''    if (layered_readback) SDL_DestroyTexture(layered_readback);
    if (layered_dib)   DeleteObject(layered_dib);'''
new = '''    if (layered_readback) SDL_DestroyTexture(layered_readback);
    if (layered_dib)   DeleteObject(layered_dib);
    if (video_rgba_tex) SDL_DestroyTexture(video_rgba_tex);'''
assert old in t, "cleanup"
t = t.replace(old, new)

p.write_text(t, encoding="utf-8")
print("final transparency migration applied")
