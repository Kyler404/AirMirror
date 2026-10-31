#include "qtui/device_frame.h"
#include "video/frame_manifest.h"
#include "log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../video/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace ap::ui {

bool DeviceFrame::load(const std::filesystem::path& png) {
    rgba_.clear();
    hole_alpha_.clear();
    out_.clear();
    w_ = h_ = 0;

    FILE* fp = _wfopen(png.c_str(), L"rb");
    if (!fp) return false;
    int channels = 0;
    unsigned char* data = stbi_load_from_file(fp, &w_, &h_, &channels, 4);
    fclose(fp);
    if (!data || w_ <= 0 || h_ <= 0) {
        if (data) stbi_image_free(data);
        return false;
    }
    rgba_.assign(data, data + static_cast<size_t>(w_) * h_ * 4);
    stbi_image_free(data);

    // Screen hole rect from the build-time manifest (same source as the
    // SDL renderer); fixed fallback for unlisted assets.
    const FrameScreenRect* m = find_frame_screen(png.stem().string().c_str());
    int sx0, sy0, sw0, sh0;
    if (m) {
        sx0 = m->sx; sy0 = m->sy; sw0 = m->sw; sh0 = m->sh;
    } else {
        LOG_WARN << "no frame manifest entry: " << png.stem().string();
        sx0 = int(w_ * 0.053333f); sy0 = int(h_ * 0.025f);
        sw0 = int(w_ * 0.893333f); sh0 = int(h_ * 0.95f);
    }
    hx_ = sx0; hy_ = sy0; hw_ = sw0; hh_ = sh0;

    // Hole shape mask: flood fill transparent pixels from the image
    // centre (identical to the legacy renderer) so the mirror frame is
    // clipped to the hole's rounded corners.
    auto alpha = [&](int x, int y) {
        return rgba_[(static_cast<size_t>(y) * w_ + x) * 4 + 3];
    };
    std::vector<unsigned char> inside(static_cast<size_t>(w_) * h_, 0);
    std::vector<int> stack;
    auto push = [&](int x, int y) {
        if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
        const int i = y * w_ + x;
        if (!inside[i] && alpha(x, y) < 32) { inside[i] = 1; stack.push_back(i); }
    };
    push(w_ / 2, h_ / 2);
    while (!stack.empty()) {
        const int i = stack.back(); stack.pop_back();
        const int x = i % w_, y = i / w_;
        push(x - 1, y); push(x + 1, y); push(x, y - 1); push(x, y + 1);
    }
    hole_alpha_.assign(static_cast<size_t>(hw_) * hh_, 0);
    for (int y = 0; y < hh_; ++y)
        for (int x = 0; x < hw_; ++x)
            hole_alpha_[static_cast<size_t>(y) * hw_ + x] =
                inside[static_cast<size_t>(hy_ + y) * w_ + (hx_ + x)] ? 255 : 0;

    // Floating cut-outs INSIDE the screen hole (dynamic island, home
    // indicator) are opaque blobs surrounded by hole. The flood fill above
    // treats them as obstacles, so the shell would paint them OVER the
    // mirror video — but the mirrored image already contains its own
    // island/home indicator, producing a double. Classify opaque
    // connected components of the hole rect: any component whose bounding
    // box does not touch the rect border belongs to a floating cut-out —
    // punch it into the hole mask so the video shows through.
    {
        const int rw = hw_, rh = hh_;
        std::vector<unsigned char> blocked(static_cast<size_t>(rw) * rh, 0);
        for (int y = 0; y < rh; ++y)
            for (int x = 0; x < rw; ++x)
                if (alpha(hx_ + x, hy_ + y) >= 32 &&
                    !inside[static_cast<size_t>(hy_ + y) * w_ + (hx_ + x)])
                    blocked[static_cast<size_t>(y) * rw + x] = 1;
        std::vector<int> comp(static_cast<size_t>(rw) * rh, -1);
        std::vector<int> queue;
        int ncomp = 0;
        std::vector<char> touches_border;
        for (int y0 = 0; y0 < rh; ++y0) {
            for (int x0 = 0; x0 < rw; ++x0) {
                const size_t i0 = static_cast<size_t>(y0) * rw + x0;
                if (!blocked[i0] || comp[i0] >= 0) continue;
                const int id = ncomp++;
                touches_border.push_back(0);
                comp[i0] = id;
                queue.clear();
                queue.push_back(static_cast<int>(i0));
                while (!queue.empty()) {
                    const int idx = queue.back(); queue.pop_back();
                    const int cx = idx % rw, cy = idx / rw;
                    if (cx <= 1 || cy <= 1 || cx >= rw - 2 || cy >= rh - 2)
                        touches_border[id] = 1;
                    constexpr int kdx[4] = {-1, 1, 0, 0};
                    constexpr int kdy[4] = {0, 0, -1, 1};
                    for (int d = 0; d < 4; ++d) {
                        const int nx = cx + kdx[d], ny = cy + kdy[d];
                        if (nx < 0 || nx >= rw || ny < 0 || ny >= rh) continue;
                        const size_t ni = static_cast<size_t>(ny) * rw + nx;
                        if (blocked[ni] && comp[ni] < 0) {
                            comp[ni] = id;
                            queue.push_back(static_cast<int>(ni));
                        }
                    }
                }
            }
        }
        for (int y = 0; y < rh; ++y) {
            for (int x = 0; x < rw; ++x) {
                const size_t i = static_cast<size_t>(y) * rw + x;
                if (blocked[i] && !touches_border[comp[i]])
                    hole_alpha_[i] = 255;   // island / home indicator: show video
            }
        }
    }

    // Body outline: first/last opaque pixel per row (for SetWindowRgn).
    row_x0_.assign(h_, -1);
    row_x1_.assign(h_, -1);
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x)
            if (alpha(x, y) >= 250) { if (row_x0_[y] < 0) row_x0_[y] = x; row_x1_[y] = x; }
    }

    // Some assets fill the area OUTSIDE the body silhouette with opaque
    // white (alpha=255) instead of transparent — in pure mode that paints
    // white/black bands around the phone. Kill everything outside the
    // body outline: pixels left of row_x0 or right of row_x1 are not part
    // of the device.
    for (int y = 0; y < h_; ++y) {
        if (row_x0_[y] < 0) {
            // fully transparent row: zero the whole strip
            std::memset(&rgba_[static_cast<size_t>(y) * w_ * 4], 0,
                        static_cast<size_t>(w_) * 4);
            continue;
        }
        for (int x = 0; x < row_x0_[y]; ++x)
            std::memset(&rgba_[(static_cast<size_t>(y) * w_ + x) * 4], 0, 4);
        for (int x = row_x1_[y] + 1; x < w_; ++x)
            std::memset(&rgba_[(static_cast<size_t>(y) * w_ + x) * 4], 0, 4);
    }

    out_.assign(rgba_.size(), 0);
    LOG_INFO << "Qt shell loaded: " << png.filename().string()
             << " (" << w_ << "x" << h_ << ", hole " << hw_ << "x" << hh_ << ")";
    return true;
}

