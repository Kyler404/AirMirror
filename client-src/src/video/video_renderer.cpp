
#include "video/video_renderer.h"
#include "video/stream_hub.h"
#include "video/video_recorder.h"
#include "airplay/live_settings.h"
#include "airplay/receiver_name.h"
#include "log.h"

#include "video/gpu_video.h"
#include "video/cover_art.h"
#include "video/app_ui/app_ui.h"
#include "platform/single_instance.h"
#include "platform/firewall.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_ttf.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#ifdef HAVE_UI_TESTS
#include "ui/ui_tests.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_exporters.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include "zh_glyph_ranges.h"
#include "frame_manifest.h"
#endif

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

#if defined(_WIN32)
    #include <dwmapi.h>
    #include <shobjidl.h>
    #ifndef DWMWA_SYSTEMBACKDROP_TYPE
        #define DWMWA_SYSTEMBACKDROP_TYPE 38
    #endif
    #ifndef DWMSBT_MAINWINDOW
        #define DWMSBT_MAINWINDOW 2
    #endif
    #ifndef DWMWA_WINDOW_CORNER_PREFERENCE
        #define DWMWA_WINDOW_CORNER_PREFERENCE 33
    #endif
    #ifndef DWMWCP_DONOTROUND
        #define DWMWCP_DONOTROUND 1
    #endif
    #ifndef DWMWCP_DEFAULT
        #define DWMWCP_DEFAULT 0
    #endif
#endif

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

// 边框素材的 PNG 解码用 stb_image（公有领域，单头文件）。vcpkg 的
// ffmpeg 构建里 PNG 解码器是关的（依赖 zlib feature），不值得为它
// 重编 ffmpeg。STB_IMAGE_IMPLEMENTATION 只在本文件定义一次。
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace ap::video {
namespace {

// Keep the stage spacious; the app_ui shell owns all chrome geometry.
constexpr int kDefaultWinWidth  = 1220;
constexpr int kDefaultWinHeight = 800;
struct ResolutionPreset { const char* label; int w; int h; };
constexpr ResolutionPreset kResolutionPresets[] = {
    {"1920 × 1080（全高清）",   1920, 1080},
    {"2560 × 1440（2K，默认）", 2560, 1440},
    {"1920 × 1920（方形上限）", 1920, 1920},
    {"2560 × 2560（方形上限）", 2560, 2560},
    {"2868 × 2868（方形上限）", 2868, 2868},
    {"3840 × 3840（方形上限）", 3840, 3840},
    {"3840 × 2160（4K 超清）",  3840, 2160},
};

#if defined(_WIN32)
// Enable the Windows 11 Mica system backdrop on an SDL-owned window.
// No-op on pre-Win11 hosts (DwmSetWindowAttribute silently ignores
// unknown attribute IDs — the function just returns a non-zero HRESULT).
void enable_mica_backdrop(SDL_Window* w) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(w, &info)) return;
    HWND hwnd = info.info.win.window;
    int backdrop = DWMSBT_MAINWINDOW;
    ::DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                            &backdrop, sizeof(backdrop));
}

// Native Windows folder picker, parented to the SDL application window.
// Returns false for Cancel as well as dialog creation errors; in both cases
// the current path remains untouched.
bool browse_recording_folder(SDL_Window* w, const std::string& initial,
                             std::string& selected) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(w, &info)) return false;

    const HRESULT init_hr = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool should_uninitialize = SUCCEEDED(init_hr);

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr) && dialog) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                           FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"选择录制保存文件夹");

        try {
            const std::wstring initial_w =
                std::filesystem::u8path(initial).wstring();
            IShellItem* initial_item = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(
                    initial_w.c_str(), nullptr, IID_PPV_ARGS(&initial_item)))) {
                dialog->SetFolder(initial_item);
                initial_item->Release();
            }
        } catch (const std::exception&) {
            // Keep the system default folder if the typed path is malformed.
        }

        hr = dialog->Show(info.info.win.window);
        if (SUCCEEDED(hr)) {
            IShellItem* result = nullptr;
            if (SUCCEEDED(dialog->GetResult(&result)) && result) {
                PWSTR path_w = nullptr;
                if (SUCCEEDED(result->GetDisplayName(
                        SIGDN_FILESYSPATH, &path_w)) && path_w) {
                    selected = std::filesystem::path(path_w).u8string();
                    CoTaskMemFree(path_w);
                }
                result->Release();
            }
        }
        dialog->Release();
    }
    if (should_uninitialize) CoUninitialize();
    return !selected.empty();
}
#else
void enable_mica_backdrop(SDL_Window*) {}
#endif

void apply_overlay_theme() {
    app_ui::ApplyTheme(ImGui::GetStyle());
}

void copy_plane(std::vector<unsigned char>& dst,
                const uint8_t* src, int stride,
                int plane_w, int plane_h) {
    dst.resize(static_cast<std::size_t>(plane_w) * plane_h);
    for (int row = 0; row < plane_h; ++row) {
        std::memcpy(dst.data() + static_cast<std::size_t>(row) * plane_w,
                    src + static_cast<std::size_t>(row) * stride,
                    static_cast<std::size_t>(plane_w));
    }
}

} // namespace

namespace {

// Lifted out of run() so the SDL hit-test callback (a C function
// pointer with no captures) can read `chromeless` via callback_data.
// All fields are touched only on the render thread — no atomic
// needed; the callback runs synchronously on the same thread that
// owns SDL events. Page routing / transient inputs live in app_ui.
struct UiState {
    bool chromeless = false;
    bool diagnostics = false;
};

// Both layouts use a custom title bar. Keep native resize handles at
// the edges; only the title bar (or the chromeless stage) is draggable.
SDL_HitTestResult SDLCALL hit_test_cb(SDL_Window* w,
                                      const SDL_Point* p,
                                      void* data) {
    auto* ui = reinterpret_cast<UiState*>(data);
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(w, &win_w, &win_h);
    const Uint32 flags = SDL_GetWindowFlags(w);
    if (flags & SDL_WINDOW_FULLSCREEN) return SDL_HITTEST_NORMAL;
    const int B = ui && ui->chromeless ? 10 : 6;
    const bool resizable = !(flags & SDL_WINDOW_MAXIMIZED);
    const bool top    = resizable && p->y < B;
    const bool bottom = resizable && p->y >= win_h - B;
    const bool left   = resizable && p->x < B;
    const bool right  = resizable && p->x >= win_w - B;
    if (top    && left)  return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top    && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left)  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (top)             return SDL_HITTEST_RESIZE_TOP;
    if (bottom)          return SDL_HITTEST_RESIZE_BOTTOM;
    if (left)            return SDL_HITTEST_RESIZE_LEFT;
    if (right)           return SDL_HITTEST_RESIZE_RIGHT;
    if (ui && (ui->chromeless || (p->y < S(68) && p->x < win_w - S(200))))
        return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}


// Open every font in a priority list so we can fall back per codepoint.
// Primary fonts cover Latin / Cyrillic / Greek (Segoe UI, DejaVu Sans…);
// the CJK fonts cover ideographs + kana + hangul (Microsoft YaHei for
// Simplified Chinese, Meiryo for Japanese, Malgun Gothic for Korean).
std::vector<TTF_Font*> open_font_chain(int pt_size) {
    static const std::array<const char*, 12> candidates = {{
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\segoeui.ttf",   // Latin / Cyrillic / Greek
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\msyh.ttc",      // Chinese Simplified
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",    // Chinese fallback (older)
        "C:\\Windows\\Fonts\\meiryo.ttc",    // Japanese
        "C:\\Windows\\Fonts\\YuGothM.ttc",
        "C:\\Windows\\Fonts\\msgothic.ttc",
        "C:\\Windows\\Fonts\\malgun.ttf",    // Korean
        "C:\\Windows\\Fonts\\seguisym.ttf",  // misc symbols
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
    }};
    std::vector<TTF_Font*> fonts;
    for (const char* path : candidates) {
        if (!path) continue;
        TTF_Font* f = TTF_OpenFont(path, (int)(pt_size * g_ui_scale));
        if (f) {
            fonts.push_back(f);
            LOG_INFO << "VideoRenderer loaded font " << path
                     << " @" << pt_size << "pt";
        }
    }
    if (fonts.empty()) {
        LOG_WARN << "VideoRenderer: no system font found — text overlay disabled";
    }
    return fonts;
}

// Tiny UTF-8 decoder — returns codepoint at `i` and advances `i` past
// the sequence. Returns 0xFFFD (replacement) on malformed input.
uint32_t utf8_next(const std::string& s, std::size_t& i) {
    if (i >= s.size()) return 0;
    unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) { ++i; return c0; }
    uint32_t cp = 0;
    int      n  = 0;
    if      ((c0 & 0xe0) == 0xc0) { cp = c0 & 0x1f; n = 1; }
    else if ((c0 & 0xf0) == 0xe0) { cp = c0 & 0x0f; n = 2; }
    else if ((c0 & 0xf8) == 0xf0) { cp = c0 & 0x07; n = 3; }
    else { ++i; return 0xFFFD; }
    ++i;
    for (int k = 0; k < n; ++k) {
        if (i >= s.size()) return 0xFFFD;
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xc0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (c & 0x3f);
        ++i;
    }
    return cp;
}

// Return the first font in `fonts` that provides the given codepoint,
// or fonts[0] if none of them do (so the missing glyph at least renders
// as .notdef / tofu in a known font).
TTF_Font* pick_font_for(uint32_t cp, const std::vector<TTF_Font*>& fonts) {
    for (TTF_Font* f : fonts) {
        if (TTF_GlyphIsProvided32(f, cp)) return f;
    }
    return fonts.empty() ? nullptr : fonts[0];
}

// Render an arbitrary UTF-8 string across a font chain: split the string
// into runs of codepoints sharing the same font, render each run with
// TTF_RenderUTF8_Blended, then hconcat the surfaces so the output looks
// as if one super-font rendered the whole thing.
SDL_Surface* render_multi_font(const std::vector<TTF_Font*>& fonts,
                               const std::string& utf8,
                               SDL_Color color) {
    if (fonts.empty() || utf8.empty()) return nullptr;

    // Build runs: [font_for_this_substring, utf8_substring]
    struct Run { TTF_Font* font; std::string text; };
    std::vector<Run> runs;

    TTF_Font*   cur_font = nullptr;
    std::string cur_text;
    for (std::size_t i = 0; i < utf8.size(); ) {
        std::size_t start = i;
        uint32_t    cp    = utf8_next(utf8, i);
        TTF_Font*   f     = pick_font_for(cp, fonts);
        if (f != cur_font) {
            if (!cur_text.empty()) runs.push_back({cur_font, std::move(cur_text)});
            cur_text.clear();
            cur_font = f;
        }
        cur_text.append(utf8, start, i - start);
    }
    if (!cur_text.empty()) runs.push_back({cur_font, std::move(cur_text)});

    // Fast path: only one run, just use TTF directly.
    if (runs.size() == 1 && runs[0].font) {
        return TTF_RenderUTF8_Blended(runs[0].font, runs[0].text.c_str(), color);
    }

    // Slow path: render each run, then hconcat.
    std::vector<SDL_Surface*> surfs;
    int total_w = 0, max_h = 0;
    for (const auto& r : runs) {
        if (!r.font) { surfs.push_back(nullptr); continue; }
        SDL_Surface* s = TTF_RenderUTF8_Blended(r.font, r.text.c_str(), color);
        surfs.push_back(s);
        if (s) { total_w += s->w; max_h = std::max(max_h, s->h); }
    }
    if (total_w == 0) {
        for (auto* s : surfs) if (s) SDL_FreeSurface(s);
        return nullptr;
    }

    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(
        0, total_w, max_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!out) {
        for (auto* s : surfs) if (s) SDL_FreeSurface(s);
        return nullptr;
    }
    SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_NONE);

    int x = 0;
    for (SDL_Surface* s : surfs) {
        if (!s) continue;
        SDL_Rect dst{ x, max_h - s->h, s->w, s->h };  // baseline-bottom align
        SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(s, nullptr, out, &dst);
        x += s->w;
        SDL_FreeSurface(s);
    }
    return out;
}

