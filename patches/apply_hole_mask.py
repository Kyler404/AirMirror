#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""孔形掩码：画面 alpha 逐像素裁成屏幕孔的精确形状（洪泛填充计算），
边框 PNG 不再涂任何遮罩 —— 四角由边框自身的圆角材质呈现。"""
from pathlib import Path

p = Path(__file__).resolve().parent.parent / "client-src" / "src" / "video" / "video_renderer.cpp"
t = p.read_text(encoding="utf-8")

# 1) DeviceFrame 增加掩码字段
old = """struct DeviceFrame {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;                    // 纹理尺寸
    // 屏幕内容区在整张边框图里的归一化矩形（0..1）
    float hx0 = 0.f, hy0 = 0.f, hx1 = 1.f, hy1 = 1.f;
};"""
new = """struct DeviceFrame {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;                    // 纹理尺寸
    // 屏幕内容区在整张边框图里的归一化矩形（0..1）
    float hx0 = 0.f, hy0 = 0.f, hx1 = 1.f, hy1 = 1.f;
    // 屏幕孔形状掩码（白 + alpha=孔内255/孔外0，尺寸=屏幕区）：绘制时
    // 把画面 alpha 逐像素裁成孔形 —— 与素材圆角严丝合缝，边框零修改。
    SDL_Texture* mask = nullptr;
    int mw = 0, mh = 0;
};"""
assert old in t, "A struct"
t = t.replace(old, new)

# 2) 掩码混合模式（替换 render_video_rounded 函数的位置）
old = """// 把视频纹理以「圆角矩形」形状绘制到目标区域（SDL_RenderGeometry 三
// 角扇）。圆角外的画面不参与绘制 —— 外壳的圆角边框才能完整包住方角
// 画面。angle 语义与 SDL_RenderCopyEx 的顺时针角度一致（0/90/180/270），
// 旋转通过 UV 映射实现，顶点直接落在最终的可见区域上。
static void render_video_rounded(SDL_Renderer* renderer, SDL_Texture* tex,
                                 const SDL_Rect& src, const SDL_Rect& hole,
                                 float radius, int angle,
                                 float tex_w, float tex_h) {"""
idx = t.find(old)
assert idx > 0, "B fn head"
end = t.find("\n}\n", idx)
assert end > idx, "B fn end"
t = t[:idx] + """// 掩码混合：颜色不变，alpha = 掩码.a × 目标.a —— 把已绘制的画面
// 逐像素裁成屏幕孔的形状（含圆角），与素材严丝合缝。
static const SDL_BlendMode kMaskAlphaMul = SDL_ComposeCustomBlendMode(
    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLEND_OPERATION_ADD,
    SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLEND_OPERATION_ADD);
""" + t[end + 3:]

# 3) load_frame_texture：删斜切遮罩，加洪泛孔形掩码
old = """    // 屏幕区四角是 45° 斜切（素材实测：孔边缘点全部落在 x+y=常数 的
    // 直线上，是直边倒角而非圆弧）：斜切线外、仍属画面矩形的像素填
    // 成不透明黑 —— 与素材黑色边框融为一体，方角画面的四个角由此被
    // 遮住；斜切线内正常显示画面。遮罩形状必须与素材一致，否则中间
    // 会留下楔形缝隙（圆弧遮罩配斜切孔=漏光）。
    const int cham = static_cast<int>(sw0 * 0.037f);
    auto fill_corner = [&](int x0, int y0, int sx, int sy) {
        for (int i = 0; i <= cham; ++i)
            for (int j = 0; j <= cham - i; ++j) {
                const int x = x0 + sx * i, y = y0 + sy * j;
                if (x < 0 || x >= w || y < 0 || y >= h) continue;
                auto* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
                p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
            }
    };
    fill_corner(sx0,       sy0,       +1, +1);
    fill_corner(sx0 + sw0, sy0,       -1, +1);
    fill_corner(sx0,       sy0 + sh0, +1, -1);
    fill_corner(sx0 + sw0, sy0 + sh0, -1, -1);
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,"""
new = """    // 屏幕孔形状掩码：从图心洪泛填充（只走 alpha<32 的透明像素），
    // 孔被不透明机身封闭，得到孔的精确形状（含圆角）。绘制时把画面
    // alpha 逐像素裁成孔形 —— 边框 PNG 不做任何修改，四角由边框自身
    // 的圆角材质呈现（此前往边框上涂黑三角的方案全部废弃：形状永远
    // 对不上，留裁剪痕迹）。
    std::vector<unsigned char> inside(static_cast<std::size_t>(w) * h, 0);
    {
        std::vector<int> stack;
        auto push = [&](int x, int y) {
            if (x < 0 || x >= w || y < 0 || y >= h) return;
            const int i = y * w + x;
            if (!inside[i] && alpha(x, y) < 32) { inside[i] = 1; stack.push_back(i); }
        };
        push(w / 2, h / 2);
        while (!stack.empty()) {
            const int i = stack.back(); stack.pop_back();
            const int x = i % w, y = i / w;
            push(x - 1, y); push(x + 1, y); push(x, y - 1); push(x, y + 1);
        }
    }
    if (sw0 > 0 && sh0 > 0) {
        std::vector<unsigned char> mask(static_cast<std::size_t>(sw0) * sh0 * 4);
        for (int y = 0; y < sh0; ++y)
            for (int x = 0; x < sw0; ++x) {
                auto* q = &mask[(static_cast<std::size_t>(y) * sw0 + x) * 4];
                q[0] = q[1] = q[2] = 255;
                q[3] = inside[(static_cast<std::size_t>(sy0 + y) * w + (sx0 + x))] ? 255 : 0;
            }
        SDL_Texture* mtex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                              SDL_TEXTUREACCESS_STATIC, sw0, sh0);
        if (mtex && SDL_UpdateTexture(mtex, nullptr, mask.data(), sw0 * 4) == 0) {
            out.mask = mtex; out.mw = sw0; out.mh = sh0;
        } else if (mtex) {
            SDL_DestroyTexture(mtex);
        }
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,"""
assert old in t, "C load"
t = t.replace(old, new)

# 4) 绘制：掩码乘 alpha + RenderCopyEx（替换几何网格调用）
old = """                SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
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
                }"""
new = """                // 画面 → RGBA 离屏纹理（NV12 不能直接做 alpha 运算），
                // 再用孔形掩码把 alpha 逐像素裁成屏幕孔的形状（掩码随
                // 画面同角度旋转，圆角与素材严丝合缝），最后绘入孔位。
                SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
                if (video_rgba_tex && SDL_SetRenderTarget(renderer, video_rgba_tex) == 0) {
                    SDL_RenderSetViewport(renderer, nullptr);
                    SDL_RenderCopy(renderer, video_tex, nullptr, nullptr);
                    if (shell.mask) {
                        SDL_SetTextureBlendMode(shell.mask, kMaskAlphaMul);
                        SDL_RenderCopyEx(renderer, shell.mask, nullptr, nullptr,
                                         angle, nullptr, SDL_FLIP_NONE);
                    }
                    SDL_SetRenderTarget(renderer, prev_target);
                    SDL_RenderSetViewport(renderer, &stage_rect);
                    SDL_SetTextureBlendMode(video_rgba_tex, SDL_BLENDMODE_BLEND);
                    SDL_RenderCopyEx(renderer, video_rgba_tex, nullptr, &hole,
                                     angle, nullptr, SDL_FLIP_NONE);
                } else {
                    SDL_RenderCopyEx(renderer, video_tex, nullptr, &hole,
                                     angle, nullptr, SDL_FLIP_NONE);
                }"""
assert old in t, "D draw"
t = t.replace(old, new)

# 5) 重载时销毁掩码
old = """            if (frame_portrait.tex) { SDL_DestroyTexture(frame_portrait.tex); frame_portrait = DeviceFrame{}; }
            if (frame_landscape.tex) { SDL_DestroyTexture(frame_landscape.tex); frame_landscape = DeviceFrame{}; }"""
new = """            if (frame_portrait.tex) SDL_DestroyTexture(frame_portrait.tex);
            if (frame_portrait.mask) SDL_DestroyTexture(frame_portrait.mask);
            frame_portrait = DeviceFrame{};
            if (frame_landscape.tex) SDL_DestroyTexture(frame_landscape.tex);
            if (frame_landscape.mask) SDL_DestroyTexture(frame_landscape.mask);
            frame_landscape = DeviceFrame{};"""
assert old in t, "E reload"
t = t.replace(old, new)

p.write_text(t, encoding="utf-8")
print("hole-mask applied")