const std::vector<uint8_t>& DeviceFrame::composite(const uint8_t* video_rgba,
                                                   int vw, int vh, float zoom) {
    if (!valid() || hw_ <= 0 || hh_ <= 0 || vw <= 0 || vh <= 0) {
        out_.clear();
        return out_;
    }
    // Cover the hole with the video (aspect-fill), scaled by zoom about
    // the centre, then clip to the hole mask. Cover = the LARGER scale
    // factor, so the video always covers the whole hole and the overflow
    // crops — using min here would letterbox (transparent bands at the
    // hole edges, visible as desktop-showing stripes in pure mode).
    const float scale = std::max(float(hw_) * zoom / float(vw),
                                 float(hh_) * zoom / float(vh));
    const float cw = float(vw) * scale;
    const float ch = float(vh) * scale;
    const float fx = (float(hw_) - cw) / 2.f;
    const float fy = (float(hh_) - ch) / 2.f;

    // Start from the shell PNG itself (transparent margins included).
    out_ = rgba_;
    auto* out = out_.data();
    // Hole pixel (x, y) samples the video at ((x-fx)*vw/cw, (y-fy)*vh/ch)
    // — the cover-fit rect (cw × ch at offset fx, fy) maps video space to
    // hole space; sampling must go the other way.
    const float sx_step = float(vw) / cw;
    const float sy_step = float(vh) / ch;
    for (int y = 0; y < hh_; ++y) {
        const int sy = int((y - fy) * sy_step);
        if (sy < 0 || sy >= vh) continue;
        const uint8_t* vrow = video_rgba + static_cast<size_t>(sy) * vw * 4;
        const uint8_t* mrow = hole_alpha_.data() + static_cast<size_t>(y) * hw_;
        uint8_t* orow = out + (static_cast<size_t>(hy_ + y) * w_ + hx_) * 4;
        for (int x = 0; x < hw_; ++x) {
            if (!mrow[x]) continue;                    // outside hole: keep shell
            const int sx = int((x - fx) * sx_step);
            if (sx < 0 || sx >= vw) continue;
            const uint8_t* p = vrow + static_cast<size_t>(sx) * 4;
            uint8_t* q = orow + static_cast<size_t>(x) * 4;
            q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = 255;
        }
    }
    return out_;
}

