// stream_hub.cpp — implementation. Every push/snapshot body is carried
// over verbatim from video_renderer.cpp so behaviour is bit-identical;
// only the consumer side (ui_snapshot) is new.

#include "video/stream_hub.h"
#include "airplay/live_settings.h"
#include "video/video_recorder.h"
#include "log.h"

#include <chrono>
#include <algorithm>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/buffer.h>
}

namespace ap::video {

namespace {
void copy_plane(std::vector<unsigned char>& dst,
                const unsigned char* src, int src_stride,
                int width, int height) {
    dst.resize(static_cast<size_t>(width) * height);
    const size_t row_bytes = static_cast<size_t>(width);
    if (src_stride == static_cast<int>(width)) {
        std::memcpy(dst.data(), src, row_bytes * height);
        return;
    }
    for (int r = 0; r < height; ++r)
        std::memcpy(dst.data() + static_cast<size_t>(r) * row_bytes,
                    src + static_cast<size_t>(r) * src_stride, row_bytes);
}

void update_fps_ema(std::atomic<int64_t>& last_push_ns,
                    std::atomic<float>& ema_target) {
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t prev_ns = last_push_ns.exchange(now_ns);
    if (prev_ns > 0) {
        const double dt_s = (now_ns - prev_ns) / 1e9;
        if (dt_s > 0.001 && dt_s < 1.0) {
            const float inst_fps = static_cast<float>(1.0 / dt_s);
            const float prev_ema = ema_target.load(std::memory_order_relaxed);
            const float e = (prev_ema <= 0.0f) ? inst_fps
                                               : prev_ema * 0.85f + inst_fps * 0.15f;
            ema_target.store(e, std::memory_order_relaxed);
        }
    }
}
} // namespace

StreamHub::StreamHub() : recorder_(std::make_unique<VideoRecorder>()) {}
StreamHub::~StreamHub() {
    if (recorder_) recorder_->stop();
    if (gpu_device_) av_buffer_unref(&gpu_device_);
}

// --- DecodedFrame RAII (unchanged) -----------------------------------
DecodedFrame::~DecodedFrame() {
    if (frame) av_frame_free(&frame);
}
DecodedFrame::DecodedFrame(DecodedFrame&& o) noexcept : frame(o.frame) {
    o.frame = nullptr;
}
DecodedFrame& DecodedFrame::operator=(DecodedFrame&& o) noexcept {
    if (this != &o) {
        if (frame) av_frame_free(&frame);
        frame = o.frame;
        o.frame = nullptr;
    }
    return *this;
}

AVBufferRef* StreamHub::gpu_device_ref() {
    // Legacy SDL path: the render thread publishes its D3D11 device and
    // the decoder waits briefly for it. Qt path: nobody publishes (the
    // GL canvas does not own a D3D11 device), so return null immediately
    // and let the decoder create its own device (h264_decoder fallback).
    if (headless_gpu_) return nullptr;
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait_for(lock, std::chrono::seconds(5), [this] {
        return gpu_ready_ || !live_settings_;
    });
    return gpu_device_ ? av_buffer_ref(gpu_device_) : nullptr;
}

void StreamHub::set_gpu_device(AVBufferRef* ref) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (gpu_device_) av_buffer_unref(&gpu_device_);
    gpu_device_ = ref;
    gpu_ready_  = ref != nullptr;
    cv_.notify_all();
}

void StreamHub::push_frame(const uint8_t* y, int y_stride,
                           const uint8_t* u, int u_stride,
                           const uint8_t* v, int v_stride,
                           int width, int height,
                           int64_t origin_ns) {
    if (width <= 0 || height <= 0) return;
    if (recorder_) recorder_->submit_i420(y, y_stride, u, u_stride,
                                          v, v_stride, width, height);
    const int c_w = width / 2, c_h = height / 2;

    update_fps_ema(last_push_ns_, video_fps_ema_);
    video_w_seen_.store(width,  std::memory_order_relaxed);
    video_h_seen_.store(height, std::memory_order_relaxed);
    frames_total_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mtx_);
    copy_plane(y_buf_, y, y_stride, width, height);
    copy_plane(u_buf_, u, u_stride, c_w,   c_h);
    copy_plane(v_buf_, v, v_stride, c_w,   c_h);
    frame_w_         = width;
    frame_h_         = height;
    has_frame_       = true;
    frame_is_nv12_   = false;
    has_avframe_     = false;
    frame_origin_ns_ = origin_ns;
    cv_.notify_one();
}

