#pragma once

// stream_hub.h — pure data plane shared by any UI front end.
//
// Everything the old SDL renderer knew about *streams* lives here:
// the latest-wins frame slots (I420 / NV12 / refcounted AVFrame),
// cover-art / metadata / progress buffers, bandwidth + FPS EMA
// accounting, active-device tracking and the recorder feed. No SDL,
// no window, no threads of its own — the UI front end drains it.
//
// Producers (MirrorListener / routes / main) keep calling exactly the
// same entry points they called on VideoRenderer.

#include "video/video_renderer.h"   // DecodedFrame lives here (shared RAII)
#include "airplay/media_control.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
struct AVFrame;
struct AVBufferRef;
// av_buffer_ref / av_buffer_unref are NOT declared here: stream_hub.cpp
// includes libavutil/buffer.h; header users only pass pointers around.
}

namespace ap::airplay { struct LiveSettings; }
namespace ap::video { class VideoRecorder; }

namespace ap::video {

// One decoded video frame, fully owned by the consumer for the duration
// of a draw. ui_snapshot() DEEP-COPIES the planes: the hub's internal
// buffers are overwritten by producers at stream rate, while the UI
// consumes at paint rate — shared pointers would race with the next
// push (use-after-free). The copy costs one memcpy per drawn frame.
struct FrameView {
    std::vector<uint8_t> y, u, v, uv;
    int y_stride = 0, u_stride = 0, v_stride = 0, uv_stride = 0;
    int width = 0, height = 0;
    bool nv12 = false;
    int64_t origin_ns = 0;
};

class StreamHub {
public:
    StreamHub();
    ~StreamHub();

    StreamHub(const StreamHub&)            = delete;
    StreamHub& operator=(const StreamHub&) = delete;

    // ---- producers (thread-safe, called from RTSP/decode threads) ----

    void push_frame(const uint8_t* y, int y_stride,
                    const uint8_t* u, int u_stride,
                    const uint8_t* v, int v_stride,
                    int width, int height,
                    int64_t origin_ns = 0);

    void push_frame_nv12(const uint8_t* y,  int y_stride,
                         const uint8_t* uv, int uv_stride,
                         int width, int height,
                         int64_t origin_ns = 0);

    void push_avframe(const AVFrame* src, int64_t origin_ns = 0);

    // Caller owns the returned FFmpeg ref. Null when D3D11 is unavailable.
    AVBufferRef* gpu_device_ref();

    void push_audio_pcm(const int16_t* samples, int sample_count,
                        int sample_rate, int channels);

    void record_payload_bytes(std::size_t n);

    void push_cover_art(const uint8_t* jpeg, std::size_t size);

    void push_metadata(const std::string& title,
                       const std::string& artist,
                       const std::string& album);

    void push_progress(uint32_t elapsed_ms, uint32_t total_ms);
    void push_playback_rate(float rate);

    void clear_session();

    void note_flush();
    bool in_flush_grace() const;

    void set_idle_info(const std::string& name, const std::string& ip);

    struct DeviceInfo {
        std::string peer_ip;
        std::string session_id;
        std::string kind;        // "Mirror", "HLS", "Audio"
        std::string name;        // from SETUP plist
        std::string model;       // "iPhone15,3"
    };
    void set_active_device(DeviceInfo info);
    void clear_active_device();
    bool clear_session_if_active(const std::string& session_id);
    bool active_device_snapshot(DeviceInfo& out) const;

    void set_live_settings(ap::airplay::LiveSettings* live) { live_settings_ = live; }
    ap::airplay::LiveSettings* live_settings() const { return live_settings_; }

    // ---- recorder (owned here; UI toggles it) -------------------------

    VideoRecorder* recorder() { return recorder_.get(); }

    // ---- consumer side (UI thread) -------------------------------------

    // Snapshot everything the UI needs in one lock pass. Copies plane
    // pointers (not pixel data); the consumer must draw before the next
    // push overwrites the slot — latest-wins, same semantics as before.
    struct UiSnapshot {
        FrameView frame;   // planes deep-copied; see FrameView comment
        bool      has_frame = false;
        // cover
        bool         has_cover = false;
        std::vector<unsigned char> cover_jpeg;   // decoded lazily by UI
        // metadata
        std::string track_title, track_artist, track_album;
        // idle info
        std::string idle_name, idle_ip;
        // playback state
        uint32_t prog_elapsed = 0, prog_total = 0;
        bool     playing = true;
        int64_t  progress_pushed_at_ns = 0;
    };
    UiSnapshot ui_snapshot();

    // Render-thread consumption: moves the pending frame slot out (the
    // SDL legacy thread needs ownership of the AVFrame ref), plus the
    // dirty flags for cover/metadata. Mirrors the old renderer's
    // mutex-protected consume block.
    struct RenderConsume {
        DecodedFrame   avf;             // owns the ref when has_avf
        bool           has_avf = false;
        std::vector<unsigned char> y, u, v, uv;
        bool           has_planes = false;
        bool           nv12 = false;
        int            w = 0, h = 0;
        int64_t        origin_ns = 0;
        bool           cover_dirty = false;
        std::vector<unsigned char> cover;
        bool           meta_dirty = false;
        std::string    title, artist, album;
        bool           clear_cover = false;
        bool           idle_dirty = false;
        std::string    idle_name, idle_ip;
    };
    RenderConsume render_consume();
    bool has_pending() const {
        return has_frame_ || has_avframe_ || cover_dirty_ || meta_dirty_
            || idle_dirty_
            || clear_cover_requested_.load(std::memory_order_relaxed);
    }

