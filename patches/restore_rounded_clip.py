#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""恢复圆角几何裁剪：画面以 SDL_RenderGeometry 按内容区圆角矩形绘制
（向量级裁剪），NV12 先转绘 RGBA 离屏纹理。"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# 1) 恢复 render_video_rounded（放在 ModelAlias 表之前）
old = "// Apple 设备标识符 → Bezel 素材分组用的营销名。"
new = '''// 把视频纹理以「圆角矩形」形状绘制到目标区域（SDL_RenderGeometry 三
// 角扇）。圆角外的画面不参与绘制 —— 外壳的圆角边框才能完整包住方角
// 画面（等价于 HTML 方案里 .phone-screen 的 border-radius 裁剪）。
// angle 语义与 SDL_RenderCopyEx 的顺时针角度一致（0/90/180/270），
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
assert old in t, "rounded fn"
t = t.replace(old, new, 1)

# 2) 成员：RGBA 中间纹理
old = '''    bool layered_on = false;'''
new = '''    bool layered_on = false;
    // 圆角绘制用的 RGBA 中间纹理：NV12/YUV 视频纹理不能直接走
    // SDL_RenderGeometry（D3D11 后端的几何管线采样不到 UV 平面），
    // 每帧先把当前帧转绘到这张离屏纹理，再对它做圆角网格采样。
    SDL_Texture* video_rgba_tex = nullptr;
    int video_rgba_w = 0, video_rgba_h = 0;'''
assert old in t, "members"
t = t.replace(old, new, 1)

# 3) 绘制：RenderCopyEx → RGBA 转绘 + 圆角几何
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
t = t.replace(old, new, 1)

# 4) 退出清理
old = '''    if (layered_readback) SDL_DestroyTexture(layered_readback);
    if (layered_dib)   DeleteObject(layered_dib);'''
new = '''    if (layered_readback) SDL_DestroyTexture(layered_readback);
    if (layered_dib)   DeleteObject(layered_dib);
    if (video_rgba_tex) SDL_DestroyTexture(video_rgba_tex);'''
assert old in t, "cleanup"
t = t.replace(old, new, 1)

p.write_text(t, encoding="utf-8")
print("rounded clip restored")