// Draw a filled rect into a target rect of (win_w, win_h) such that the
// source aspect is preserved (letterbox / pillarbox).
SDL_Rect fit_inside(int src_w, int src_h, int win_w, int win_h) {
    SDL_Rect r{0, 0, win_w, win_h};
    if (src_w <= 0 || src_h <= 0) return r;
    const double src_ar = static_cast<double>(src_w) / src_h;
    const double win_ar = static_cast<double>(win_w) / win_h;
    if (src_ar > win_ar) {
        r.w = win_w;
        r.h = static_cast<int>(win_w / src_ar);
        r.x = 0;
        r.y = (win_h - r.h) / 2;
    } else {
        r.h = win_h;
        r.w = static_cast<int>(win_h * src_ar);
        r.x = (win_w - r.w) / 2;
        r.y = 0;
    }
    return r;
}

// ---- 投屏边框（设备外壳）资源 --------------------------------------
// assets/frame/<名字> - Portrait.png / <名字> - Landscape.png。
// 运行时枚举目录，PNG 经 ffmpeg 解码为 RGBA 纹理；屏幕孔的位置在
// 加载时按 alpha 通道自动测量：从中心向四边扫描透明区边界，再用
// 对称性修正灵动岛覆盖层和底部反光造成的截断（Bezel 素材通用）。
struct DeviceFrameSource {
    std::string label;                  // 去掉朝向后缀的文件名
    std::filesystem::path portrait, landscape;
};
struct DeviceFrame {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;                    // 纹理尺寸
    // 屏幕内容区在整张边框图里的归一化矩形（0..1）
    float hx0 = 0.f, hy0 = 0.f, hx1 = 1.f, hy1 = 1.f;
    // 屏幕孔形状掩码（白 + alpha=孔内255/孔外0，尺寸=屏幕区）：绘制时
    // 把画面 alpha 逐像素裁成孔形 —— 与素材圆角严丝合缝，边框零修改。
    SDL_Texture* mask = nullptr;
    int mw = 0, mh = 0;
    // 机身轮廓（每纹理行的不透明 x 范围，含侧键凸起）：窗口区域用。
    std::vector<int> row_x0, row_x1;
};

static std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                      static_cast<int>(text.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

// 解码图片为 RGBA（stb_image；_wfopen 走原生宽路径，避免代码页问题）。
static bool decode_image_rgba(const std::filesystem::path& path,
                              std::vector<unsigned char>& rgba, int& w, int& h) {
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) return false;
    int channels = 0;
    unsigned char* data = stbi_load_from_file(fp, &w, &h, &channels, 4);
    fclose(fp);
    if (!data || w <= 0 || h <= 0) { if (data) stbi_image_free(data); return false; }
    rgba.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return true;
}

static std::vector<DeviceFrameSource> scan_frame_sources(const std::filesystem::path& dir) {
    std::vector<DeviceFrameSource> out;
    std::error_code ec;
    std::map<std::string, DeviceFrameSource> grouped;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        std::string ext = path.extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".png") continue;
        std::string stem = path.stem().string();
        if (stem.size() > 12 && stem.compare(stem.size() - 12, 12, " - Landscape") == 0) {
            grouped[stem.substr(0, stem.size() - 12)].landscape = path;
        } else if (stem.size() > 11 && stem.compare(stem.size() - 11, 11, " - Portrait") == 0) {
            grouped[stem.substr(0, stem.size() - 11)].portrait = path;
        }
    }
    for (auto& [label, src] : grouped) {
        src.label = label;
        out.push_back(src);
    }
    return out;
}

// 测量屏幕孔 + 建 RGBA 纹理。失败返回 tex==nullptr。
static void load_frame_texture(SDL_Renderer* renderer, const std::filesystem::path& path,
                               DeviceFrame& out) {
    out = DeviceFrame{};
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!decode_image_rgba(path, rgba, w, h) || w <= 0 || h <= 0) return;
    auto alpha = [&](int x, int y) {
        return rgba[(static_cast<size_t>(y) * w + x) * 4 + 3];
    };
    // 屏幕内容区：来自构建期清单（patches/gen_frame_manifest.py 用
    // 洪泛填充精确测量每张素材，数值=设备原生屏幕分辨率）。查不到的
    // 素材退回固定比例。
    const FrameScreenRect* m = find_frame_screen(path.stem().string().c_str());
    int sx0, sy0, sw0, sh0;
    if (m) {
        sx0 = m->sx; sy0 = m->sy; sw0 = m->sw; sh0 = m->sh;
    } else {
        LOG_WARN << "no frame manifest entry: " << path.stem().string();
        sx0 = static_cast<int>(w * 0.053333f);
        sy0 = static_cast<int>(h * 0.025f);
        sw0 = static_cast<int>(w * 0.893333f);
        sh0 = static_cast<int>(h * 0.95f);
    }
    // 屏幕孔形状掩码：从图心洪泛填充（只走 alpha<32 的透明像素），
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
    // 机身轮廓：每行首个/末个不透明像素（含侧键凸起与圆角）——
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
    out.tex = tex; out.w = w; out.h = h;
    // 内容区 = 清单测量的屏幕区（全图归一化）
    out.hx0 = static_cast<float>(sx0) / w;
    out.hx1 = static_cast<float>(sx0 + sw0) / w;
    out.hy0 = static_cast<float>(sy0) / h;
    out.hy1 = static_cast<float>(sy0 + sh0) / h;
}


// ---- 系统热键线程 ---------------------------------------------------
// WM_HOTKEY 是线程消息（hwnd=NULL），SDL 的消息泵会吞掉不转发——
// 因此用独立线程：自建消息窗口 + RegisterHotKey + 自管 GetMessage，
// 收到热键后用线程安全的 SDL_PushEvent 发 SDL_USEREVENT 给主循环。
struct HotkeyThread {
    enum { kAltH = 1, kAltPlus = 2, kAltMinus = 3, kAltShiftPlus = 4 };
    std::thread worker;
    std::atomic<bool> stop{false};
    DWORD thread_id = 0;
    static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
        return DefWindowProcW(h, m, w, l);
    }
    static DWORD WINAPI run(LPVOID param) {
        auto* self = static_cast<HotkeyThread*>(param);
        WNDCLASSW wc = {};
        wc.lpfnWndProc = wndproc;
        wc.lpszClassName = L"airplay_hotkey";
        RegisterClassW(&wc);
        HWND hwnd = CreateWindowExW(0, L"airplay_hotkey", L"", 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, nullptr, nullptr, nullptr);
        if (!hwnd) return 0;
        const BOOL reg1 = RegisterHotKey(hwnd, kAltH,     MOD_ALT | MOD_NOREPEAT, 0x48);  // H
        const BOOL reg2 = RegisterHotKey(hwnd, kAltPlus,  MOD_ALT | MOD_NOREPEAT, 0x6B);  // 小键盘 +
        const BOOL reg4 = RegisterHotKey(hwnd, kAltShiftPlus, MOD_ALT | MOD_NOREPEAT, 0xBB);  // 主键盘 +/=
        const BOOL reg3 = RegisterHotKey(hwnd, kAltMinus, MOD_ALT | MOD_NOREPEAT, 0x6D);  // 小键盘 -
        RegisterHotKey(hwnd, 5, MOD_ALT | MOD_NOREPEAT, 0xBD);  // 主键盘 -
        {
            char reg[64];
            std::snprintf(reg, sizeof(reg), "hotkey reg H=%d NP+=%d NM-=%d MK+=%d",
                          (int)(reg1 != 0), (int)(reg2 != 0),
                          (int)(reg3 != 0), (int)(reg4 != 0));
            LOG_WARN << reg;
        }
        MSG msg;
        while (!self->stop.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_HOTKEY) {
                SDL_Event e;
                SDL_zero(e);
                e.type = SDL_USEREVENT;
                e.user.code = static_cast<int>(msg.wParam);
                SDL_PushEvent(&e);
            }
        }
        UnregisterHotKey(hwnd, kAltH);
        UnregisterHotKey(hwnd, kAltPlus);
        UnregisterHotKey(hwnd, kAltMinus);
        UnregisterHotKey(hwnd, kAltShiftPlus);
        UnregisterHotKey(hwnd, 5);
        DestroyWindow(hwnd);
        return 0;
    }
    void start() {
        worker = std::thread([this] {
            thread_id = GetCurrentThreadId();
            run(this);
        });
    }
    void shutdown() {
        stop.store(true);
        if (thread_id) PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
        if (worker.joinable()) worker.join();
    }
};

// 掩码混合：颜色不变，alpha = 掩码.a × 目标.a —— 把已绘制的画面
// 逐像素裁成屏幕孔的形状（含圆角），与素材严丝合缝。
static const SDL_BlendMode kMaskAlphaMul = SDL_ComposeCustomBlendMode(
    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
    SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);

// Apple 设备标识符 → Bezel 素材分组用的营销名。AirPlay SETUP plist 的
// "model" 字段（如 "iPhone17,2"）经此映射到 assets/frame 的分组前缀。
// 小写比较；新增机型往表里加一行即可。2026 代 iPhone 与 iPad 的
// 标识符按 Apple 惯例推算，若有出入以日志里的 model 为准修正。
struct ModelAlias { const char* identifier; const char* marketing; };
static const ModelAlias kModelAliases[] = {
    {"iphone19,2", "iPhone 18 Pro Max"},
    {"iphone19,1", "iPhone 18 Pro"},
    {"iphone18,2", "iPhone 17 Pro Max"},
    {"iphone18,1", "iPhone 17 Pro"},
    {"iphone18,3", "iPhone 17"},
    {"iphone18,5", "iPhone Air"},
    {"iphone17,2", "iPhone 16 Pro Max"},
    {"iphone17,1", "iPhone 16 Pro"},
    {"iphone17,3", "iPhone 16"},
    {"iphone17,4", "iPhone 16 Plus"},
    {"iphone17,5", "iPhone 16e"},
    {"iphone16,2", "iPhone 15 Pro Max"},
    {"iphone16,1", "iPhone 15 Pro"},
    {"iphone15,5", "iPhone 15 Plus"},
    {"iphone15,4", "iPhone 15"},
    {"ipad18,4",   "iPad Air 13_ (M4)"},
    {"ipad18,3",   "iPad Air 11_ (M4)"},
    {"ipad17,4",   "iPad Pro (M5) 13_"},
    {"ipad17,3",   "iPad Pro (M5) 11_"},
    {"ipad15,6",   "iPad (A16)"},
    {"ipad15,5",   "iPad (A16)"},
    {"ipad16,1",   "iPad mini (A17 Pro)"},
};