    // GPU device plumbing (D3D11VA decode path hands its device here).
    void set_gpu_device(AVBufferRef* ref);
    // Qt front end: no renderer thread publishes a D3D11 device — skip
    // the publish-wait in gpu_device_ref().
    void set_headless_gpu(bool on) { headless_gpu_ = on; }

    // ---- stats (atomics, read anywhere) --------------------------------

    float video_fps() const { return video_fps_ema_.load(std::memory_order_relaxed); }
    int   video_w() const { return video_w_seen_.load(std::memory_order_relaxed); }
    int   video_h() const { return video_h_seen_.load(std::memory_order_relaxed); }
    bool  video_live() const {
        return frames_total_.load(std::memory_order_relaxed) > 0 &&
               video_fps_ema_.load(std::memory_order_relaxed) > 0.5f;
    }
    uint64_t frames_total() const { return frames_total_.load(std::memory_order_relaxed); }
    float payload_mbps() const { return payload_mbps_ema_.load(std::memory_order_relaxed); }
    uint64_t payload_bytes_total() const { return payload_bytes_total_.load(std::memory_order_relaxed); }
    float pipeline_latency_ms() const { return pipeline_latency_ms_ema_.load(std::memory_order_relaxed); }

    // Latency bookkeeping (was in the renderer loop; UI calls after present).
    void note_frame_presented(int64_t origin_ns);

    // Remaining legacy-loop accessors.
    bool     playing_state() const { return playing_.load(std::memory_order_relaxed); }
    uint32_t prog_elapsed() const { return progress_elapsed_ms_.load(std::memory_order_relaxed); }
    uint32_t prog_total() const { return progress_total_ms_.load(std::memory_order_relaxed); }
    const std::string& track_title() const { return meta_title_; }
    const std::string& track_artist() const { return meta_artist_; }
    const std::string& track_album() const { return meta_album_; }
    void set_pipeline_latency_ema(float ms) {
        pipeline_latency_ms_ema_.store(ms, std::memory_order_relaxed);
    }
    float pipeline_latency_ms_raw() const {
        return pipeline_latency_ms_ema_.load(std::memory_order_relaxed);
    }

    // ---- control callbacks (UI actions) --------------------------------

    void set_disconnect_handler(std::function<void()> h) { disconnect_handler_ = std::move(h); }
    void fire_disconnect() { if (disconnect_handler_) disconnect_handler_(); }

    std::function<void(ap::airplay::MediaCommand, uint32_t)> audio_control_handler;
    std::atomic<bool> audio_control_available_{false};
    std::atomic<bool> audio_command_pending_{false};
    std::atomic<bool> audio_command_failed_{false};

private:
    // frame slot
    std::mutex                 mtx_;
    std::condition_variable    cv_;
    AVBufferRef*               gpu_device_ = nullptr;
    bool                       gpu_ready_ = false;
    std::vector<unsigned char> y_buf_, u_buf_, v_buf_, uv_buf_;
    int                        frame_w_{0}, frame_h_{0};
    bool                       has_frame_{false};
    bool                       frame_is_nv12_{false};
    int64_t                    frame_origin_ns_{0};
    DecodedFrame               slot_avframe_;
    bool                       has_avframe_{false};

    std::mutex                 cover_mtx_;
    std::vector<unsigned char> cover_jpeg_;
    bool                       cover_dirty_{false};

    std::mutex                 meta_mtx_;
    std::string                meta_title_, meta_artist_, meta_album_;
    bool                       meta_dirty_{false};
    std::string                idle_name_, idle_ip_;
    bool                       idle_dirty_{true};

    std::atomic<uint32_t>      progress_elapsed_ms_{0};
    std::atomic<uint32_t>      progress_total_ms_{0};
    std::atomic<int64_t>       progress_pushed_at_ns_{0};
    std::atomic<bool>          playing_{true};
    std::atomic<bool>          clear_cover_requested_{false};
    std::atomic<int64_t>       flush_grace_until_ns_{0};

    mutable std::mutex         device_mtx_;
    DeviceInfo                 device_;
    bool                       has_device_{false};

    std::atomic<float>         video_fps_ema_{0.0f};
    std::atomic<int64_t>       last_push_ns_{0};
    std::atomic<int>           video_w_seen_{0};
    std::atomic<int>           video_h_seen_{0};
    std::atomic<uint64_t>      frames_total_{0};
    std::atomic<uint64_t>      payload_bytes_total_{0};
    std::atomic<float>         payload_mbps_ema_{0.0f};
    std::atomic<int64_t>       last_payload_ns_{0};
    std::atomic<float>         pipeline_latency_ms_ema_{0.0f};
    std::atomic<bool>          headless_gpu_{false};

    ap::airplay::LiveSettings* live_settings_ = nullptr;
    std::function<void()>      disconnect_handler_;
    std::unique_ptr<VideoRecorder> recorder_;
};

} // namespace ap::video