void StreamHub::push_frame_nv12(const uint8_t* y,  int y_stride,
                                const uint8_t* uv, int uv_stride,
                                int width, int height,
                                int64_t origin_ns) {
    if (width <= 0 || height <= 0) return;
    if (recorder_) recorder_->submit_nv12(y, y_stride, uv, uv_stride,
                                          width, height);
    const int uv_h = height / 2;

    update_fps_ema(last_push_ns_, video_fps_ema_);
    video_w_seen_.store(width,  std::memory_order_relaxed);
    video_h_seen_.store(height, std::memory_order_relaxed);
    frames_total_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mtx_);
    copy_plane(y_buf_,  y,  y_stride,  width, height);
    copy_plane(uv_buf_, uv, uv_stride, width, uv_h);
    frame_w_         = width;
    frame_h_         = height;
    has_frame_       = true;
    frame_is_nv12_   = true;
    has_avframe_     = false;
    frame_origin_ns_ = origin_ns;
    cv_.notify_one();
}

void StreamHub::push_avframe(const AVFrame* src, int64_t origin_ns) {
    if (!src) return;
    if (recorder_) recorder_->submit(src);
    AVFrame* clone = av_frame_clone(src);
    if (!clone) return;

    update_fps_ema(last_push_ns_, video_fps_ema_);
    video_w_seen_.store(clone->width,  std::memory_order_relaxed);
    video_h_seen_.store(clone->height, std::memory_order_relaxed);
    frames_total_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mtx_);
    slot_avframe_    = DecodedFrame{clone};
    frame_w_         = clone->width;
    frame_h_         = clone->height;
    has_frame_       = false;
    frame_is_nv12_   = false;
    has_avframe_     = true;
    frame_origin_ns_ = origin_ns;
    cv_.notify_one();
}

void StreamHub::push_audio_pcm(const int16_t* samples, int sample_count,
                               int sample_rate, int channels) {
    if (recorder_)
        recorder_->submit_audio(samples, sample_count, sample_rate, channels);
}

void StreamHub::record_payload_bytes(std::size_t n) {
    payload_bytes_total_.fetch_add(static_cast<uint64_t>(n),
                                   std::memory_order_relaxed);
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t prev_ns = last_payload_ns_.exchange(now_ns,
                                                      std::memory_order_relaxed);
    if (prev_ns > 0) {
        const double dt_s = (now_ns - prev_ns) / 1e9;
        if (dt_s > 0.001 && dt_s < 1.0) {
            const float inst_mbps = static_cast<float>(
                (static_cast<double>(n) * 8.0) / (dt_s * 1e6));
            const float prev_ema = payload_mbps_ema_.load(std::memory_order_relaxed);
            const float ema = (prev_ema <= 0.0f) ? inst_mbps
                              : prev_ema * 0.85f + inst_mbps * 0.15f;
            payload_mbps_ema_.store(ema, std::memory_order_relaxed);
        }
    }
}

void StreamHub::push_cover_art(const uint8_t* jpeg, std::size_t size) {
    if (!jpeg || size == 0) return;
    std::lock_guard<std::mutex> lock(cover_mtx_);
    cover_jpeg_.assign(jpeg, jpeg + size);
    cover_dirty_ = true;
    cv_.notify_one();
}

void StreamHub::push_metadata(const std::string& title,
                              const std::string& artist,
                              const std::string& album) {
    std::lock_guard<std::mutex> lock(meta_mtx_);
    if (meta_title_ == title && meta_artist_ == artist && meta_album_ == album)
        return;
    meta_title_ = title; meta_artist_ = artist; meta_album_ = album;
    meta_dirty_ = true;
    cv_.notify_one();
}

void StreamHub::push_progress(uint32_t elapsed_ms, uint32_t total_ms) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    progress_elapsed_ms_.store(elapsed_ms, std::memory_order_relaxed);
    progress_total_ms_.store(total_ms,     std::memory_order_relaxed);
    progress_pushed_at_ns_.store(ns,       std::memory_order_relaxed);
    cv_.notify_one();
}

void StreamHub::push_playback_rate(float rate) {
    const bool new_playing = rate > 0.5f;
    const bool old_playing = playing_.exchange(new_playing, std::memory_order_relaxed);
    if (old_playing != new_playing) {
        LOG_INFO << "playback state: " << (new_playing ? "PLAYING" : "PAUSED");
    }
    cv_.notify_one();
}

