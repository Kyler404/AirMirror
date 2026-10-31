#pragma once

// device_frame.h — Qt-front-end device shell (bezel) compositing.
// Port of the legacy SDL renderer's frame pipeline to plain CPU RGBA:
// the phone PNG is the positioning container, the mirror frame is drawn
// into the screen hole (clipped to the hole's rounded shape), and the
// shell PNG is stamped on top. All math carried over unchanged.

#include <string>
#include <vector>
#include <filesystem>

namespace ap::ui {

class DeviceFrame {
public:
    // Load a PNG pair (" - Portrait"/" - Landscape") from assets/frame.
    // Returns false when the file is missing/undecodable.
    bool load(const std::filesystem::path& png);

    bool valid() const { return !rgba_.empty(); }
    int  width()  const { return w_; }
    int  height() const { return h_; }

    // Composite the video (RGBA, w×h) into the screen hole. Output:
    // shell-sized RGBA with alpha (caller uploads it with GL_RGBA).
    // Reuses internal buffers; valid until the next call.
    const std::vector<uint8_t>& composite(const uint8_t* video_rgba,
                                          int vw, int vh,
                                          float zoom);

    // Aspect of the full shell (for canvas letterboxing decisions).
    float aspect() const { return h_ > 0 ? float(w_) / float(h_) : 1.f; }

    // Per-row opaque span (body outline, incl. side buttons) in shell
    // pixels — used to size the window to the visible body.
    const std::vector<int>& row_x0() const { return row_x0_; }
    const std::vector<int>& row_x1() const { return row_x1_; }
    // Outline bounding box in shell pixels {x, y, w, h}; empty shell → 0s.
    void body_bounds(int& x, int& y, int& w, int& h) const;
    // Corner radius of the body silhouette in shell pixels (measured from
    // the alpha outline: first-body-row inset). 0 when unknown.
    float corner_radius() const;
    // Base pointer of the last composite output (for GL skip-upload).
    const uint8_t* composite_base() const { return out_.data(); }

private:
    std::vector<uint8_t> rgba_;          // shell PNG (decoded once)
    std::vector<uint8_t> hole_alpha_;    // per-screen-pixel 255/0 mask
    std::vector<uint8_t> out_;           // composite output
    int w_ = 0, h_ = 0;
    int hx_ = 0, hy_ = 0, hw_ = 0, hh_ = 0;   // screen hole rect in pixels
    std::vector<int> row_x0_, row_x1_;        // body outline per row
};

// Pick the shell PNG for a device: "iPhone18,2"-style identifiers map
// through the marketing-name alias table (same as the legacy renderer),
// then the label prefix is matched against assets/frame groups. Colour
// continuity: when previous_label has a colour and a matching group
// exists, it is kept.
std::filesystem::path find_shell_png(const std::filesystem::path& dir,
                                     bool landscape,
                                     const std::string& model_identifier,
                                     const std::string& previous_label = {});

} // namespace ap::ui