// 在素材分组里找型号对应的壳：先沿用上一个壳的颜色，没有同色再取
// 该型号的第一个分组；型号未知或无素材时返回 -1。前缀匹配带词界，
// 避免「iPhone 17 Pro」误吞「iPhone 17 Pro Max」。
static int match_frame_for_model(const std::vector<DeviceFrameSource>& sources,
                                 const std::string& model,
                                 const std::string& previous_label) {
    std::string ident;
    for (char c : model) ident += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const char* marketing = nullptr;
    for (const auto& alias : kModelAliases)
        if (ident == alias.identifier) { marketing = alias.marketing; break; }
    if (!marketing) return -1;
    const std::string want(marketing);
    const auto matches = [&](const std::string& label) {
        if (label.size() < want.size()) return false;
        if (!std::equal(want.begin(), want.end(), label.begin(),
                        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                              std::tolower(static_cast<unsigned char>(b)); })) return false;
        return label.size() == want.size() || label.compare(want.size(), 3, " - ") == 0;
    };
    std::string color;
    {
        const size_t cs = previous_label.find(" - ");
        if (cs != std::string::npos) color = previous_label.substr(cs + 3);
    }
    int first_match = -1, same_color = -1;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (!matches(sources[i].label)) continue;
        if (first_match < 0) first_match = static_cast<int>(i);
        if (same_color < 0 && !color.empty()) {
            const size_t cs = sources[i].label.find(" - ");
            if (cs != std::string::npos &&
                sources[i].label.compare(cs + 3, color.size(), color) == 0)
                same_color = static_cast<int>(i);
        }
    }
    return same_color >= 0 ? same_color : first_match;
}

} // namespace


// ---------------------------------------------------------------------------
// Data plane: every producer entry point forwards to the shared StreamHub.
// The SDL thread (run()) and the Qt front end both consume the same hub.
// ---------------------------------------------------------------------------
VideoRenderer::VideoRenderer() : hub_(std::make_unique<StreamHub>()) {}
VideoRenderer::~VideoRenderer() {
    stop();
    if (hub_) hub_->recorder()->stop();
}

StreamHub& VideoRenderer::hub() { return *hub_; }

AVBufferRef* VideoRenderer::gpu_device_ref() {
    return hub_->gpu_device_ref();
}

bool VideoRenderer::start(const std::string& title) {
    running_ = true;
    thread_  = std::thread(&VideoRenderer::run, this, title);
    return true;
}

void VideoRenderer::stop() {
    running_.store(false);
    if (hub_) hub_->recorder()->stop();
    if (thread_.joinable()) thread_.join();
}

void VideoRenderer::push_cover_art(const uint8_t* jpeg, std::size_t size)
    { hub_->push_cover_art(jpeg, size); }
void VideoRenderer::push_progress(uint32_t elapsed_ms, uint32_t total_ms)
    { hub_->push_progress(elapsed_ms, total_ms); }
void VideoRenderer::push_playback_rate(float rate)
    { hub_->push_playback_rate(rate); }
void VideoRenderer::note_flush()
    { hub_->note_flush(); }
bool VideoRenderer::in_flush_grace() const
    { return hub_->in_flush_grace(); }
void VideoRenderer::clear_session()
    { hub_->clear_session(); }
void VideoRenderer::set_live_settings(ap::airplay::LiveSettings* live)
    { hub_->set_live_settings(live); }
void VideoRenderer::push_avframe(const AVFrame* src, int64_t origin_ns)
    { hub_->push_avframe(src, origin_ns); }
void VideoRenderer::push_audio_pcm(const int16_t* samples, int sample_count,
                                   int sample_rate, int channels)
    { hub_->push_audio_pcm(samples, sample_count, sample_rate, channels); }
void VideoRenderer::record_payload_bytes(std::size_t n)
    { hub_->record_payload_bytes(n); }
void VideoRenderer::set_active_device(DeviceInfo info) {
    StreamHub::DeviceInfo d{std::move(info.peer_ip), std::move(info.session_id),
                            std::move(info.kind), std::move(info.name),
                            std::move(info.model)};
    hub_->set_active_device(std::move(d));
}
void VideoRenderer::clear_active_device()
    { hub_->clear_active_device(); }
bool VideoRenderer::clear_session_if_active(const std::string& session_id)
    { return hub_->clear_session_if_active(session_id); }
void VideoRenderer::set_audio_control_handler(
        std::function<void(ap::airplay::MediaCommand, uint32_t)> handler)
    { hub_->audio_control_handler = std::move(handler); }
void VideoRenderer::set_disconnect_handler(std::function<void()> h)
    { hub_->set_disconnect_handler(std::move(h)); }
bool VideoRenderer::active_device_snapshot(DeviceInfo& out) const {
    StreamHub::DeviceInfo d;
    if (!hub_->active_device_snapshot(d)) return false;
    out = DeviceInfo{std::move(d.peer_ip), std::move(d.session_id),
                     std::move(d.kind), std::move(d.name), std::move(d.model)};
    return true;
}
void VideoRenderer::set_idle_info(const std::string& name, const std::string& ip)
    { hub_->set_idle_info(name, ip); }
void VideoRenderer::push_metadata(const std::string& title,
                                  const std::string& artist,
                                  const std::string& album)
    { hub_->push_metadata(title, artist, album); }
void VideoRenderer::push_frame(const uint8_t* y, int y_stride,
                               const uint8_t* u, int u_stride,
                               const uint8_t* v, int v_stride,
                               int width, int height,
                               int64_t origin_ns)
    { hub_->push_frame(y, y_stride, u, u_stride, v, v_stride, width, height, origin_ns); }
void VideoRenderer::push_frame_nv12(const uint8_t* y,  int y_stride,
                                    const uint8_t* uv, int uv_stride,
                                    int width, int height,
                                    int64_t origin_ns)
    { hub_->push_frame_nv12(y, y_stride, uv, uv_stride, width, height, origin_ns); }

// The SDL window thread is only built for the legacy front end.
void VideoRenderer::run(const std::string& title) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        LOG_ERROR << "SDL_Init failed: " << SDL_GetError();
        running_ = false;
        return;
    }

    // Bilinear filtering for any texture we scale (video frames and
    // cover art). SDL defaults to nearest-neighbour, which gave the
    // output a hard "blocky" look compared to Apple-certified
    // receivers. Must be set before SDL_CreateRenderer to take effect.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    // 8 位 alpha 通道：隐藏界面的逐像素透明依赖后备缓冲的 alpha
    // （必须在建窗前声明，DWM 才能按 alpha 合成到桌面）。
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#if defined(_WIN32)
    // SDL otherwise omits WS_THICKFRAME for borderless windows, which
    // prevents Windows from resizing them despite the hit-test handles.
    SDL_SetHint("SDL_BORDERLESS_RESIZABLE_STYLE", "1");
#endif

    auto preferences = live_settings_ ? live_settings_->ui_preferences() : ap::UiPreferences{};
    int initial_x = SDL_WINDOWPOS_CENTERED, initial_y = SDL_WINDOWPOS_CENTERED;
    int initial_w = kDefaultWinWidth, initial_h = kDefaultWinHeight;
    if (preferences.window_placement_valid) {
        initial_w = preferences.window_width;
        initial_h = preferences.window_height;
        SDL_Rect available{};
        bool found = false;
        for (int display = 0; display < SDL_GetNumVideoDisplays(); ++display) {
            SDL_Rect bounds{};
            if (SDL_GetDisplayUsableBounds(display, &bounds) != 0) continue;
            // Require a reachable title bar, including on displays with negative coordinates.
            if (preferences.window_y >= bounds.y && preferences.window_y + 56 <= bounds.y + bounds.h &&
                preferences.window_x + 100 <= bounds.x + bounds.w && preferences.window_x + initial_w - 100 >= bounds.x) {
                available = bounds; found = true; break;
            }
        }
        if (found || SDL_GetDisplayUsableBounds(0, &available) == 0) {
            initial_w = std::min(initial_w, available.w);
            initial_h = std::min(initial_h, available.h);
            initial_x = found ? std::clamp(preferences.window_x, available.x, available.x + available.w - initial_w)
                              : available.x + (available.w - initial_w) / 2;
            initial_y = found ? std::clamp(preferences.window_y, available.y, available.y + available.h - initial_h)
                              : available.y + (available.h - initial_h) / 2;
        }
    }
    SDL_Window*   window   = SDL_CreateWindow(
        title.c_str(), initial_x, initial_y, initial_w, initial_h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_OPENGL);
    // Read the initial vsync preference from LiveSettings (default
    // true). Toggling at runtime is handled by SDL_RenderSetVSync
    // below in the render loop.
    const bool vsync_initial = live_settings_
        ? live_settings_->vsync_enabled.load(std::memory_order_relaxed)
        : true;
    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
    if (vsync_initial) renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
#if defined(_WIN32)
    // OpenGL 渲染器：alpha 通道 + DWM blur-behind 才能实现逐像素
    // 透明窗口（D3D11 翻转模型会绕过 DWM 合成，透明失效）。
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
    SDL_Renderer* renderer = window
        ? SDL_CreateRenderer(window, -1, renderer_flags)
        : nullptr;
    bool vsync_active = vsync_initial;
    if (!window || !renderer) {
        LOG_ERROR << "SDL window/renderer create failed: " << SDL_GetError();
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window)   SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        running_ = false;
        return;
    }
    LOG_INFO << "VideoRenderer window up (" << kDefaultWinWidth
             << 'x' << kDefaultWinHeight << ", resizable)";
    SDL_SetWindowMinimumSize(window, 960, 720);
    // 系统级热键线程：Alt+H 隐藏界面 / Alt+= 放大 / Alt+- 缩小。
    // 按住 Alt 时 SDL 收不到后续按键的 KEYDOWN（实测），组合键必须
    // 走 RegisterHotKey；WM_HOTKEY 是线程消息，故独立线程自管泵。
    HotkeyThread hotkeys;
    hotkeys.start();

    GpuVideo gpu_video;
#if defined(_WIN32)
    SDL_SysWMinfo instance_window_info{};
    SDL_VERSION(&instance_window_info.version);
    if (SDL_GetWindowWMInfo(window, &instance_window_info))
        SetPropW(instance_window_info.info.win.window,
                 ap::platform::kInstanceWindowProperty, reinterpret_cast<HANDLE>(1));