void StreamHub::note_flush() {
    constexpr int64_t grace_ms = 500;
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    flush_grace_until_ns_.store(now_ns + grace_ms * 1'000'000,
                                std::memory_order_relaxed);
}

bool StreamHub::in_flush_grace() const {
    const int64_t until = flush_grace_until_ns_.load(std::memory_order_relaxed);
    if (until == 0) return false;
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return now_ns < until;
}

void StreamHub::clear_session() {
    LOG_INFO << "StreamHub clear_session: reset cover / metadata / progress / video frame";
    audio_control_available_.store(false);
    audio_command_pending_.store(false);
    audio_command_failed_.store(false);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        has_frame_      = false;
        frame_is_nv12_  = false;
        frame_w_        = 0;
        frame_h_        = 0;
        slot_avframe_   = DecodedFrame();
        has_avframe_    = false;
    }
    {
        std::lock_guard<std::mutex> lock(meta_mtx_);
        meta_title_.clear();
        meta_artist_.clear();
        meta_album_.clear();
        meta_dirty_ = true;
    }
    progress_elapsed_ms_.store(0, std::memory_order_relaxed);
    progress_total_ms_.store(0,   std::memory_order_relaxed);
    progress_pushed_at_ns_.store(0, std::memory_order_relaxed);
    playing_.store(true, std::memory_order_relaxed);
    clear_cover_requested_.store(true, std::memory_order_relaxed);
    frames_total_.store(0, std::memory_order_relaxed);
    video_fps_ema_.store(0.0f, std::memory_order_relaxed);
    last_push_ns_.store(0, std::memory_order_relaxed);
    payload_bytes_total_.store(0, std::memory_order_relaxed);
    payload_mbps_ema_.store(0.0f, std::memory_order_relaxed);
    last_payload_ns_.store(0, std::memory_order_relaxed);
    pipeline_latency_ms_ema_.store(0.0f, std::memory_order_relaxed);
    cv_.notify_one();
}

void StreamHub::set_idle_info(const std::string& name, const std::string& ip) {
    std::lock_guard<std::mutex> lock(meta_mtx_);
    idle_name_ = name; idle_ip_ = ip; idle_dirty_ = true;
    cv_.notify_one();
}

void StreamHub::set_active_device(DeviceInfo info) {
    std::lock_guard<std::mutex> lock(device_mtx_);
    device_     = std::move(info);
    has_device_ = true;
}

void StreamHub::clear_active_device() {
    std::lock_guard<std::mutex> lock(device_mtx_);
    has_device_ = false;
    device_     = {};
}

bool StreamHub::clear_session_if_active(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(device_mtx_);
    if (!has_device_ || device_.session_id != session_id) return false;
    clear_session();
    has_device_ = false;
    device_ = {};
    LOG_INFO << "Connection ended: cleared active session=" << session_id;
    return true;
}

bool StreamHub::active_device_snapshot(DeviceInfo& out) const {
    std::lock_guard<std::mutex> lock(device_mtx_);
    if (!has_device_) return false;
    out = device_;
    return true;
}

StreamHub::RenderConsume StreamHub::render_consume() {
    RenderConsume rc;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (has_avframe_) {
            rc.avf      = std::move(slot_avframe_);
            slot_avframe_ = DecodedFrame();
            rc.has_avf  = true;
            rc.w = frame_w_; rc.h = frame_h_;
            rc.origin_ns = frame_origin_ns_;
            has_avframe_ = false;
        } else if (has_frame_) {
            rc.y = std::move(y_buf_);  y_buf_.clear();
            if (frame_is_nv12_) { rc.uv = std::move(uv_buf_); uv_buf_.clear(); }
            else {
                rc.u = std::move(u_buf_); u_buf_.clear();
                rc.v = std::move(v_buf_); v_buf_.clear();
            }
            rc.has_planes  = true;
            rc.nv12        = frame_is_nv12_;
            rc.w           = frame_w_;
            rc.h           = frame_h_;
            rc.origin_ns   = frame_origin_ns_;
            has_frame_     = false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(cover_mtx_);
        rc.clear_cover = clear_cover_requested_.exchange(false,
                        std::memory_order_relaxed);
        if (cover_dirty_) {
            rc.cover = std::move(cover_jpeg_);
            cover_jpeg_.clear();
            cover_dirty_ = false;
            rc.cover_dirty = true;
        }
    }
    {
        std::lock_guard<std::mutex> lock(meta_mtx_);
        if (meta_dirty_) {
            rc.title = std::move(meta_title_);
            rc.artist = std::move(meta_artist_);
            rc.album = std::move(meta_album_);
            meta_title_.clear(); meta_artist_.clear(); meta_album_.clear();
            meta_dirty_ = false;
            rc.meta_dirty = true;
        }
        if (idle_dirty_) {
            rc.idle_name = std::move(idle_name_);
            rc.idle_ip   = std::move(idle_ip_);
            idle_name_.clear(); idle_ip_.clear();
            idle_dirty_ = false;
            rc.idle_dirty = true;
        }
    }
    return rc;
}

