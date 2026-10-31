#pragma once

// video_canvas.h — QOpenGLWidget that draws the StreamHub's latest
// YUV frame with a GLSL BT.601 shader (the same colour convention the
// SDL YUV path used). The hub's latest-wins slot semantics are kept:
// paintGL drains the snapshot; producers overwrite freely.

#include "video/stream_hub.h"
#include "qtui/device_frame.h"
#include <QOpenGLWidget>
#include <memory>
#include <string>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_1_1>

// Plain GL texture names (no QOpenGLTexture: its storage API is awkward
// for single-channel YUV planes).
using GLTex = GLuint;

namespace ap::ui {

class VideoCanvas : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit VideoCanvas(ap::video::StreamHub& hub, QWidget* parent = nullptr);

    // Zoom: 1.0 = fit inside the widget, >1 crops in.
    void set_zoom(float z) { zoom_ = z; update(); }

    // Device shell (bezel) configuration; call before the first frame.
    void set_assets_dir(const std::wstring& dir) { assets_dir_ = dir; }
    // Aspect (w/h) of the currently loaded shell, or -1 when none.
    float shell_aspect() const { return shell_ && shell_->valid() ? shell_->aspect() : -1.f; }
    // Window-clipping region shaped as the phone silhouette, scaled to
    // the given window size. Empty when no shell is loaded.
    QRegion shell_region(int win_w, int win_h) const;
    // Visible body bounding box in shell pixels (excludes transparent
    // margins around the asset).
    void body_bounds(int& w, int& h) const;
    // Label of the loaded shell ("iPhone 17 Pro Max"), empty when none.
    QString shell_name() const { return QString::fromStdString(shell_label_); }
    // Measured shell corner radius (shell px) and body bbox (for the
    // window-clip region). Delegates to DeviceFrame.
    float shell_corner_radius() const {
        return shell_ && shell_->valid() ? shell_->corner_radius() : 0.f; }
    void shell_body_bounds(int& x, int& y, int& w, int& h) const {
        if (shell_ && shell_->valid()) shell_->body_bounds(x, y, w, h);
        else { x = y = w = h = 0; } }
    // Pure (frameless) mode: draw the composite stretched to the whole
    // widget so the outline region and the image align 1:1.
    void set_fill_mode(bool on) { fill_mode_ = on; }
    void set_model_hint(const std::string& model) { model_hint_ = model; }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void timerEvent(QTimerEvent*) override;

private:
    void destroy_textures();

    ap::video::StreamHub& hub_;
    ap::airplay::LiveSettings* live_ = nullptr;   // frame_style switch
    GLTex tex_y_ = 0;                    // RGBA video texture (reuses slot)
    QOpenGLFunctions_1_1 gl11_;          // fixed-function entry points

    std::unique_ptr<DeviceFrame> shell_;
    bool fill_mode_ = false;            // stretch composite to widget
    int  shell_loaded_for_ = -1;         // 0 portrait, 1 landscape
    std::wstring assets_dir_;
    std::string  model_hint_;            // last shell's device model
    std::string  shell_label_;           // e.g. "iPhone 17 Pro Max" (colour continuity)

    void* sws_ = nullptr;                // SwsContext*
    std::vector<uint8_t> rgba_;          // CPU-converted frame
    int rgba_tex_w_ = 0, rgba_tex_h_ = 0;
    int src_fmt_ = -1;
    int frame_w_ = 0, frame_h_ = 0;
    float zoom_ = 1.0f;
};

} // namespace ap::ui