#endif
    gpu_video.init(renderer);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        av_buffer_unref(&gpu_device_);
        gpu_device_ = gpu_video.device_ref();
        gpu_ready_ = true;
        cv_.notify_all();
    }

    // Win11 Mica backdrop. No-op on pre-Win11 — caller sees a regular
    // opaque window chrome instead of the subtle acrylic blur.
    enable_mica_backdrop(window);

    // Dear ImGui on top of the SDL renderer. The overlay runs in this
    // same thread and GPU context — zero frame copy, no IPC.



    g_ui_scale = compute_ui_scale(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    apply_overlay_theme();
    if (g_ui_scale > 1.01f) ImGui::GetStyle().ScaleAllSizes(g_ui_scale);
    ImFont* heading_font = nullptr;
    ImFont* bold_font = nullptr;
    ImFont* small_font = nullptr;
    ImFont* page_font = nullptr;
#if defined(_WIN32)
    if (std::filesystem::exists("C:/Windows/Fonts/segoeui.ttf")) {
        // 中文回退：把微软雅黑并进每个字体（MergeMode 只影响紧接着添加的那个字体）
        static ImFontConfig zh_merge;
        zh_merge.MergeMode = true;
        zh_merge.PixelSnapH = true;
io.FontDefault = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", S(16.0f));
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", S(16.0f), &zh_merge, zh_glyph_ranges);
heading_font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", S(36.0f));
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", S(36.0f), &zh_merge, zh_glyph_ranges);
bold_font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisb.ttf", S(18.0f));
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", S(18.0f), &zh_merge, zh_glyph_ranges);
page_font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", S(24.0f));
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", S(24.0f), &zh_merge, zh_glyph_ranges);
small_font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", S(12.0f));
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", S(12.0f), &zh_merge, zh_glyph_ranges);
    }
#endif
    if (!io.FontDefault) io.FontDefault = io.Fonts->AddFontDefault();
    if (!bold_font) bold_font = io.FontDefault;
    if (!small_font) small_font = io.FontDefault;
    if (!heading_font) heading_font = io.FontDefault;
    if (!page_font) page_font = bold_font;
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

#ifdef HAVE_UI_TESTS
    // Official Dear ImGui Test Engine. The engine injects input through
    // ImGui's IO queue and reads item rects from ImGui's item hooks, so
    // checks are numeric (rects/sizes) rather than screenshot comparisons.
    ImGuiTestEngine* test_engine = nullptr;
    if (ui_tests_enabled_) {
        test_engine = ImGuiTestEngine_CreateContext();
        ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(test_engine);
        test_io.ConfigSavedSettings = false;
        test_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
        test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
        test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
        test_io.ConfigCaptureEnabled = false;
        test_io.ExportResultsFilename = "ui-tests-results.xml";
        test_io.ExportResultsFormat = ImGuiTestEngineExportFormat_JUnitXml;
        ImGuiTestEngine_Start(test_engine, ImGui::GetCurrentContext());
        ImGuiTestEngine_InstallDefaultCrashHandler();
        ap::video::ui_tests::register_tests(test_engine,
            {this, live_settings_, g_ui_scale});
        ImGuiTestEngine_QueueTests(test_engine, ImGuiTestGroup_Tests);
        LOG_INFO << "UI tests: Dear ImGui Test Engine suite queued";
    }
#endif

    SDL_Texture* video_tex = nullptr;
    int          video_tex_w = 0, video_tex_h = 0;
    Uint32       video_tex_fmt = 0;  // SDL_PIXELFORMAT_IYUV or SDL_PIXELFORMAT_NV12

    SDL_Texture* cover_tex = nullptr;
    int          cover_tex_w = 0, cover_tex_h = 0;

    // Text overlay: load SDL_ttf + a multi-font chain (Latin primary +
    // CJK fallbacks) at two sizes so make_text can cover any iOS track
    // name, including Japanese / Chinese / Korean.
    std::vector<TTF_Font*> fonts_big;
    std::vector<TTF_Font*> fonts_small;
    if (TTF_Init() == 0) {
        fonts_big   = open_font_chain(32);
        fonts_small = open_font_chain(20);
        if (TTF_Font* semibold = TTF_OpenFont("C:/Windows/Fonts/seguisb.ttf",32)) {
            if (!fonts_big.empty()) { TTF_CloseFont(fonts_big.front()); fonts_big.front()=semibold; }
            else fonts_big.push_back(semibold);
        }
    } else {
        LOG_WARN << "TTF_Init failed: " << TTF_GetError();
    }
    SDL_Texture* title_tex     = nullptr; int title_w     = 0, title_h     = 0;
    SDL_Texture* artist_tex    = nullptr; int artist_w    = 0, artist_h    = 0;
    SDL_Texture* album_tex     = nullptr; int album_w     = 0, album_h     = 0;
    SDL_Texture* idle_name_tex = nullptr; int idle_name_w = 0, idle_name_h = 0;
    SDL_Texture* idle_ip_tex   = nullptr; int idle_ip_w   = 0, idle_ip_h   = 0;
    SDL_Texture* idle_msg_tex  = nullptr; int idle_msg_w  = 0, idle_msg_h  = 0;

    auto make_text = [&](const std::vector<TTF_Font*>& fonts,
                         const std::string& s, SDL_Color c,
                         SDL_Texture*& tex, int& w, int& h) {
        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        w = h = 0;
        if (fonts.empty() || s.empty()) return;
        SDL_Surface* surf = render_multi_font(fonts, s, c);
        if (!surf) return;
        tex = SDL_CreateTextureFromSurface(renderer, surf);
        w = surf->w; h = surf->h;
        SDL_FreeSurface(surf);
        // Default texture blend mode is NONE, which ignores alpha and renders
        // the surface as a solid opaque rectangle (every transparent pixel of
        // the glyph background fills the window with white). Force BLEND so
        // only the anti-aliased glyph pixels are drawn.
        if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    };

    auto last_video_frame_time = std::chrono::steady_clock::now()
                                 - std::chrono::seconds(10);

    // Persistent UI toggles (struct lifted to file scope above so
    // the SDL hit-test callback can access it). Defaults match the
    // design mockup (chrome visible). Chromeless mode hides toolbar
    // + sidebar + options + status bar so the AirPlay video fills
    // the whole window; unlike fullscreen this stays a regular
    // resizable window. F11 / double-click toggle real fullscreen,
    // H toggles chromeless, ESC peels back layers.
    UiState ui;
    ui.diagnostics = preferences.diagnostics;
    if (live_settings_)
        ui.chromeless = live_settings_->chromeless.load(std::memory_order_relaxed);
    if (SDL_SetWindowHitTest(window, hit_test_cb, &ui) != 0)
        LOG_WARN << "Window resize hit-test unavailable: " << SDL_GetError();
    if (ui.chromeless) SDL_SetWindowMinimumSize(window, 160, 120);

    // New application UI (pages/widgets/theme in app_ui/*.h). Lives for
    // the whole render-loop lifetime; pages read the Context snapshot.
    std::unique_ptr<ap::video::app_ui::AppUI> app_ui =
        std::make_unique<ap::video::app_ui::AppUI>();
    std::array<char, 1024> recording_dir{};
    // 256 bytes: ImGui truncates at the buffer limit, and a 51-byte buffer
    // used to cut a long Chinese name mid-UTF-8 (invalid byte sequence ->
    // normalize_receiver_name rejects it, so the name silently would not save).
    std::array<char, 256> receiver_name_buf{};
    if (live_settings_)
        std::snprintf(receiver_name_buf.data(), receiver_name_buf.size(), "%s",
                      live_settings_->receiver_name().c_str());
    VideoRecorder::Options recording_options;
    recording_options.codec = static_cast<VideoRecorder::VideoCodec>(preferences.recording_codec);
    recording_options.encoder = static_cast<VideoRecorder::EncoderMode>(preferences.recording_encoder);
    recording_options.bitrate_mbps = preferences.recording_bitrate;
    const std::string default_recording_dir = VideoRecorder::default_directory();
    std::snprintf(recording_dir.data(), recording_dir.size(), "%s",
                  preferences.recording_directory.empty() ? default_recording_dir.c_str() : preferences.recording_directory.c_str());
    auto toggle_recording = [&]() {
        if (!recorder_) return;
        if (recorder_->status().recording) recorder_->stop();
        else recorder_->start(recording_dir.data(), recording_options);
    };

    bool fullscreen = live_settings_
        ? live_settings_->fullscreen.load(std::memory_order_relaxed) : false;
    auto toggle_fullscreen = [&]() {
        fullscreen = !fullscreen;
        SDL_SetWindowFullscreen(window,
            fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        SDL_ShowCursor(fullscreen ? SDL_DISABLE : SDL_ENABLE);
        if (live_settings_)
            live_settings_->fullscreen.store(fullscreen, std::memory_order_relaxed);
    };
    if (fullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(SDL_DISABLE);
    } else if (!ui.chromeless && preferences.window_maximized) {
        SDL_MaximizeWindow(window);
    }

    auto publish_preferences = [&]() {
        auto current = preferences;
        current.diagnostics = ui.diagnostics;
        current.recording_directory = recording_dir.data();
        current.recording_codec = static_cast<int>(recording_options.codec);
        current.recording_encoder = static_cast<int>(recording_options.encoder);
        current.recording_bitrate = recording_options.bitrate_mbps;
        const Uint32 flags = SDL_GetWindowFlags(window);
        if (!fullscreen && !ui.chromeless && !(flags & SDL_WINDOW_MINIMIZED)) {
            current.window_maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
            if (!current.window_maximized) {
                SDL_GetWindowPosition(window, &current.window_x, &current.window_y);
                SDL_GetWindowSize(window, &current.window_width, &current.window_height);
                current.window_placement_valid = true;
            }
        }
        if (!(current == preferences)) {
            preferences = current;
            if (live_settings_) live_settings_->set_ui_preferences(current);
        }
    };

    // Chromeless aspect-lock state. Triggers a re-snap on:
    //   - chromeless toggled on
    //   - video frame dimensions change (iPhone rotation, e.g.
    //     portrait <-> landscape mid-mirror)
    //   - user resize via the hit-test edges
    // saved_w/h capture the chrome-mode window size before entering chromeless so we can
    // restore it on exit (otherwise the menus get crushed against a
    // tall narrow video-aspect window).
    bool prev_chromeless    = false;
    // 进入隐藏界面的时刻：用于在底部短暂浮现「怎么恢复」的提示。
    std::chrono::steady_clock::time_point chromeless_at{};
    // ---- 设备边框：扫描 assets\frame，按设置懒加载横/竖纹理 ----
    const std::filesystem::path frame_dir = [] {
        wchar_t buf[32768] = {};
        GetModuleFileNameW(nullptr, buf, 32768);
        std::filesystem::path exe(buf);
        return exe.parent_path() / "assets" / "frame";
    }();
    const std::vector<DeviceFrameSource> frame_sources = scan_frame_sources(frame_dir);
    if (frame_sources.empty())
        LOG_WARN << "no device frame assets in " << wide_to_utf8(frame_dir.wstring());
    int frame_loaded_style = -2;   // -2 强制首次加载
    std::string frame_last_label;  // 上一个生效的分组，自动模式借此延续颜色
    std::string frame_last_model;  // 自动模式最近一次匹配的设备型号标识
    DeviceFrame frame_portrait, frame_landscape;
    // ---- 隐藏界面：窗口区域（SetWindowRgn）------------------------
    // 按机身轮廓把主窗口裁成手机形状：区域外不属于窗口 → 桌面直接透出。
    // （ULW/分层窗口在本机被系统性禁用 gle=31，色键/blur-behind 在
    // 交换链窗口上一律无效——全部废弃。）
    HRGN body_rgn_ = nullptr;
    int body_rgn_key_w_ = 0, body_rgn_key_h_ = 0, body_rgn_key_style_ = -9;


    // 圆角绘制用的 RGBA 中间纹理：NV12/YUV 视频纹理不能直接走
    // SDL_RenderGeometry（D3D11 后端的几何管线采样不到 UV 平面），
    // 每帧先把当前帧转绘到这张离屏纹理，再对它做圆角网格采样。
    SDL_Texture* video_rgba_tex = nullptr;
    int video_rgba_w = 0, video_rgba_h = 0;
    int  prev_video_w       = 0;
    int  prev_video_h       = 0;
    int  saved_window_w     = 0;
    int  saved_window_h     = 0;
    bool saved_window_maximized = false;
    bool custom_resolution = false;
    float stage_zoom = 1.0f;   // Alt+=/Alt+- 的窗口模式画面缩放
    auto reset_all_settings = [&]() {
        if (!live_settings_) return;
        const ap::PersistentSettings defaults;
        int mirror_w = 2560, mirror_h = 1440;
        SDL_DisplayMode display{};
        if (SDL_GetCurrentDisplayMode(0, &display) == 0 &&
            display.w >= 320 && display.h >= 240 && display.w <= 7680 && display.h <= 4320) {
            mirror_w = display.w;
            mirror_h = display.h;
        }
        live_settings_->set_receiver_name(defaults.receiver_name);
        live_settings_->mirror_width.store(mirror_w);
        live_settings_->mirror_height.store(mirror_h);
        live_settings_->hevc_enabled.store(defaults.hevc_enabled);
        live_settings_->max_fps.store(defaults.max_fps);
        live_settings_->refresh_rate.store(defaults.refresh_rate);
        live_settings_->mirror_hwaccel.store(defaults.mirror_hwaccel);
        live_settings_->vsync_enabled.store(defaults.vsync_enabled);
        live_settings_->video_rotation.store(defaults.video_rotation);
        live_settings_->frame_style.store(defaults.frame_style);
        live_settings_->fullscreen.store(defaults.fullscreen);
        live_settings_->chromeless.store(defaults.chromeless);
        preferences = defaults.ui;
        live_settings_->set_ui_preferences(preferences);
        std::snprintf(receiver_name_buf.data(),receiver_name_buf.size(),"%s",defaults.receiver_name.c_str());
        std::snprintf(recording_dir.data(),recording_dir.size(),"%s",default_recording_dir.c_str());
        recording_options = VideoRecorder::Options{};
        ui.diagnostics = defaults.ui.diagnostics;
        ui.chromeless = false;
        custom_resolution = false;
        stage_zoom = 1.0f;
        prev_chromeless = false;
        saved_window_w = saved_window_h = 0;
        fullscreen = false;
        SDL_SetWindowFullscreen(window,0);
        SDL_ShowCursor(SDL_ENABLE);
        SDL_RestoreWindow(window);
        SDL_SetWindowMinimumSize(window,960,720);
        saved_window_maximized = false;
        SDL_SetWindowSize(window,defaults.ui.window_width,defaults.ui.window_height);
        SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
        publish_preferences();
        app_ui->view.reset_notice_until = static_cast<float>(ImGui::GetTime()) + 4;
    };

    // iOS sends upright frames with updated dimensions when it rotates.
    // Auto must not apply another 90 degrees to an already upright image.
    auto rotation_degrees = [&]() {
        const int mode = live_settings_ ? live_settings_->video_rotation.load() : -1;
        return mode >= 0 && mode <= 3 ? mode * 90 : 0;
    };
    auto display_width = [&]() {
        return rotation_degrees() % 180 ? video_tex_h : video_tex_w;
    };
    auto display_height = [&]() {
        return rotation_degrees() % 180 ? video_tex_w : video_tex_h;
    };

    // "Shrink to fit" snap: collapse the dimension that exceeds the
    // target aspect, leave the other one alone. Used on chromeless
    // entry so the new window fits inside the previous chrome window's
    // bounds. Manual resizing stays free; fit_inside preserves video aspect.
    // 隐藏界面模式的窗口吸附目标：优先「机身」比例（含边框）——
    // 窗口=手机，四周完全无背景；外壳未加载时退回画面比例。
    auto body_aspect = [&]() -> float {
        const DeviceFrame& shell = display_width() >= display_height()
            ? frame_landscape : frame_portrait;
        if (shell.tex && shell.w > 0 && shell.h > 0)
            return static_cast<float>(shell.w) / static_cast<float>(shell.h);
        return -1.f;
    };
    auto snap_to_video_aspect = [&]() {
        if (!ui.chromeless || fullscreen) return;
        if (video_tex_w <= 0 || video_tex_h <= 0) return;
        int wnow = 0, hnow = 0;
        SDL_GetWindowSize(window, &wnow, &hnow);
        if (wnow <= 0 || hnow <= 0) return;
        float vid_a = body_aspect();
        if (vid_a <= 0) vid_a = float(display_width()) / float(display_height());
        const float win_a = float(wnow) / float(hnow);
        int snap_w = wnow, snap_h = hnow;
        if (win_a > vid_a) {
            snap_w = static_cast<int>(hnow * vid_a + 0.5f);
        } else {
            snap_h = static_cast<int>(wnow / vid_a + 0.5f);
        }
        if (std::abs(snap_w - wnow) > 1 || std::abs(snap_h - hnow) > 1) {
            SDL_SetWindowSize(window, snap_w, snap_h);
        }
    };

    // "Preserve area" snap: keep the longer current dimension as the
    // new long axis of the target aspect. Used on iPhone rotation —
    // the naive shrink-to-fit halves the visible area each rotation
    // because portrait/landscape inverts the aspect, and the dim
    // that was just snapped becomes the one to shrink again. Long-
    // axis preservation effectively swaps width/height across the
    // rotation so screen real estate stays roughly constant.
    auto snap_preserving_area = [&]() {
        if (!ui.chromeless || fullscreen) return;
        if (video_tex_w <= 0 || video_tex_h <= 0) return;
        int wnow = 0, hnow = 0;
        SDL_GetWindowSize(window, &wnow, &hnow);
        if (wnow <= 0 || hnow <= 0) return;
        float vid_a = body_aspect();
        if (vid_a <= 0) vid_a = float(display_width()) / float(display_height());
        const int long_dim = std::max(wnow, hnow);
        int new_w, new_h;
        if (vid_a >= 1.0f) {        // landscape video
            new_w = long_dim;
            new_h = static_cast<int>(long_dim / vid_a + 0.5f);
        } else {                     // portrait video
            new_h = long_dim;
            new_w = static_cast<int>(long_dim * vid_a + 0.5f);
        }
        if (std::abs(new_w - wnow) > 1 || std::abs(new_h - hnow) > 1) {
            SDL_SetWindowSize(window, new_w, new_h);
        }
    };

    // Alt+=/Alt+- 缩放：隐藏界面直接缩放窗口（手机整体缩放，高度
    // 限制 240..1800，步进 10%）；窗口模式缩放画面在舞台内的比例
    // （0.3..3.0）。全屏模式不响应。
    auto scale_phone = [&](float f) {
        if (fullscreen) return;
        if (ui.chromeless) {
            int ww = 0, hh = 0;
            SDL_GetWindowSize(window, &ww, &hh);
            if (ww <= 0 || hh <= 0) return;
            float nh = hh * f;
            if (nh < 240.0f) nh = 240.0f;
            if (nh > 1800.0f) nh = 1800.0f;
            const float k = nh / hh;
            SDL_SetWindowSize(window, static_cast<int>(ww * k + 0.5f),
                                      static_cast<int>(nh + 0.5f));
        } else {
            stage_zoom = std::clamp(stage_zoom * f, 0.3f, 3.0f);
        }
    };

    ap::platform::FirewallMonitor firewall_monitor;
#ifdef HAVE_UI_TESTS
    static uint64_t hb_top = 0;
#endif
    while (running_.load()) {
#ifdef HAVE_UI_TESTS
        if (++hb_top % 300 == 1) LOG_INFO << "loop-top " << hb_top;
#endif
#ifdef HAVE_UI_TESTS
        if (ui_tests_done_.load())
            break;
        {
            const int tw = ui_test_resize_w_.exchange(0);
            const int th = ui_test_resize_h_.exchange(0);
            if (tw > 0 && th > 0)
                SDL_SetWindowSize(window, tw, th);
        }
#endif
        if (show_requested_.exchange(false)) {
            SDL_ShowWindow(window);
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
                SDL_RestoreWindow(window);
            SDL_RaiseWindow(window);
#if defined(_WIN32)
            if (instance_window_info.info.win.window)
                SetForegroundWindow(instance_window_info.info.win.window);
#endif
        }
        // Re-snap the window aspect on either of:
        //   - chromeless flipping on (snap once on entry)
        //   - video dimensions changing while chromeless (iPhone
        //     rotation between portrait/landscape, resolution change
        //     on next mirror connect, etc.)
        // On the way OUT of chromeless, restore the chrome-mode size
        // we captured on entry — otherwise the panels render into a
        // window the size of the video, which crushes the layout.
        const bool video_changed =
            ui.chromeless &&
            (display_width() != prev_video_w || display_height() != prev_video_h);
        if (ui.chromeless != prev_chromeless) {
            SDL_SetWindowMinimumSize(window, ui.chromeless ? 160 : 960, ui.chromeless ? 120 : 720);
            if (ui.chromeless && !fullscreen) {
                saved_window_maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
                if (saved_window_maximized) SDL_RestoreWindow(window);
                SDL_GetWindowSize(window,
                                  &saved_window_w, &saved_window_h);
                snap_to_video_aspect();
            } else if (!ui.chromeless &&
                       saved_window_w > 0 && saved_window_h > 0) {
                SDL_SetWindowSize(window,
                                  saved_window_w, saved_window_h);
                if (saved_window_maximized) SDL_MaximizeWindow(window);
            }
            prev_chromeless = ui.chromeless;
            if (ui.chromeless)
                chromeless_at = std::chrono::steady_clock::now();
            if (live_settings_)
                live_settings_->chromeless.store(ui.chromeless, std::memory_order_relaxed);
            prev_video_w    = display_width();
            prev_video_h    = display_height();
        } else if (video_changed) {
            // iPhone rotation flips portrait <-> landscape: use the
            // area-preserving snap so the window swaps dims rather
            // than shrinking by half each rotation.
            snap_preserving_area();
            prev_video_w = display_width();
            prev_video_h = display_height();
        }

        // ---- 窗口区域（隐藏界面）：把主窗口裁成机身精确轮廓 ------
        // 轮廓逐行来自素材的不透明范围（含侧键/圆角），区域外不属于
        // 窗口 → 桌面直接透出。尺寸/样式变化时重建；退出时清除。
#ifdef _WIN32
        {
            SDL_SysWMinfo wm;
            SDL_VERSION(&wm.version);
            const bool have = SDL_GetWindowWMInfo(window, &wm) != 0;
            const DeviceFrame* shell = &(display_width() >= display_height()
                                         ? frame_landscape : frame_portrait);
            const bool want = ui.chromeless && !fullscreen && have && shell->tex;
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

        // Honor live VSYNC toggle: SDL 2.0.18+ supports flipping
        // VSYNC at runtime without rebuilding the renderer. The
        // call is cheap enough to issue every frame; we still
        // gate it on a state delta so we don't spam the driver.
        if (live_settings_) {
            const bool want_vsync =
                live_settings_->vsync_enabled.load(std::memory_order_relaxed);
            if (want_vsync != vsync_active) {
                if (SDL_RenderSetVSync(renderer, want_vsync ? 1 : 0) == 0) {
                    LOG_INFO << "VideoRenderer VSYNC "
                             << (want_vsync ? "enabled" : "disabled");
                    vsync_active = want_vsync;
                }
            }
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // Always feed ImGui first so focus / hover / capture work.
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) {
                LOG_INFO << "VideoRenderer received SDL_QUIT";
                closed_  = true;
                running_ = false;
                break;
            }
            const ImGuiIO& io_poll = ImGui::GetIO();
            // A focused panel captures navigation keys even when no field is
            // being edited. Keep receiver shortcuts global in that case;
            // text fields retain letters and modified key combinations.
            const bool receiver_shortcut = e.type == SDL_KEYDOWN &&
                (e.key.keysym.sym == SDLK_f || e.key.keysym.sym == SDLK_F11 ||
                 e.key.keysym.sym == SDLK_r || e.key.keysym.sym == SDLK_h ||
                 e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS ||
                 e.key.keysym.sym == SDLK_MINUS ||
                 e.key.keysym.sym == SDLK_KP_PLUS ||
                 e.key.keysym.sym == SDLK_KP_MINUS);
            // Alt 组合键放行（Alt+H / Alt+= / Alt+- 的 SDL 键事件兜底
            // 路径；RegisterHotKey 成功时系统会吞掉按键，不会双触发）。
            const bool allow_keyboard = receiver_shortcut
                ? !io_poll.WantTextInput &&
                  !(e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))
                : !io_poll.WantCaptureKeyboard;
            if (e.type == SDL_USEREVENT) {
                switch (e.user.code) {
                    case HotkeyThread::kAltH:     ui.chromeless = !ui.chromeless; break;
                    case HotkeyThread::kAltPlus:  scale_phone(1.10f); break;
                    case HotkeyThread::kAltShiftPlus: scale_phone(1.10f); break;
                    case 5: scale_phone(1.0f / 1.10f); break;
                    case HotkeyThread::kAltMinus: scale_phone(1.0f / 1.10f); break;
                    default: break;
                }
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat && allow_keyboard) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        // ESC peels back layers in order:
                        // fullscreen first, then chromeless, then quit.
                        if (fullscreen)         toggle_fullscreen();
                        else if (ui.chromeless) ui.chromeless = false;
                        else { closed_ = true; running_ = false; }
                        break;
                    case SDLK_f:
                    case SDLK_F11:
                        toggle_fullscreen();
                        break;
                    case SDLK_h:
                        if (e.key.keysym.mod & KMOD_ALT) ui.chromeless = !ui.chromeless;
                        break;
                    case SDLK_PLUS:
                    case SDLK_KP_PLUS:
                        if (e.key.keysym.mod & KMOD_ALT) scale_phone(1.10f);
                        break;
                    case SDLK_EQUALS:
                        // 主键盘 +/= 物理键：按住 Alt 直接按即放大
                        if (e.key.keysym.mod & KMOD_ALT) scale_phone(1.10f);
                        break;
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        if (e.key.keysym.mod & KMOD_ALT) scale_phone(1.0f / 1.10f);
                        break;
                    case SDLK_r:
                        toggle_recording();
                        break;
                    default: break;
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN &&
                e.button.button == SDL_BUTTON_LEFT &&
                e.button.clicks == 2 &&
                !io_poll.WantCaptureMouse) {
                toggle_fullscreen();
            }
        }
        if (!running_.load()) break;

        // --- Consume pending video frame -----------------------------
        // Two producers feed the slot:
        //   - push_avframe (mirror, refcounted AVFrame, zero-copy)
        //   - push_frame / push_frame_nv12 (HLS, plane buffers)
        // The AVFrame slot wins when present; legacy slot used by
        // HLS players that haven't been migrated.
        bool have_avf   = false;
        bool have_frame = false;
        bool have_nv12  = false;
        std::vector<unsigned char> y, u, v, uv;
        DecodedFrame avf_local;
        int w = 0, h = 0;
        int64_t frame_origin_ns_local = 0;
        DeviceInfo pacing_device;
        StreamHub::RenderConsume pending_cover_consume_;
        const bool audio_only = active_device_snapshot(pacing_device) && pacing_device.kind == "Audio";
        // Audio controls/progress need fewer redraws than mirrored video.
        // A newly queued video frame still wakes this wait immediately.
        const auto refresh_wait = std::chrono::milliseconds(audio_only ? 33 : 16);
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait_for(lock, refresh_wait,
                         [this] {
                             return hub_->has_pending() || !running_.load();
                         });
        }
        {
            auto rc = hub_->render_consume();
            if (rc.has_avf) {
                avf_local = std::move(rc.avf);
                w = rc.w; h = rc.h;
                frame_origin_ns_local = rc.origin_ns;
                have_avf = true;
            } else if (rc.has_planes) {
                have_nv12 = rc.nv12;
                y = std::move(rc.y);
                if (have_nv12) uv = std::move(rc.uv);
                else { u = std::move(rc.u); v = std::move(rc.v); }
                w = rc.w; h = rc.h;
                frame_origin_ns_local = rc.origin_ns;
                have_frame = true;
            }
            pending_cover_consume_ = std::move(rc);
        }

        // Decide the SDL pixel format to use; depends on which path
        // delivered the frame.
        Uint32 desired_fmt = SDL_PIXELFORMAT_IYUV;
        AVFrame* af = avf_local.frame;
        if (have_avf && af) {
            if (af->format == AV_PIX_FMT_D3D11) {
                desired_fmt = SDL_PIXELFORMAT_ARGB8888;
            } else if (af->format == AV_PIX_FMT_NV12) {
                desired_fmt = SDL_PIXELFORMAT_NV12;
            } else {
                desired_fmt = SDL_PIXELFORMAT_IYUV;
            }
        } else if (have_frame) {
            desired_fmt = have_nv12 ? SDL_PIXELFORMAT_NV12
                                    : SDL_PIXELFORMAT_IYUV;
        }

        if (have_avf || have_frame) {
            if (!video_tex || video_tex_w != w || video_tex_h != h ||
                video_tex_fmt != desired_fmt) {
                gpu_video.reset_target();
                if (video_tex) SDL_DestroyTexture(video_tex);
                video_tex = SDL_CreateTexture(renderer,
                    desired_fmt, desired_fmt == SDL_PIXELFORMAT_ARGB8888
                        ? SDL_TEXTUREACCESS_TARGET : SDL_TEXTUREACCESS_STREAMING, w, h);
                video_tex_w   = w;
                video_tex_h   = h;
                video_tex_fmt = desired_fmt;
                LOG_INFO << "VideoRenderer texture ("
                         << (desired_fmt == SDL_PIXELFORMAT_NV12 ? "NV12"
                             : desired_fmt == SDL_PIXELFORMAT_ARGB8888 ? "GPU BGRA"
                                                                     : "IYUV")
                         << ") created " << w << 'x' << h;
            }
            if (video_tex) {
                if (have_avf && af) {
                    // Direct upload from the refcounted AVFrame —
                    // the planes never crossed our buffers, just
                    // the FFmpeg pool → SDL staging texture.
                    if (af->format == AV_PIX_FMT_D3D11) {
                        if (!gpu_video.convert(renderer, video_tex, af)) {
                            LOG_ERROR << "D3D11 surface conversion failed";
                            gpu_video.reset_target();
                            SDL_DestroyTexture(video_tex);
                            video_tex = nullptr;
                        }
                    } else if (af->format == AV_PIX_FMT_NV12) {
                        SDL_UpdateNVTexture(video_tex, nullptr,
                            af->data[0], af->linesize[0],
                            af->data[1], af->linesize[1]);
                    } else {
                        // YUV420P / YUVJ420P (full-range) — SDL
                        // treats both as IYUV, slight color-range
                        // bias on YUVJ but indistinguishable for
                        // mirror.
                        SDL_UpdateYUVTexture(video_tex, nullptr,
                            af->data[0], af->linesize[0],
                            af->data[1], af->linesize[1],
                            af->data[2], af->linesize[2]);
                    }
                } else if (have_nv12) {
                    SDL_UpdateNVTexture(video_tex, nullptr,
                        y.data(), w, uv.data(), w);
                } else {
                    SDL_UpdateYUVTexture(video_tex, nullptr,
                        y.data(), w, u.data(), w / 2, v.data(), w / 2);
                }
            }
            last_video_frame_time = std::chrono::steady_clock::now();
        }
        // avf_local destructor below releases our FFmpeg ref back
        // to the pool the moment SDL's UpdateTexture has issued
        // its CopySubresourceRegion to the GPU.

        // --- Drop the cover if clear_session was called --------------
        // (clear_cover flag arrives via render_consume below)
        // --- Consume pending cover-art JPEG / metadata ---------------
        // render_consume() (called in the frame-wait block above) already
        // moved cover + metadata out into pending_cover_consume_.
        std::vector<unsigned char> pending_cover;
        std::string t_title, t_artist, t_album, t_idle_name, t_idle_ip;
        bool meta_changed = false;
        bool idle_changed = false;
        {
            auto& rc = pending_cover_consume_;
            if (rc.clear_cover) {
                if (cover_tex) {
                    SDL_DestroyTexture(cover_tex);
                    cover_tex   = nullptr;
                    cover_tex_w = cover_tex_h = 0;
                }
            }
            if (rc.cover_dirty) {
                pending_cover = std::move(rc.cover);
                rc.cover.clear();
            }
            if (rc.meta_dirty) {
                t_title      = std::move(rc.title);
                t_artist     = std::move(rc.artist);
                t_album      = std::move(rc.album);
                meta_changed = true;
            }
            if (rc.idle_dirty) {
                t_idle_name  = std::move(rc.idle_name);
                t_idle_ip    = std::move(rc.idle_ip);
                idle_changed = true;
            }
        }
        if (idle_changed) {
            make_text(fonts_big,   t_idle_name,
                      SDL_Color{255, 255, 255, 255},
                      idle_name_tex, idle_name_w, idle_name_h);
            make_text(fonts_small, t_idle_ip,
                      SDL_Color{180, 180, 180, 255},
                      idle_ip_tex,   idle_ip_w,   idle_ip_h);
            make_text(fonts_small, "等待 AirPlay 连接",
                      SDL_Color{150, 150, 150, 255},
                      idle_msg_tex,  idle_msg_w,  idle_msg_h);
        }
        if (meta_changed) {
            make_text(fonts_big,   t_title,  SDL_Color{255, 255, 255, 255},
                      title_tex,  title_w,  title_h);
            make_text(fonts_small, t_artist, SDL_Color{220, 220, 220, 255},
                      artist_tex, artist_w, artist_h);
            make_text(fonts_small, t_album,  SDL_Color{180, 180, 180, 255},
                      album_tex,  album_w,  album_h);
            LOG_INFO << "VideoRenderer metadata textures updated";
        }

        if (!pending_cover.empty()) {
            int cw = 0, ch = 0;
            std::vector<uint8_t> rgb;
            if (decode_cover_art(pending_cover.data(), pending_cover.size(),
                                   cw, ch, rgb)) {
                if (!cover_tex || cover_tex_w != cw || cover_tex_h != ch) {
                    if (cover_tex) SDL_DestroyTexture(cover_tex);
                    cover_tex = SDL_CreateTexture(renderer,
                        SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, cw, ch);
                    cover_tex_w = cw; cover_tex_h = ch;
                }
                if (cover_tex) {
                    SDL_UpdateTexture(cover_tex, nullptr, rgb.data(), cw * 3);
                    LOG_INFO << "VideoRenderer cover art updated ("
                             << cw << 'x' << ch << ')';
                }
            } else {
                LOG_WARN << "VideoRenderer: JPEG decode failed ("
                         << pending_cover.size() << "B)";
            }
        }

        // --- Decide which texture to show ----------------------------
        // Video takes priority if we got a recent frame. iOS stops
        // emitting H.264 frames whenever the mirror content is static
        // (menu screens, paused video, idle home screen) — the encoder
        // is too efficient to bother re-sending identical frames. We
        // therefore keep the last video texture on screen as long as
        // a device is paired, regardless of the 500 ms freshness
        // window. Only on TEARDOWN (clear_active_device) does the
        // texture stop showing and we fall back to idle.
        const auto now = std::chrono::steady_clock::now();
        const bool video_fresh =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_video_frame_time).count() < 500;
        const bool playing_now = hub_->playing_state();
        DeviceInfo active_dev;
        const bool has_active_dev = active_device_snapshot(active_dev);
        const bool show_video =
            video_tex && active_dev.kind != "Audio" && (video_fresh || has_active_dev || !playing_now);

        int win_w = 0, win_h = 0;
        SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
        const int full_w = win_w;
        const int full_h = win_h;

        // Layout: top toolbar, sidebar (left), options panel (right),
        // status bar (bottom). Two ways to drop the chrome:
        //   - fullscreen: SDL_WINDOW_FULLSCREEN_DESKTOP, no chrome
        //   - chromeless: regular resizable window, no chrome
        // Both collapse all insets to 0 so the video stage covers the
        // whole window, and both skip the ImGui panel block below.
        const bool no_chrome = fullscreen || ui.chromeless;
        const auto firewall=firewall_monitor.status();
        // Feed the firewall state into app_ui before querying layout math:
        // the banner visibility depends on it. Diagnostics render inside
        // the footer strip, so no bottom inset is needed anymore.
        {
            namespace au = ap::video::app_ui;
            au::Context& c0 = app_ui->ctx;
            c0.firewall_state = static_cast<int>(firewall.state);
            c0.scale = g_ui_scale;
        }
        // The app_ui shell owns all chrome geometry: the SDL stage rect
        // must come from the same math the ImGui pages use, or the video
        // drifts out of the stage card at >100% DPI.
        const auto sa = app_ui->StageArea(
            static_cast<float>(full_w), static_cast<float>(full_h));
        const bool stage_page = sa.w > 0 && sa.h > 0;
        const int stage_x = no_chrome ? 0 : static_cast<int>(sa.x);
        const int stage_y = no_chrome ? 0 : static_cast<int>(sa.y);
        const int stage_w = no_chrome ? full_w : std::max(0, static_cast<int>(sa.w));
        const int stage_h = no_chrome ? full_h : std::max(0, static_cast<int>(sa.h));

        // 设备边框：换算当前应生效的素材（设置变化或连接设备变化时
        // 重建纹理）。frame_style：0=自动跟随机型，-1=关闭，≥1=手动指定。
        int frame_effective = -1;
        if (live_settings_) {
            const int style = live_settings_->frame_style.load(std::memory_order_relaxed);
            if (style == 0) {
                DeviceInfo dev;
                int matched = -1;
                if (active_device_snapshot(dev) && dev.kind == "Mirror" && !dev.model.empty()) {
                    matched = match_frame_for_model(frame_sources, dev.model, frame_last_label);
                    frame_last_model = dev.model;
                } else {
                    frame_last_model.clear();
                }
                frame_effective = matched >= 0 ? matched
                                  : (frame_sources.empty() ? -1 : 0);  // 匹配不上用第一个兜底
            } else if (style >= 1) {
                frame_effective = style - 1;
                frame_last_model.clear();
                if (frame_effective >= static_cast<int>(frame_sources.size())) frame_effective = -1;
            }
        }
        if (frame_effective != frame_loaded_style) {
            frame_loaded_style = frame_effective;
            if (frame_portrait.tex) SDL_DestroyTexture(frame_portrait.tex);
            if (frame_portrait.mask) SDL_DestroyTexture(frame_portrait.mask);
            frame_portrait = DeviceFrame{};
            if (frame_landscape.tex) SDL_DestroyTexture(frame_landscape.tex);
            if (frame_landscape.mask) SDL_DestroyTexture(frame_landscape.mask);
            frame_landscape = DeviceFrame{};
            if (frame_effective >= 0) {
                const auto& src = frame_sources[frame_effective];
                frame_last_label = src.label;
                load_frame_texture(renderer, src.portrait, frame_portrait);
                load_frame_texture(renderer, src.landscape, frame_landscape);
                if (!frame_portrait.tex && !frame_landscape.tex)
                    LOG_WARN << "device frame decode failed: " << src.label;
                else
                    LOG_INFO << "device frame: " << src.label
                             << (frame_last_model.empty() ? std::string()
                                                          : " (model " + frame_last_model + ")");
                // 外壳加载后若处于隐藏界面模式，补一次窗口吸附：
                // 按 H 时设备可能尚未连接，当时吸附的是画面比例，
                // 外壳（机身比例）加载后不补吸，四周会露出背景。
                if (ui.chromeless && !fullscreen) snap_to_video_aspect();
            }
        }

        // Override win_w/win_h locally so the existing video-fit and
        // idle-text centring math operates inside the stage rect
        // rather than the whole window. Restore after the SDL drawing
        // pass, before the ImGui pass references full window size.
        win_w = stage_w;
        win_h = stage_h;

        // Clear the whole window first (background under all panels),
        // then clip subsequent SDL draws to the stage rect via
        // viewport — the ImGui panels will paint over the cleared
        // sides afterwards.
        if (no_chrome) {
            // 隐藏界面/全屏：清屏写成全透明（DWM 合成桌面背景）
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        } else {
            // 应用背景色（与 app_ui 主题 --bg 一致；ImGui 面板半透明，
            // 底色必须由 SDL 通道给出）
            SDL_SetRenderDrawColor(renderer, 245, 245, 243, 255);
        }
        SDL_RenderClear(renderer);
        const bool framed_video = !no_chrome && show_video && stage_page;
        // 窗口模式：设备外壳四周留边，并给底部信息条让位。
        const int video_top_gap  = static_cast<int>(S(30));
        const int video_bot_gap  = video_top_gap + static_cast<int>(S(64));
        const int frame_pad = static_cast<int>(S(42));
        const SDL_Rect stage_rect{stage_x + (framed_video ? frame_pad : 0), stage_y + (framed_video ? video_top_gap : 0),
                                  std::max(1, stage_w - (framed_video ? frame_pad * 2 : 0)), std::max(1, stage_h - (framed_video ? video_bot_gap : 0))};
        win_w = stage_rect.w;
        win_h = stage_rect.h;
        SDL_RenderSetViewport(renderer, &stage_rect);

        auto draw_pause_badge = [&](const SDL_Rect& dst) {
            const int badge = std::min(dst.w, dst.h) / 6;
            const int cx    = dst.x + dst.w / 2;
            const int cy    = dst.y + dst.h / 2;
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
            SDL_Rect bg{cx - badge, cy - badge, badge * 2, badge * 2};
            SDL_RenderFillRect(renderer, &bg);
            const int bw = badge / 3;
            const int bh = badge;
            const int gap = badge / 3;
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 235);
            SDL_Rect b1{cx - gap - bw, cy - bh / 2, bw, bh};
            SDL_Rect b2{cx + gap,      cy - bh / 2, bw, bh};
            SDL_RenderFillRect(renderer, &b1);
            SDL_RenderFillRect(renderer, &b2);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        };

        // Suppress the pause badge entirely on Screen Mirroring
        // sessions: there, "no audio" doesn't mean "playback paused"
        // (it just means whatever is on the iPhone screen has no
        // soundtrack — a menu, the home screen, a static page). The
        // badge made sense in audio-only / HLS playback flows where
        // the user actually paused content. In mirror it just sat
        // there until the user pressed pause+play to clear it.
        const bool show_pause_badge =
            !playing_now &&
            (!has_active_dev || active_dev.kind != "Mirror");

        if (show_video) {
            if (no_chrome) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            } else {
                // 视频区背景用应用纸白色（与整体浅色主题统一，用户指定）
                SDL_SetRenderDrawColor(renderer, 245, 246, 244, 255);
            }
            SDL_RenderFillRect(renderer, nullptr);
            const int angle = rotation_degrees();
            // 设备外壳只在镜像会话叠加，按显示朝向自动选横/竖壳。
            const bool mirror_stream = has_active_dev && active_dev.kind == "Mirror";
            const DeviceFrame& shell = display_width() >= display_height()
                ? frame_landscape : frame_portrait;
            if (mirror_stream && shell.tex) {
                // 两层合成（Bezel HTML 基准）：整张边框 PNG 为定位容器
                // （源矩形=整张纹理，含透明边距，纯黑底上不可见）；
                // 画面填充「屏幕内容区」（构建期清单精确测量，边框自带
                // 的黑边/圆角/灵动岛自然遮盖画面方角）。隐藏界面：窗口
                // 已按整张边框比例吸附，边框铺满窗口；显示器全屏/窗口
                // 模式：完整手机居中（高度顶满）。
                SDL_Rect fdst;
                if (ui.chromeless) {
                    fdst = {0, 0, win_w, win_h};
                } else {
                    fdst = fit_inside(shell.w, shell.h, win_w, win_h);
                    if (stage_zoom != 1.0f) {
                        fdst.x += static_cast<int>(fdst.w * (1.0f - stage_zoom) / 2);
                        fdst.y += static_cast<int>(fdst.h * (1.0f - stage_zoom) / 2);
                        fdst.w  = static_cast<int>(fdst.w * stage_zoom);
                        fdst.h  = static_cast<int>(fdst.h * stage_zoom);
                    }
                }
                SDL_Rect hole;
                hole.x = fdst.x + static_cast<int>(shell.hx0 * fdst.w);
                hole.y = fdst.y + static_cast<int>(shell.hy0 * fdst.h);
                hole.w = static_cast<int>((shell.hx1 - shell.hx0) * fdst.w);
                hole.h = static_cast<int>((shell.hy1 - shell.hy0) * fdst.h);
                // 画面按内容区的圆角形状绘制（三角网格）—— 圆角外的
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
                // 画面 → RGBA 离屏纹理（NV12 不能直接做 alpha 运算），
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
                }
                SDL_RenderCopy(renderer, shell.tex, nullptr, &fdst);
                if (show_pause_badge) {
                    draw_pause_badge(hole);
                }
            } else {
                SDL_Rect dst = fit_inside(display_width(), display_height(), win_w, win_h);
                // SDL rotates the destination rectangle around its centre. Swap
                // its unrotated extents so the final bounds fit the stage exactly.
                SDL_Rect copy_dst = dst;
                if (angle % 180) {
                    copy_dst.w = dst.h;
                    copy_dst.h = dst.w;
                    copy_dst.x = dst.x + (dst.w - copy_dst.w) / 2;
                    copy_dst.y = dst.y + (dst.h - copy_dst.h) / 2;
                }
                SDL_RenderCopyEx(renderer, video_tex, nullptr, &copy_dst,
                                 angle, nullptr, SDL_FLIP_NONE);
                if (show_pause_badge) {
                    draw_pause_badge(dst);
                }
            }
        } else if (cover_tex && no_chrome) {
            // Cover art centered in the top 70% of the window; the bottom
            // strip holds the metadata text (title / artist / album).
            const int strip_h = std::min(win_h, std::max(win_h / 4, 230));
            const int top_h   = win_h - strip_h;
            SDL_Rect dst = fit_inside(cover_tex_w, cover_tex_h, win_w, top_h);
            SDL_RenderCopy(renderer, cover_tex, nullptr, &dst);

            // Pause indicator: two white bars on a semi-transparent black
            // square, centered on the cover. Only while iOS reports
            // rate: 0 via text/parameters (or POST /rate?value=0).
            if (show_pause_badge) {
                draw_pause_badge(dst);
            }

            // Text lines stacked left-aligned with a small margin.
            const int pad_x = 20;
            int       y_pos = top_h + 10;
            auto draw_text = [&](SDL_Texture* tex, int tw, int th) {
                if (!tex) return;
                int max_w = win_w - 2 * pad_x;
                int w     = std::min(tw, max_w);
                SDL_Rect r{pad_x, y_pos, w, th};
                SDL_RenderCopy(renderer, tex, nullptr, &r);
                y_pos += th + 4;
            };
            draw_text(title_tex,  title_w,  title_h);
            draw_text(artist_tex, artist_w, artist_h);
            draw_text(album_tex,  album_w,  album_h);


        } else if (no_chrome) {
            // Idle: no mirror frame, no cover — show device info.
            auto draw_centered = [&](SDL_Texture* tex, int tw, int th, int y) {
                if (!tex) return;
                SDL_Rect r{(win_w - tw) / 2, y, tw, th};
                SDL_RenderCopy(renderer, tex, nullptr, &r);
            };
            const int total_h = idle_name_h + idle_ip_h + idle_msg_h + 24;
            int y_cursor = (win_h - total_h) / 2;
            draw_centered(idle_name_tex, idle_name_w, idle_name_h, y_cursor);
            y_cursor += idle_name_h + 12;
            draw_centered(idle_ip_tex,   idle_ip_w,   idle_ip_h,   y_cursor);
            y_cursor += idle_ip_h + 12;
            draw_centered(idle_msg_tex,  idle_msg_w,  idle_msg_h,  y_cursor);
        }

        // Restore full-window viewport so ImGui can draw the side panels
        // and status bar across the whole window. win_w / win_h were
        // overridden to stage dims for the SDL drawing pass; bring them
        // back to the full window size for the ImGui pass.
        SDL_RenderSetViewport(renderer, nullptr);
        win_w = full_w;
        win_h = full_h;

        // --- Dear ImGui overlay pass ---------------------------------
        // Runs every frame after the SDL_Render* video pass. The fixed
        // panels (sidebar / options / status) cover everything outside
        // the stage rect; the stage rect itself stays untouched so the
        // video / cover / idle texture is still visible.
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- AppUI: the whole application interface -------------------
        // One object owns every page/widget; the renderer only feeds it
        // state snapshots and action callbacks.
        {
            namespace au = ap::video::app_ui;
            au::Context& c = app_ui->ctx;
            c.scale = g_ui_scale;
            c.receiver_name = receiver_name_buf.data();
            c.device_name = has_active_dev ? active_dev.name : std::string();
            c.device_kind = active_dev.kind;
            c.service_on = service_running();
            c.playing = playing_now;
            c.video_live = have_frame || have_avf;
            c.video_w = hub_->video_w();
            c.video_h = hub_->video_h();
            c.video_fps = hub_->video_fps();
            c.gpu_decode = live_settings_ && live_settings_->mirror_hwaccel.load();
            c.vsync = live_settings_ && live_settings_->vsync_enabled.load();
            c.diagnostics = ui.diagnostics;
            if (live_settings_) {
                c.mirror_w = live_settings_->mirror_width.load();
                c.mirror_h = live_settings_->mirror_height.load();
                c.hevc = live_settings_->hevc_enabled.load();
                c.fps_cap = live_settings_->max_fps.load();
                c.rotation = live_settings_->video_rotation.load();
                c.frame_style = live_settings_->frame_style.load();
            }
            c.res_labels.clear();
            for (const auto& preset : kResolutionPresets)
                c.res_labels.push_back(preset.label);
            c.frame_labels.clear();
            for (const auto& s : frame_sources) c.frame_labels.push_back(s.label);
            const auto rec = recorder_ ? recorder_->status() : VideoRecorder::Status{};
            c.recording = rec.recording;
            c.rec_elapsed_ms = rec.elapsed_ms;
            c.rec_frames = rec.frames;
            c.rec_dropped = rec.dropped;
            c.rec_audio = rec.audio_samples;
            c.rec_path = rec.output_path;
            c.rec_error = rec.error;
            c.rec_codec = rec.codec;
            c.rec_codec_sel = static_cast<int>(recording_options.codec);
            c.rec_encoder_sel = static_cast<int>(recording_options.encoder);
            c.rec_bitrate = recording_options.bitrate_mbps;
            std::snprintf(c.rec_dir_buf, sizeof(c.rec_dir_buf), "%s", recording_dir.data());
            c.firewall_state = static_cast<int>(firewall.state);
            c.firewall_busy = firewall.busy;
            c.firewall_message = firewall.message;
            c.audio_ready = audio_control_available_.load();
            c.audio_pending = audio_command_pending_.load();
            c.audio_failed = audio_command_failed_.load();
            c.prog_elapsed = hub_->prog_elapsed();
            c.prog_total = hub_->prog_total();
            c.track_title = hub_->track_title(); c.track_artist = hub_->track_artist(); c.track_album = hub_->track_album();
            c.cover_w = static_cast<float>(cover_tex_w);
            c.cover_h = static_cast<float>(cover_tex_h);
            c.frames_total = hub_->frames_total();
            c.payload_mbps = hub_->payload_mbps();
            {
                const uint64_t total_b = hub_->payload_bytes_total();
                char buf[32];
                if (total_b < (10ull << 20))
                    std::snprintf(buf, sizeof(buf), "%.1f KB", total_b / 1024.0);
                else if (total_b < (10ull << 30))
                    std::snprintf(buf, sizeof(buf), "%.1f MB", total_b / (1024.0 * 1024.0));
                else
                    std::snprintf(buf, sizeof(buf), "%.2f GB", total_b / (1024.0 * 1024.0 * 1024.0));
                c.payload_total = buf;
            }
            c.win_w = full_w; c.win_h = full_h;
            c.stage_zoom = stage_zoom;
            c.stage_video = show_video && stage_page;

            // actions
            c.act_open_settings = [&]() { app_ui->view.page = au::ViewState::SettingsPage; };
            c.act_disconnect = [&]() { if (disconnect_handler_) disconnect_handler_(); };
            c.act_service = [&](bool on) { if (service_toggle_) service_toggle_(on); };
            c.act_fullscreen = [&]() { toggle_fullscreen(); };
            c.act_hide_chrome = [&]() { ui.chromeless = true; };
            c.act_zoom_in = [&]() { scale_phone(1.1f); };
            c.act_zoom_out = [&]() { scale_phone(1.0f / 1.1f); };
            c.write_zoom = [&](float v) { stage_zoom = std::clamp(v, 0.3f, 3.0f); };
            c.act_toggle_record = [&]() { toggle_recording(); };
            c.act_browse_dir = [&]() {
                std::string selected_folder;
                if (browse_recording_folder(window, recording_dir.data(), selected_folder))
                    std::snprintf(recording_dir.data(), recording_dir.size(), "%s", selected_folder.c_str());
            };
            c.act_prev = [&]() {
                audio_command_failed_.store(false); audio_command_pending_.store(true);
                audio_control_handler_(ap::airplay::MediaCommand::Previous, 0);
            };
            c.act_next = [&]() {
                audio_command_failed_.store(false); audio_command_pending_.store(true);
                audio_control_handler_(ap::airplay::MediaCommand::Next, 0);
            };
            c.act_play_pause = [&]() {
                audio_command_failed_.store(false); audio_command_pending_.store(true);
                audio_control_handler_(ap::airplay::MediaCommand::PlayPause, 0);
            };
            c.act_allow_firewall = [&]() { firewall_monitor.allow_local_network(); };
            c.act_reset_settings = [&]() { reset_all_settings(); };
            c.act_resolution = [&](int index, int is_preset) {
                if (!is_preset || !live_settings_) return;
                if (index < 0 || index >= (int)IM_ARRAYSIZE(kResolutionPresets)) return;
                custom_resolution = false;
                stage_zoom = 1.0f;
                live_settings_->mirror_width.store(kResolutionPresets[index].w);
                live_settings_->mirror_height.store(kResolutionPresets[index].h);
            };
            c.on_custom_res = [&](int w, int h) {
                if (!live_settings_) return;
                custom_resolution = true;
                if (w != live_settings_->mirror_width.load())
                    live_settings_->mirror_width.store(w);
                if (h != live_settings_->mirror_height.load())
                    live_settings_->mirror_height.store(h);
            };
            c.act_rotation = [&](int r) {
                if (live_settings_) live_settings_->video_rotation.store(r);
            };
            c.act_frame_style = [&](int v) {
                if (live_settings_) live_settings_->frame_style.store(v, std::memory_order_relaxed);
            };
            c.write_gpu = [&](bool v) { if (live_settings_) live_settings_->mirror_hwaccel.store(v); };
            c.write_vsync = [&](bool v) { if (live_settings_) live_settings_->vsync_enabled.store(v); };
            c.write_hevc = [&](bool v) { if (live_settings_) live_settings_->hevc_enabled.store(v); };
            c.write_diag = [&](bool v) { ui.diagnostics = v; };
            c.write_name = [&](const std::string& n) {
                if (!live_settings_) return;
                live_settings_->set_receiver_name(n);
                std::snprintf(receiver_name_buf.data(), receiver_name_buf.size(), "%s", n.c_str());
            };
            c.write_fps = [&](int v) {
                if (!live_settings_) return;
                live_settings_->max_fps.store(v);
                live_settings_->refresh_rate.store(v);
            };
            c.write_rec_codec = [&](int v) { recording_options.codec = static_cast<VideoRecorder::VideoCodec>(v); };
            c.write_rec_encoder = [&](int v) { recording_options.encoder = static_cast<VideoRecorder::EncoderMode>(v); };
            c.write_rec_bitrate = [&](int v) { recording_options.bitrate_mbps = v; };
            c.write_rec_dir = [&](const std::string& d) {
                std::snprintf(recording_dir.data(), recording_dir.size(), "%s", d.c_str());
            };

            // cover art: reuse the SDL texture directly as an ImGui image
            if (cover_tex) {
                c.has_cover = true;
                c.cover = (ImTextureID)(intptr_t)cover_tex;
            } else {
                c.has_cover = false;
            }

            app_ui->init(io.FontDefault, small_font, page_font, heading_font, bold_font, g_ui_scale);
            const au::AppUI::Layout lay = app_ui->Frame(
                static_cast<float>(full_w), static_cast<float>(full_h));

            // window controls bubbled from the title bar
            if (app_ui->view.title_min) SDL_MinimizeWindow(window);
            if (app_ui->view.title_max) {
                if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(window);
                else SDL_MaximizeWindow(window);
            }
            if (app_ui->view.title_close) {
                SDL_Event event{}; event.type = SDL_QUIT; SDL_PushEvent(&event);
            }
        }

        publish_preferences();
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(
            ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);

