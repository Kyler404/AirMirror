#include "qtui/video_canvas.h"

#include <chrono>
#include "qtui/device_frame.h"
#include "airplay/live_settings.h"
#include "log.h"
#include <QPainter>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace ap::ui {

VideoCanvas::VideoCanvas(ap::video::StreamHub& hub, QWidget* parent)
    : QOpenGLWidget(parent), hub_(hub) {
    live_ = hub.live_settings();
    // RGB32 texture upload — works on any GL context; no version games.
    startTimer(33);
}

void VideoCanvas::body_bounds(int& w, int& h) const {
    w = h = 0;
    if (!shell_ || !shell_->valid()) return;
    const std::vector<int>& x0 = shell_->row_x0();
    const std::vector<int>& x1 = shell_->row_x1();
    int top = -1, bot = -1, lo = shell_->width(), hi = 0;
    for (int y = 0; y < shell_->height(); ++y) {
        if (x0[y] < 0) continue;
        if (top < 0) top = y;
        bot = y;
        lo = std::min(lo, x0[y]);
        hi = std::max(hi, x1[y] + 1);
    }
    if (top < 0) return;
    w = hi - lo;
    h = bot - top + 1;
}

QRegion VideoCanvas::shell_region(int win_w, int win_h) const {
    // TRUE silhouette region: walk the per-row outline and emit a polygon
    // that follows its actual shape (rounded corners AND side buttons).
    // Row-rect union looked jagged because every row is a 1px strip; here
    // we keep only rows where the span actually changes (turning points),
    // which reproduces the silhouette with far fewer, longer edges.
    if (!shell_ || !shell_->valid() || win_w <= 0 || win_h <= 0)
        return QRegion();
    const float sx = float(win_w) / float(shell_->width());
    const float sy = float(win_h) / float(shell_->height());
    QRegion rgn;
    const std::vector<int>& x0 = shell_->row_x0();
    const std::vector<int>& x1 = shell_->row_x1();
    const int H = shell_->height();

    int prev_l = -1, prev_r = -1, run_start = 0;
    auto flush = [&](int y_end_excl) {
        if (prev_l < 0) return;
        const int y0 = run_start, y1 = y_end_excl - 1;
        // Emit the run as a trapezoid: (prev_l..prev_r) at top and bottom
        // rows of the run. Long straight runs become one rect; corner
        // curves become many 1..2px runs — but ONLY where the outline
        // curves, so straight edges stay clean.
        rgn += QRect(int(prev_l * sx), int(y0 * sy),
                     int((prev_r - prev_l + 1) * sx) + 1,
                     int((y1 - y0 + 1) * sy) + 1);
    };
    for (int y = 0; y < H; ++y) {
        if (x0[y] < 0) { flush(y); prev_l = prev_r = -1; continue; }
        if (x0[y] != prev_l || x1[y] != prev_r) {
            flush(y);
            prev_l = x0[y]; prev_r = x1[y];
            run_start = y;
        }
    }
    flush(H);
    return rgn;
}

void VideoCanvas::timerEvent(QTimerEvent*) {
    if (isVisible()) {
        // TEMP probe: log geometry transitions in fill mode
        static int last_w = -1, last_h = -1;
        if (fill_mode_ && (width() != last_w || height() != last_h)) {
            last_w = width(); last_h = height();
            LOG_INFO << "canvas geom: " << last_w << "x" << last_h
                     << " dpr=" << devicePixelRatioF()
                     << " mapToWindow=" << mapTo(window(), QPoint(0, 0)).y();
        }
        update();
    }
}