namespace {

// Apple identifier -> marketing name (kept in sync with the legacy
// renderer's kModelAliases table).
struct ModelAlias { const char* identifier; const char* marketing; };
const ModelAlias kModelAliases[] = {
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
    {"iphone16,2", "iPhone 16 Pro Max"},   // 15 Pro family: no own asset —
    {"iphone16,1", "iPhone 16 Pro"},       //   reuse the 16 Pro chassis PNGs
    {"iphone15,5", "iPhone 16 Plus"},      // 15/15 Plus: 16-shaped chassis
    {"iphone15,4", "iPhone 16"},
    {"ipad18,4",   "iPad Air 13_ (M4)"},
    {"ipad18,3",   "iPad Air 11_ (M4)"},
    {"ipad17,2",   "iPad Air 13_ (M4)"},   // Air M3 (11"/13"): same chassis
    {"ipad17,1",   "iPad Air 11_ (M4)"},   //   family as the M4 assets
    {"ipad17,4",   "iPad Pro (M5) 13_"},
    {"ipad17,3",   "iPad Pro (M5) 11_"},
    {"ipad15,6",   "iPad (A16)"},
    {"ipad15,5",   "iPad (A16)"},
    {"ipad16,1",   "iPad mini (A17 Pro)"},
};

std::string lower_copy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

void DeviceFrame::body_bounds(int& x, int& y, int& w, int& h) const {
    x = y = w = h = 0;
    if (!valid()) return;
    int top = -1, bot = -1, lo = w_, hi = 0;
    for (int yy = 0; yy < h_; ++yy) {
        if (row_x0_[yy] < 0) continue;
        if (top < 0) top = yy;
        bot = yy;
        lo = std::min(lo, row_x0_[yy]);
        hi = std::max(hi, row_x1_[yy] + 1);
    }
    if (top < 0) return;
    x = lo; y = top; w = hi - lo; h = bot - top + 1;
}

float DeviceFrame::corner_radius() const {
    if (!valid()) return 0.f;
    int x, y, w, h;
    body_bounds(x, y, w, h);
    const int bot = y + h - 1;
    int r = 0;
    if (row_x0_[y]  >= 0) r = std::max(r, row_x0_[y]  - x);            // TL
    if (row_x1_[y]  >= 0) r = std::max(r, x + w - 1 - row_x1_[y]);     // TR
    if (row_x0_[bot] >= 0) r = std::max(r, row_x0_[bot] - x);          // BL
    if (row_x1_[bot] >= 0) r = std::max(r, x + w - 1 - row_x1_[bot]);  // BR
    return float(r);
}

std::filesystem::path find_shell_png(const std::filesystem::path& dir,
                                     bool landscape,
                                     const std::string& model_identifier,
                                     const std::string& previous_label) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return {};
    std::map<std::string, std::filesystem::path> portrait, landscape_map;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        std::string ext = path.extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".png") continue;
        std::string stem = path.stem().string();
        if (stem.size() > 12 && stem.compare(stem.size() - 12, 12, " - Landscape") == 0)
            landscape_map[stem.substr(0, stem.size() - 12)] = path;
        else if (stem.size() > 11 && stem.compare(stem.size() - 11, 11, " - Portrait") == 0)
            portrait[stem.substr(0, stem.size() - 11)] = path;
    }
    auto& chosen = landscape ? landscape_map : portrait;
    if (chosen.empty()) return {};

    // Identifier -> marketing name.
    const std::string ident = lower_copy(model_identifier);
    const char* marketing = nullptr;
    for (const auto& alias : kModelAliases)
        if (ident == alias.identifier) { marketing = alias.marketing; break; }
    if (!marketing) return chosen.begin()->second;   // unknown model: first

    // Word-boundary prefix match on the group label.
    const std::string want = lower_copy(marketing);
    const auto matches = [&](const std::string& label) {
        const std::string lo = lower_copy(label);
        if (lo.size() < want.size()) return false;
        if (lo.compare(0, want.size(), want) != 0) return false;
        return lo.size() == want.size() || lo.compare(want.size(), 3, " - ") == 0;
    };
    // Colour continuity from the previous shell label.
    std::string color;
    {
        const size_t cs = previous_label.find(" - ");
        if (cs != std::string::npos) color = lower_copy(previous_label.substr(cs + 3));
    }
    const std::filesystem::path* first = nullptr;
    const std::filesystem::path* same_color = nullptr;
    for (const auto& [label, path] : chosen) {
        if (!matches(label)) continue;
        if (!first) first = &path;
        if (!same_color && !color.empty()) {
            const size_t cs = lower_copy(label).find(" - ");
            if (cs != std::string::npos &&
                lower_copy(label).compare(cs + 3, color.size(), color) == 0)
                same_color = &path;
        }
    }
    if (same_color) return *same_color;
    if (first) return *first;
    return chosen.begin()->second;
}

} // namespace ap::ui