#ifdef HAVE_UI_TESTS
        if (test_engine) {
            ImGuiTestEngine_PostSwap(test_engine);
            int tested = 0, success = 0;
            ImGuiTestEngine_GetResult(test_engine, tested, success);
            if (tested > 0 && !ImGuiTestEngine_GetIO(test_engine).IsRunningTests)
                ui_tests_done_.store(true);
        }
#endif

        // Pipeline-latency measurement: from when the encrypted
        // frame body finished arriving on the wire (origin_ns
        // tagged in MirrorListener) to right after present
        // returns. Captures decrypt + decode + render + VSYNC.
        // Ignored when origin_ns is 0 (HLS / non-mirror frames or
        // older callers that didn't tag).
        if ((have_frame || have_avf) && frame_origin_ns_local > 0) {
            const int64_t now_ns2 =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            const float lat_ms =
                static_cast<float>((now_ns2 - frame_origin_ns_local) / 1e6);
            const float prev_ema = hub_->pipeline_latency_ms_raw();
            const float ema = (prev_ema <= 0.0f)
                                  ? lat_ms
                                  : prev_ema * 0.90f + lat_ms * 0.10f;
            hub_->set_pipeline_latency_ema(ema);
        }
    }

    if (recorder_) recorder_->stop();
    publish_preferences();
    if (title_tex)     SDL_DestroyTexture(title_tex);
    if (artist_tex)    SDL_DestroyTexture(artist_tex);
    if (album_tex)     SDL_DestroyTexture(album_tex);
    if (idle_name_tex) SDL_DestroyTexture(idle_name_tex);
    if (idle_ip_tex)   SDL_DestroyTexture(idle_ip_tex);
    if (idle_msg_tex)  SDL_DestroyTexture(idle_msg_tex);
    gpu_video.reset_target();
    if (video_tex)     SDL_DestroyTexture(video_tex);
    if (cover_tex)     SDL_DestroyTexture(cover_tex);

    if (video_rgba_tex) SDL_DestroyTexture(video_rgba_tex);
    if (body_rgn_) DeleteObject(body_rgn_);
    hotkeys.shutdown();
    for (TTF_Font* f : fonts_big)   if (f) TTF_CloseFont(f);
    for (TTF_Font* f : fonts_small) if (f) TTF_CloseFont(f);
    TTF_Quit();
#ifdef HAVE_UI_TESTS
    if (test_engine) {
        ImGuiTestEngine_Stop(test_engine);
        int tested = 0, success = 0;
        ImGuiTestEngine_GetResult(test_engine, tested, success);
        ui_tests_failed_.store(tested - success);
        LOG_INFO << "UI tests: " << success << "/" << tested << " passed";
    }
#endif
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
#ifdef HAVE_UI_TESTS
    // The test engine must be destroyed AFTER the ImGui context so its
    // per-test ini data can be written out.
    if (test_engine) ImGuiTestEngine_DestroyContext(test_engine);
#endif
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    LOG_INFO << "VideoRenderer stopped";
}


} // namespace ap::video
