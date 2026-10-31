// video_renderer_bridge.cpp — QT_UI build only.
//
// The AirPlay stack (routes.cpp, audio_receiver.cpp, hls_player.cpp)
// addresses the renderer through VideoRenderer*. In the Qt build the SDL
// window thread is never compiled (video_renderer.cpp is excluded), so
// this thin bridge provides the VideoRenderer surface the stack calls,
// all forwarding to the StreamHub data plane.

#include "video/video_renderer.h"
#include "video/stream_hub.h"
#include "video/video_recorder.h"
#include "log.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

namespace ap::video {

namespace {

// The Qt canvas consumes CPU-side YUV planes only. When the decoder runs
// with D3D11VA the AVFrame carries GPU texture pointers (data[0] is an
// ID3D11Texture2D wrapper) — download to system memory NV12 first, then
// hand the hub plain planes. Cost: one GPU→CPU copy per frame, which the
// legacy SDL renderer avoided by presenting the GPU texture directly.
void push_avframe_cpu(StreamHub& hub, const AVFrame* src, int64_t origin_ns) {
    if (src->format != AV_PIX_FMT_D3D11) {
        hub.push_avframe(src, origin_ns);
        return;
    }
    // Download to NV12 system memory.
    AVFrame* sw = av_frame_alloc();
    if (!sw) return;
    sw->format = AV_PIX_FMT_NV12;
    sw->width  = src->width;
    sw->height = src->height;
    if (av_frame_get_buffer(sw, 32) < 0) {
        av_frame_free(&sw);
        LOG_WARN << "Qt bridge: hw frame buffer alloc failed";
        return;
    }
    if (av_hwframe_transfer_data(sw, src, 0) < 0) {
        av_frame_free(&sw);
        LOG_WARN << "Qt bridge: hw->sw transfer failed";
        return;
    }
    // Hand the planes over (push_frame_nv12 copies them into the slot and
    // feeds the recorder).
    hub.push_frame_nv12(sw->data[0], sw->linesize[0],
                        sw->data[1], sw->linesize[1],
                        sw->width, sw->height, origin_ns);
    av_frame_free(&sw);
}

} // namespace

VideoRenderer::VideoRenderer() : hub_(std::make_unique<StreamHub>()) {}
VideoRenderer::~VideoRenderer() {
    stop();
    if (hub_) hub_->recorder()->stop();
}

StreamHub& VideoRenderer::hub() { return *hub_; }

AVBufferRef* VideoRenderer::gpu_device_ref() {
    return hub_->gpu_device_ref();
}

bool VideoRenderer::start(const std::string&) {
    LOG_INFO << "Qt UI: SDL window thread not started (data-plane only)";
    hub_->set_headless_gpu(true);
    return true;    // nothing to start; the Qt window is the front end
}

void VideoRenderer::stop() {
    if (hub_) hub_->recorder()->stop();
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
    { push_avframe_cpu(*hub_, src, origin_ns); }
void VideoRenderer::push_audio_pcm(const int16_t* samples, int sample_count,
                                   int sample_rate, int channels)
    { hub_->push_audio_pcm(samples, sample_count, sample_rate, channels); }
void VideoRenderer::record_payload_bytes(std::size_t n)
    { hub_->record_payload_bytes(n); }
void VideoRenderer::set_active_device(DeviceInfo info)
    { hub_->set_active_device(StreamHub::DeviceInfo{
          std::move(info.peer_ip), std::move(info.session_id),
          std::move(info.kind), std::move(info.name), std::move(info.model)}); }
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
    out = VideoRenderer::DeviceInfo{std::move(d.peer_ip), std::move(d.session_id),
                                    std::move(d.kind), std::move(d.name),
                                    std::move(d.model)};
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
    { hub_->push_frame(y, y_stride, u, u_stride, v, v_stride, width, height,
                       origin_ns); }
void VideoRenderer::push_frame_nv12(const uint8_t* y,  int y_stride,
                                    const uint8_t* uv, int uv_stride,
                                    int width, int height,
                                    int64_t origin_ns)
    { hub_->push_frame_nv12(y, y_stride, uv, uv_stride, width, height,
                            origin_ns); }

} // namespace ap::video