StreamHub::UiSnapshot StreamHub::ui_snapshot() {
    UiSnapshot snap;
    auto copy_rows = [](std::vector<uint8_t>& dst, const uint8_t* src,
                        int stride, int width, int height) {
        dst.resize(static_cast<size_t>(width) * height);
        if (stride == width)
            std::memcpy(dst.data(), src, static_cast<size_t>(width) * height);
        else
            for (int r = 0; r < height; ++r)
                std::memcpy(dst.data() + static_cast<size_t>(r) * width,
                            src + static_cast<size_t>(r) * stride, width);
    };
    {
        std::lock_guard<std::mutex> lock(mtx_);
        snap.has_frame = has_frame_ || has_avframe_;
        if (has_avframe_ && slot_avframe_.frame) {
            const AVFrame* f = slot_avframe_.frame;
            const int cw = f->width / 2, ch = f->height / 2;
            if (f->data[0]) copy_rows(snap.frame.y, f->data[0], f->linesize[0], f->width, f->height);
            if (f->data[1]) copy_rows(snap.frame.u, f->data[1], f->linesize[1], cw, ch);
            if (f->data[2]) copy_rows(snap.frame.v, f->data[2], f->linesize[2], cw, ch);
            snap.frame.y_stride = f->width; snap.frame.u_stride = cw; snap.frame.v_stride = cw;
            snap.frame.uv_stride = 0;
            snap.frame.nv12 = false;
            snap.frame.width  = f->width;
            snap.frame.height = f->height;
        } else if (has_frame_) {
            const int cw = frame_w_ / 2, ch = frame_h_ / 2;
            copy_rows(snap.frame.y, y_buf_.data(), frame_w_, frame_w_, frame_h_);
            if (frame_is_nv12_) {
                copy_rows(snap.frame.uv, uv_buf_.data(), frame_w_, frame_w_, ch);
                snap.frame.uv_stride = frame_w_;
            } else {
                copy_rows(snap.frame.u, u_buf_.data(), cw, cw, ch);
                copy_rows(snap.frame.v, v_buf_.data(), cw, cw, ch);
                snap.frame.u_stride = cw; snap.frame.v_stride = cw;
            }
            snap.frame.y_stride = frame_w_;
            snap.frame.nv12 = frame_is_nv12_;
            snap.frame.width  = frame_w_;
            snap.frame.height = frame_h_;
        }
        snap.frame.origin_ns = frame_origin_ns_;
    }
    {
        std::lock_guard<std::mutex> lock(cover_mtx_);
        snap.has_cover = !cover_jpeg_.empty();
        if (snap.has_cover) snap.cover_jpeg = cover_jpeg_;   // copy; small
    }
    {
        std::lock_guard<std::mutex> lock(meta_mtx_);
        snap.track_title  = meta_title_;
        snap.track_artist = meta_artist_;
        snap.track_album  = meta_album_;
        snap.idle_name    = idle_name_;
        snap.idle_ip      = idle_ip_;
    }
    snap.prog_elapsed = progress_elapsed_ms_.load(std::memory_order_relaxed);
    snap.prog_total   = progress_total_ms_.load(std::memory_order_relaxed);
    snap.playing      = playing_.load(std::memory_order_relaxed);
    snap.progress_pushed_at_ns = progress_pushed_at_ns_.load(std::memory_order_relaxed);
    return snap;
}

void StreamHub::note_frame_presented(int64_t origin_ns) {
    if (origin_ns <= 0) return;
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const float ms = static_cast<float>((now_ns - origin_ns) / 1e6);
    if (ms <= 0.0f || ms > 10'000.0f) return;
    const float prev = pipeline_latency_ms_ema_.load(std::memory_order_relaxed);
    pipeline_latency_ms_ema_.store(prev <= 0.0f ? ms : prev * 0.85f + ms * 0.15f,
                                   std::memory_order_relaxed);
}

} // namespace ap::video