void VideoCanvas::initializeGL() {
    initializeOpenGLFunctions();
    gl11_.initializeOpenGLFunctions();   // fixed-function entry points
    glDisable(GL_DEPTH_TEST);
    glGenTextures(1, &tex_y_);          // reuse tex_y_ as the RGBA texture
    glBindTexture(GL_TEXTURE_2D, tex_y_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoCanvas::resizeGL(int w, int h) {
    Q_UNUSED(w); Q_UNUSED(h);
}

void VideoCanvas::destroy_textures() {
    frame_w_ = frame_h_ = 0;
    if (sws_) { sws_freeContext(static_cast<SwsContext*>(sws_)); sws_ = nullptr; }
}

void VideoCanvas::paintGL() {
    // Idle clear colour must equal the page background (#F5F5F3). Any other
    // value leaves a huge rectangle visible across the whole stage before the
    // first frame arrives — the old #E8EEEA read as a slab of pale green.
    if (fill_mode_) glClearColor(0.f, 0.f, 0.f, 0.f);   // desktop shows through
    else         glClearColor(246.0f/255, 246.0f/255, 244.0f/255, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    auto snap = hub_.ui_snapshot();
    if (!snap.has_frame || snap.frame.y.empty()) return;
    const auto& f = snap.frame;
    const int w = f.width, h = f.height;
    if (w <= 0 || h <= 0) return;

    // (Re)load the shell when the device model or orientation changes.
    ap::video::StreamHub::DeviceInfo dev;
    const std::string dev_model = hub_.active_device_snapshot(dev) ? dev.model : std::string();
    if (shell_loaded_for_ != (w >= h ? 1 : 0) || dev_model != model_hint_) {
        const auto shell_t0 = std::chrono::steady_clock::now();
        shell_loaded_for_ = (w >= h) ? 1 : 0;
        auto png = find_shell_png(assets_dir_, w >= h, dev_model, shell_label_);
        if (!png.empty()) {
            if (!shell_) shell_ = std::make_unique<DeviceFrame>();
            if (shell_->load(png)) {
                shell_label_ = png.stem().string().substr(
                    0, png.stem().string().find(" - "));
            }
        }
        model_hint_ = dev_model;
        const auto shell_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - shell_t0).count();
        LOG_INFO << "shell switch cost: " << shell_ms << " ms";
    }

    // (Re)create conversion context when the geometry or layout changes.
    const int src_fmt = f.nv12 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
    if (!sws_ || w != frame_w_ || h != frame_h_ || src_fmt != src_fmt_) {
        if (sws_) { sws_freeContext(static_cast<SwsContext*>(sws_)); sws_ = nullptr; }
        sws_ = sws_getContext(w, h, static_cast<AVPixelFormat>(src_fmt),
                              w, h, AV_PIX_FMT_RGBA,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) { LOG_WARN << "VideoCanvas: sws ctx failed"; return; }
        frame_w_ = w; frame_h_ = h; src_fmt_ = src_fmt;
        rgba_.resize(static_cast<size_t>(w) * h * 4);
    }
    const uint8_t* src_planes[4] = {
        f.y.data(),
        f.nv12 ? f.uv.data() : f.u.data(),
        f.nv12 ? nullptr : f.v.data(),
        nullptr};
    const int src_strides[4] = {
        f.y_stride,
        f.nv12 ? f.uv_stride : f.u_stride,
        f.nv12 ? 0 : f.v_stride,
        0};
    uint8_t* dst_planes[4] = { rgba_.data(), nullptr, nullptr, nullptr };
    const int dst_strides[4] = { w * 4, 0, 0, 0 };
    sws_scale(static_cast<SwsContext*>(sws_), src_planes, src_strides,
              0, h, dst_planes, dst_strides);

    // Optional device shell: composite the mirror frame into the phone
    // PNG (rounded hole clip) and draw THAT instead of the bare video.
    // Shell choice follows the frame style setting: -1 = off, else auto.
    const uint8_t* draw_data = rgba_.data();
    int draw_w = w, draw_h = h;
    bool shell_on = false;
    int body_x = 0, body_y = 0;
    const uint8_t* shell_base_ = nullptr;
    if (shell_ && shell_->valid() && live_ &&
        live_->frame_style.load(std::memory_order_relaxed) != -1) {
        const auto& comp = shell_->composite(rgba_.data(), w, h, zoom_);
        if (!comp.empty()) {
            shell_->body_bounds(body_x, body_y, draw_w, draw_h);
            shell_base_ = comp.data();   // GL skips handle the crop
            shell_on = true;
        }
    }

    // Upload RGBA and draw with the fixed pipeline. Fill mode uploads
    // EXACTLY the body bbox (SKIP_PIXELS/SKIP_ROWS/ROW_LENGTH): zero
    // transparent margin in the texture, so the window edge == shell edge.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_y_);
    gl11_.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    if (shell_on) {
        const uint8_t* base = shell_->composite_base();
        glPixelStorei(GL_UNPACK_ROW_LENGTH, shell_->width());
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, body_x);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, body_y);
        if (rgba_tex_w_ != draw_w || rgba_tex_h_ != draw_h) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, draw_w, draw_h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, base);
            rgba_tex_w_ = draw_w; rgba_tex_h_ = draw_h;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, draw_w, draw_h,
                            GL_RGBA, GL_UNSIGNED_BYTE, base);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    } else {
        if (rgba_tex_w_ != draw_w || rgba_tex_h_ != draw_h) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, draw_w, draw_h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, draw_data);
            rgba_tex_w_ = draw_w; rgba_tex_h_ = draw_h;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, draw_w, draw_h,
                            GL_RGBA, GL_UNSIGNED_BYTE, draw_data);
        }
    }

    // Fit-inside rect (aspect-preserving) with zoom; pure mode stretches
    // the composite over the whole widget so the window region (cut from
    // the same shell) aligns with the drawn pixels exactly.
    float dw = 1.0f, dh = 1.0f;
    if (!fill_mode_) {
        const float widget_ar = float(width()) / std::max(1, height());
        const float frame_ar  = float(draw_w) / std::max(1, draw_h);
        if (frame_ar > widget_ar) dh = widget_ar / frame_ar;
        else                      dw = frame_ar / widget_ar;
        dw *= zoom_; dh *= zoom_;
    }

    gl11_.glEnable(GL_TEXTURE_2D);
    // Pure mode (fill): REPLACE copies source RGBA — including the shell's
    // per-pixel alpha — verbatim into the framebuffer; the window
    // compositor blends with the desktop. GL_BLEND here would square the
    // semi-transparent edge pixels (dst_a = src_a²) and dim the outline.
    // Chrome mode: blend the shell OVER the opaque cleared background, or
    // the PNG's transparent margins would paint the framebuffer black.
    const bool blend_shell = shell_on && !fill_mode_;
    if (blend_shell) {
        gl11_.glEnable(GL_BLEND);
        gl11_.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    gl11_.glBegin(GL_QUADS);
    gl11_.glTexCoord2f(0.f, 1.f); gl11_.glVertex2f(-dw, -dh);
    gl11_.glTexCoord2f(1.f, 1.f); gl11_.glVertex2f( dw, -dh);
    gl11_.glTexCoord2f(1.f, 0.f); gl11_.glVertex2f( dw,  dh);
    gl11_.glTexCoord2f(0.f, 0.f); gl11_.glVertex2f(-dw,  dh);
    gl11_.glEnd();
    if (blend_shell) gl11_.glDisable(GL_BLEND);
    gl11_.glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    hub_.note_frame_presented(f.origin_ns);
}

} // namespace ap::ui
