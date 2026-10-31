#pragma once

// app_ui — AirMirror application UI, faithful to the airmirror prototype:
//   top bar (brand / status / service + window controls)
//   three columns: nav sidebar 190px | main panel | right rail 292px
//   six pages: 总览 / 画面 / 音乐 / 录制 / 设置 / 系统与网络
//   footer strip (doubles as the diagnostics HUD)
// The renderer fills Context + wires actions each frame, then calls Frame().
// StageArea() is pure math so the SDL video pass can size the stage before
// the ImGui pass runs (video is drawn underneath; overview chrome overlaps).

#include "theme.h"
#include "icons.h"
#include "widgets.h"

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <cstdio>

namespace ap::video::app_ui {

// ---------------------------------------------------------------------------
// Context: per-frame snapshot + action callbacks (owned by the renderer)
// ---------------------------------------------------------------------------
struct Context {
    ImFont* body = nullptr;     // default 16
    ImFont* caption = nullptr;  // small 12-13
    ImFont* title = nullptr;    // page h1 22-24
    ImFont* display = nullptr;  // 30+ (now-playing title)
    ImFont* heading = nullptr;  // 17-18 card titles
    float scale = 1.0f;
    float S(float v) const { return v * scale; }

    // state
    std::string receiver_name = "Windows 投屏";
    std::string device_name;             // empty = none connected
    std::string device_kind;             // "Mirror" / "Audio"
    bool service_on = true;
    bool playing = false;
    bool video_live = false;
    int  video_w = 0, video_h = 0;
    float video_fps = 0;
    bool gpu_decode = true;
    bool vsync = true;
    bool diagnostics = false;

    int  mirror_w = 2560, mirror_h = 1440;
    bool hevc = true;
    int  fps_cap = 60;
    int  rotation = -1;                  // -1 auto, 0..3
    int  frame_style = 0;                // 0 auto, -1 off, >=1 manual
    std::vector<std::string> frame_labels;
    std::vector<std::string> res_labels;

    bool recording = false;
    int64_t rec_elapsed_ms = 0;
    uint64_t rec_frames = 0, rec_dropped = 0, rec_audio = 0;
    std::string rec_path, rec_error, rec_codec;
    int  rec_codec_sel = 0, rec_encoder_sel = 0, rec_bitrate = 8;
    char rec_dir_buf[1024] = {};

    int  firewall_state = 0;             // 0 Checking 1 Allowed 2 NeedsRule 3 Managed 4 Unknown
    bool firewall_busy = false;
    std::string firewall_message;

    bool audio_ready = false, audio_pending = false, audio_failed = false;
    uint32_t prog_elapsed = 0, prog_total = 0;
    std::string track_title, track_artist, track_album;
    ImTextureID cover = (ImTextureID)nullptr;
    bool has_cover = false;
    float cover_w = 0, cover_h = 0;

    uint64_t frames_total = 0;
    float payload_mbps = 0;
    std::string payload_total;
    int win_w = 0, win_h = 0;
    float stage_zoom = 1.0f;             // 0.3..3.0
    bool stage_video = false;            // SDL drew live video into the stage

    // actions
    std::function<void()> act_open_settings, act_disconnect, act_fullscreen,
        act_hide_chrome, act_toggle_record, act_browse_dir, act_play_pause,
        act_allow_firewall, act_reset_settings, act_prev, act_next,
        act_zoom_in, act_zoom_out;
    std::function<void(bool)> act_service;
    std::function<void(int, int)> act_resolution;   // (preset index, is_preset)
    std::function<void(int, int)> on_custom_res;    // commit WxH
    std::function<void(int)> act_rotation, act_frame_style;
    std::function<void(bool)> write_gpu, write_vsync, write_hevc, write_diag;
    std::function<void(float)> write_zoom;
    std::function<void(const std::string&)> write_name, write_rec_dir;
    std::function<void(int)> write_fps, write_rec_codec, write_rec_encoder,
                             write_rec_bitrate;
};

// ---------------------------------------------------------------------------
// ViewState: page routing, animations, transient inputs
// ---------------------------------------------------------------------------
struct ViewState {
    enum Page { Overview = 0, Screen, Music, Recording, SettingsPage, System };
    int page = Overview;
    int last_page = -1;
    float page_fade = 1.0f;
    bool settings_open = false;          // legacy flag: opens the settings page
    bool firewall_dismissed = false;
    char name_buf[256] = {};
    bool name_dirty = false;
    bool custom_res = false;
    int  custom_w = 0, custom_h = 0;
    float reset_notice_until = 0;
    int title_min = 0, title_max = 0, title_close = 0;
    int pending_page = -1;               // applied after the frame ends, so a
                                         // click that deactivates an input
                                         // still sees its page submitted
};

class AppUI {
public:
    Context ctx;
    ViewState view;

    void init(ImFont* body, ImFont* caption, ImFont* title, ImFont* display,
              ImFont* heading, float scale) {
        ctx.body = body; ctx.caption = caption; ctx.title = title;
        ctx.display = display; ctx.heading = heading; ctx.scale = scale;
    }

    struct Layout {
        float top_h, side_w, rail_w, footer_h;
        float main_x, main_y, main_w, main_h;
    };

    struct Rect { float x = 0, y = 0, w = 0, h = 0; };

    // Pure layout math, no ImGui calls. The SDL video pass calls this before
    // NewFrame to size the stage; it returns the overview stage-card interior,
    // or zero rect when the current page has no stage.
    Rect StageArea(float fw, float fh) const {
        if (view.page != ViewState::Overview) return {};
        const float s = ctx.scale;
        const float banner = BannerHeight();
        const float main_x = s * 16 + s * SidebarW + s * 13;
        const float main_y = s * 14 + s * TopbarH + s * 14 + (banner > 0 ? banner + s * 12 : 0);
        const float main_w = fw - main_x - (s * RightRailW + s * 13);
        const float main_h = fh - main_y - s * FooterH - s * 14;
        // PageOverview draws the stage card at main_x + S(4) with width
        // main_w - S(8); head row is S(64) tall, bottom strip S(30).
        return { main_x + s * 4, main_y + s * 64,
                 main_w - s * 8, main_h - s * 64 - s * 30 };
    }

    // Firewall banner height (0 = hidden). Pure math.
    float BannerHeight() const {
        const bool need = ctx.firewall_state == 2 || ctx.firewall_state == 4;
        return (need && !view.firewall_dismissed) ? ctx.scale * 46.0f : 0.0f;
    }

    Layout Frame(float fw, float fh) {
        if (view.settings_open) { view.page = ViewState::SettingsPage; view.settings_open = false; }
        if (view.last_page != view.page) { view.last_page = view.page; view.page_fade = 0; }
        if (ctx.firewall_state == 1) view.firewall_dismissed = false;

        Layout lay{};
        lay.top_h = ctx.S(TopbarH);
        lay.side_w = ctx.S(SidebarW);
        lay.rail_w = view.page == ViewState::Overview ? ctx.S(RightRailW) : 0.0f;
        lay.footer_h = ctx.S(FooterH);
        const float banner = BannerHeight();
        lay.main_x = ctx.S(16) + lay.side_w + ctx.S(13);
        lay.main_y = ctx.S(14) + lay.top_h + ctx.S(14) + (banner > 0 ? banner + ctx.S(12) : 0);
        lay.main_w = fw - lay.main_x - (lay.rail_w > 0 ? lay.rail_w + ctx.S(13) : 0);
        lay.main_h = fh - lay.main_y - lay.footer_h - ctx.S(14);

        view.page_fade = Ease(view.page_fade, 1.0f, 10);

        DrawTopBar(fw);
        if (banner > 0) DrawBanner(fw, banner);
        DrawSidebar(lay, fh);
        DrawMain(lay);
        if (view.page == ViewState::Overview) DrawRightRail(lay);
        DrawFooter(fw, fh);

        // Nav clicks land in the sidebar (drawn first); deferring the page
        // switch to the end of the frame keeps the old page submitted on
        // the click frame, so IsItemDeactivatedAfterEdit still fires and
        // in-progress edits (receiver name) autosave before leaving.
        if (view.pending_page >= 0) {
            view.page = view.pending_page;
            view.pending_page = -1;
        }

        // page transition veil over the main panel
        if (view.page_fade < 0.995f) {
            ImGui::SetNextWindowPos(ImVec2(lay.main_x, lay.main_y));
            ImGui::SetNextWindowSize(ImVec2(lay.main_w, lay.main_h));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("##au_fade", nullptr, kPanel | ImGuiWindowFlags_NoBackground);
            auto* d = ImGui::GetWindowDrawList();
            const int a = static_cast<int>((1 - view.page_fade) * 240);
            const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
            d->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), IM_COL32(245, 245, 243, a));
            ImGui::End(); ImGui::PopStyleVar(); ImGui::PopStyleColor();
        }
        return lay;
    }

private:
    static constexpr ImGuiWindowFlags kPanel =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    static constexpr ImGuiWindowFlags kScroll =
        kPanel & ~(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // ---- top bar --------------------------------------------------------------
    void DrawTopBar(float fw) {
        ImGui::SetNextWindowPos(ImVec2(ctx.S(16), ctx.S(14)));
        ImGui::SetNextWindowSize(ImVec2(fw - ctx.S(32), ctx.S(TopbarH)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctx.S(16), 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(255, 255, 255, 224));
        ImGui::PushStyleColor(ImGuiCol_Border, Border);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::Begin("##au_top", nullptr, kPanel);
        auto* d = ImGui::GetWindowDrawList();
        const float th = ctx.S(TopbarH);

        // brand
        const float mark = ctx.S(30);
        const ImVec2 mp = ImGui::GetCursorScreenPos();
        d->AddRectFilled(mp, ImVec2(mp.x + mark, mp.y + mark), Ink, ctx.S(10));
        DrawIcon(d, Icon::Phone, ImVec2(mp.x + (mark - ctx.S(15)) / 2, mp.y + (mark - ctx.S(15)) / 2),
                 ctx.S(15), White);
        ImGui::SetCursorScreenPos(ImVec2(mp.x + mark + ctx.S(10), mp.y + ctx.S(2)));
        ImGui::PushFont(ctx.heading);
        ImGui::TextUnformatted("AirMirror");
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(mp.x + mark + ctx.S(10), mp.y + ctx.S(19)));
        ImGui::PushFont(ctx.caption);
        ImGui::TextColored(Rgba(Subtle), "iPhone Receiver");
        ImGui::PopFont();

        // center status
        const float cx0 = fw * 0.42f;
        const bool ready = ctx.service_on;
        const bool connected = !ctx.device_name.empty();
        const char* status = !ready ? "接收服务已停止" :
                             connected ? "已连接并正在接收" : "等待设备连接";
        const float stw = ImGui::CalcTextSize(status).x;
        const float dot_x = cx0, dot_y = th / 2;
        StatusDot(d, ImVec2(dot_x, dot_y), ctx.S(3.5f), ready ? Green : Subtle, ready);
        d->AddText(ImVec2(dot_x + ctx.S(12), dot_y - ctx.S(8)), Muted, status);
        if (connected) {
            d->AddText(ImVec2(dot_x + ctx.S(12) + stw + ctx.S(10), dot_y - ctx.S(8)),
                       Subtle, ctx.device_name.c_str());
        }

        // service toggle + window controls (right)
        const float ctl = ctx.S(46);
        const float base = fw - ctx.S(32) - ctx.S(16);
        const float x3 = base - ctl * 4;
        ImGui::SetCursorScreenPos(ImVec2(x3 - ctx.S(4), (th - ctx.S(31)) / 2));
        if (IconButton("##au_top_service", Icon::Power, ready ? "停止接收服务" : "启动接收服务",
                       ctx.S(31), ctx.S(31), ready ? Green : Muted) && ctx.act_service)
            ctx.act_service(!ctx.service_on);
        auto control = [&](const char* id, Icon ic, float x, bool danger) -> int {
            ImGui::SetCursorScreenPos(ImVec2(x, 0));
            const bool hit = Hit(id, ImVec2(ctl, th));
            if (ItemHovered())
                d->AddRectFilled(ImVec2(x, 0), ImVec2(x + ctl, th),
                                 danger ? IM_COL32(248, 239, 235, 255) : Panel);
            const float isz = ctx.S(15);
            DrawIcon(d, ic, ImVec2(x + (ctl - isz) / 2, (th - isz) / 2), isz,
                     danger ? Red : Muted);
            return hit ? 1 : 0;
        };
        view.title_min   = control("##au_min", Icon::Minus, base - ctl * 3, false);
        view.title_max   = control("##au_max", Icon::Expand, base - ctl * 2, false);
        view.title_close = control("##au_close", Icon::X, base - ctl, true);
        ImGui::End();
        ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
    }

    // ---- firewall banner ---------------------------------------------------------
    void DrawBanner(float fw, float banner_h) {
        ImGui::SetNextWindowPos(ImVec2(ctx.S(16), ctx.S(14) + ctx.S(TopbarH) + ctx.S(14)));
        ImGui::SetNextWindowSize(ImVec2(fw - ctx.S(32), banner_h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctx.S(16), 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(253, 247, 240, 255));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(240, 221, 204, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::Begin("##au_banner", nullptr, kPanel);
        auto* d = ImGui::GetWindowDrawList();
        const float bh = ImGui::GetWindowHeight();
        DrawIcon(d, Icon::Shield, ImVec2(ImGui::GetCursorScreenPos().x, bh / 2 - ctx.S(8)),
                 ctx.S(16), IM_COL32(191, 128, 66, 255));
        ImGui::PushFont(ctx.caption);
        const std::string msg = ctx.firewall_message.empty()
            ? std::string("Windows 防火墙可能阻止局域网连接，设备将无法搜索到本电脑。")
            : ctx.firewall_message;
        ImGui::SetCursorScreenPos(ImVec2(ctx.S(16) + ctx.S(24), bh / 2 - ctx.S(8)));
        ImGui::TextColored(Rgba(IM_COL32(140, 96, 48, 255)), "%s", msg.c_str());
        // actions on the right
        const float bx = ImGui::GetWindowWidth() - ctx.S(16);
        ImGui::SetCursorScreenPos(ImVec2(bx - ctx.S(26), bh / 2 - ctx.S(8)));
        if (IconButton("##au_banner_x", Icon::X, "忽略", ctx.S(17), ctx.S(17),
                       IM_COL32(191, 128, 66, 255)))
            view.firewall_dismissed = true;
        ImGui::SetCursorScreenPos(ImVec2(bx - ctx.S(26) - ctx.S(96) - ctx.S(8), bh / 2 - ctx.S(15)));
        ImGui::BeginDisabled(ctx.firewall_busy);
        if (PrimaryButton("一键放行", ImVec2(ctx.S(96), ctx.S(30))) && ctx.act_allow_firewall)
            ctx.act_allow_firewall();
        ImGui::EndDisabled();
        ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
    }

    // ---- sidebar ---------------------------------------------------------------
    void DrawSidebar(const Layout& lay, float fh) {
        const float top = ctx.S(14) + ctx.S(TopbarH) + ctx.S(14);  // below top bar
        const float sh = fh - top - ctx.S(15);
        ImGui::SetNextWindowPos(ImVec2(ctx.S(16), top));
        ImGui::SetNextWindowSize(ImVec2(lay.side_w, sh));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctx.S(10), ctx.S(15)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(255, 255, 255, 194));
        ImGui::PushStyleColor(ImGuiCol_Border, Border);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::Begin("##au_side", nullptr, kPanel);
        auto* d = ImGui::GetWindowDrawList();
        const float nav_w = lay.side_w - ctx.S(20);

        ImGui::PushFont(ctx.caption);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ctx.S(11));
        ImGui::TextColored(Rgba(Subtle), "WORKSPACE");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, ctx.S(7)));

        const float nav_h = ctx.S(37);
        auto go = [&](int p) { view.pending_page = p; };
        if (NavItem("##au_nav_overview", "总览", Icon::Grid, view.page == ViewState::Overview, nav_w, ctx.body, nav_h))
            go(ViewState::Overview);
        if (NavItem("##au_nav_screen", "画面", Icon::Screen, view.page == ViewState::Screen, nav_w, ctx.body, nav_h))
            go(ViewState::Screen);
        if (NavItem("##au_nav_music", "音乐", Icon::Music, view.page == ViewState::Music, nav_w, ctx.body, nav_h))
            go(ViewState::Music);
        if (NavItem("##au_nav_recording", "录制", Icon::Record, view.page == ViewState::Recording, nav_w, ctx.body, nav_h))
            go(ViewState::Recording);

        ImGui::Dummy(ImVec2(0, ctx.S(16)));
        ImGui::PushFont(ctx.caption);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ctx.S(11));
        ImGui::TextColored(Rgba(Subtle), "PREFERENCES");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, ctx.S(7)));

        if (NavItem("##au_nav_settings", "设置", Icon::Settings, view.page == ViewState::SettingsPage, nav_w, ctx.body, nav_h))
            go(ViewState::SettingsPage);
        if (NavItem("##au_nav_system", "系统与网络", Icon::Shield, view.page == ViewState::System, nav_w, ctx.body, nav_h))
            go(ViewState::System);

        // bottom service mini-card
        const float panel_h = ImGui::GetWindowHeight();
        const float mini_h = ctx.S(62);
        ImGui::SetCursorPosY(panel_h - ctx.S(15) - mini_h);
        const ImVec2 mp = ImGui::GetCursorScreenPos();
        const ImVec2 m2(mp.x + nav_w, mp.y + mini_h);
        d->AddRectFilled(mp, m2, IM_COL32(246, 246, 244, 255), ctx.S(13));
        d->AddRect(mp, m2, IM_COL32(238, 238, 235, 255), ctx.S(13), 0, 1.0f);
        const float ph = ctx.S(36);
        const ImVec2 dp(mp.x + ctx.S(11), mp.y + (mini_h - ph) / 2);
        d->AddRectFilled(dp, ImVec2(dp.x + ctx.S(30), dp.y + ph), IM_COL32(41, 42, 40, 255), ctx.S(8));
        DrawIcon(d, Icon::Phone, ImVec2(dp.x + 3.0f * ctx.scale, dp.y + 3.0f * ctx.scale),
                 ctx.S(17), White);
        ImGui::SetCursorScreenPos(ImVec2(dp.x + ctx.S(38), dp.y + ctx.S(9)));
        ImGui::PushFont(ctx.caption);
        ImGui::TextUnformatted("接收服务");
        ImGui::SetCursorScreenPos(ImVec2(dp.x + ctx.S(38), dp.y + ctx.S(24)));
        ImGui::TextColored(Rgba(Muted), ctx.service_on ? "正在运行" : "已停止");
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(m2.x - ctx.S(40), mp.y + (mini_h - ctx.S(17)) / 2));
        bool svc = ctx.service_on;
        if (Switch("##au_svc_switch", &svc) && ctx.act_service) ctx.act_service(svc);
        ImGui::End();
        ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
    }

    // ---- main panel --------------------------------------------------------------
    void DrawMain(const Layout& lay) {
        ImGui::SetNextWindowPos(ImVec2(lay.main_x, lay.main_y));
        ImGui::SetNextWindowSize(ImVec2(lay.main_w, lay.main_h));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##au_main", nullptr, kPanel);

        const float w = ImGui::GetContentRegionAvail().x;
        switch (view.page) {
        case ViewState::Overview:     PageOverview(lay, w); break;
        case ViewState::Screen:       PageScreen(w); break;
        case ViewState::Music:        PageMusic(w); break;
        case ViewState::Recording:    PageRecording(w); break;
        case ViewState::SettingsPage: PageSettings(w); break;
        case ViewState::System:       PageSystem(w); break;
        }

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void PageHead(const char* eyebrow, const char* title, const char* sub) {
        ImGui::PushFont(ctx.caption);
        ImGui::TextColored(Rgba(Subtle), "%s", eyebrow);
        ImGui::PopFont();
        ImGui::PushFont(ctx.title);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::PushFont(ctx.caption);
        ImGui::TextColored(Rgba(Muted), "%s", sub);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, ctx.S(10)));
    }

    // ---- card scaffolding (draw-list channels: bg under content) ---------------
    struct Card { ImVec2 p; float w; };

    Card CardBegin(const char* title, const char* desc, float w, Icon icon = Icon::Info) {
        auto* d = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        d->ChannelsSplit(2);
        d->ChannelsSetCurrent(1);
        const float pad = ctx.S(16);
        ImGui::PushFont(ctx.heading);
        const ImVec2 ts = ImGui::CalcTextSize(title);
        const float isz = ctx.S(15);
        DrawIcon(d, icon, ImVec2(p.x + pad, p.y + pad + 1), isz, IM_COL32(133, 134, 127, 255));
        ImGui::SetCursorScreenPos(ImVec2(p.x + pad + isz + ctx.S(8), p.y + pad));
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        float y = p.y + pad + ts.y;
        if (desc && *desc) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + pad, y + ctx.S(5)));
            ImGui::PushFont(ctx.caption);
            ImGui::PushTextWrapPos(p.x + w - pad);
            ImGui::TextColored(Rgba(Muted), "%s", desc);
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            y += ctx.S(5) + ImGui::GetItemRectSize().y;
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + pad, y + ctx.S(12)));
        ImGui::PushItemWidth(w - pad * 2);
        ImGui::BeginGroup();
        return { p, w };
    }

    float CardEnd(const Card& c) {
        ImGui::EndGroup();
        ImGui::PopItemWidth();
        auto* d = ImGui::GetWindowDrawList();
        const ImVec2 hi = ImGui::GetItemRectMax();
        const float pad = ctx.S(16);
        const ImVec2 b(c.p.x + c.w, hi.y + pad);
        d->ChannelsSetCurrent(0);
        CardShadow(d, c.p, b, RCard);
        d->AddRectFilled(c.p, b, Canvas, RCard);
        d->AddRect(c.p, b, Border, RCard, 0, 1.0f);
        d->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(c.p.x, b.y + ctx.S(12)));
        return b.y;
    }

    // toggle row inside a card
    bool ToggleRow(const char* label, const char* help, bool* value) {
        const float row_h = ctx.S(46);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + row_h / 2 - ImGui::GetTextLineHeight() - 2));
        ImGui::BeginGroup();
        ImGui::TextUnformatted(label);
        if (help && *help) {
            ImGui::PushFont(caption_font());
            ImGui::TextColored(Rgba(Subtle), "%s", help);
            ImGui::PopFont();
        }
        ImGui::EndGroup();
        const float switch_w = ctx.S(31);
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(p.x + ImGui::GetContentRegionAvail().x - switch_w,
                                         p.y + row_h / 2 - 9));
        const bool clicked = Switch(label, value);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + row_h));
        ImGui::Separator();
        return clicked;
    }

    ImFont* caption_font() { return ctx.caption ? ctx.caption : ctx.body; }

    void Caption(const char* fmt, ...) {
        char buf[512];
        va_list ap; va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        ImGui::PushFont(caption_font());
        ImGui::TextColored(Rgba(Muted), "%s", buf);
        ImGui::PopFont();
    }

    // ---- overview ---------------------------------------------------------------
    void PageOverview(const Layout& lay, float w) {
        const float head_h = ctx.S(64);
        ImGui::SetCursorScreenPos(ImVec2(lay.main_x + ctx.S(4), lay.main_y + ctx.S(2)));
        PageHead("WORKSPACE / OVERVIEW", "屏幕镜像", "把 iPhone 画面自然地延伸到桌面。");
        const float btn_y = lay.main_y + ctx.S(10);
        ImGui::SetCursorScreenPos(ImVec2(lay.main_x + w - ctx.S(210), btn_y));
        if (Button("全屏##au_f11", ImVec2(ctx.S(64), 0)) && ctx.act_fullscreen) ctx.act_fullscreen();
        ImGui::SameLine(0, ctx.S(6));
        if (Button("纯画面##au_pure", ImVec2(ctx.S(72), 0)) && ctx.act_hide_chrome) ctx.act_hide_chrome();

        // stage card fills the rest; the SDL video renders beneath this window
        const float stage_y = lay.main_y + head_h;
        const float stage_h = lay.main_h - head_h - ctx.S(30);
        auto* d = ImGui::GetWindowDrawList();
        const ImVec2 a(lay.main_x + ctx.S(4), stage_y);
        const ImVec2 b(a.x + w - ctx.S(8), a.y + stage_h);
        // fill only when no live video beneath (SDL paints video under us)
        StageCard(d, a, b, !ctx.stage_video);

        // top-left live pill
        const bool live = !ctx.device_name.empty() && ctx.service_on;
        const char* pill = live ? "LIVE PREVIEW" : "PREVIEW PAUSED";
        ImGui::PushFont(ctx.caption);
        const ImVec2 ts = ImGui::CalcTextSize(pill);
        const ImVec2 pp(a.x + ctx.S(17), a.y + ctx.S(12));
        d->AddRectFilled(pp, ImVec2(pp.x + ts.x + ctx.S(26), pp.y + ts.y + ctx.S(8)),
                         IM_COL32(255, 255, 255, 170), 99);
        StatusDot(d, ImVec2(pp.x + ctx.S(12), pp.y + ts.y / 2 + ctx.S(4)), ctx.S(2.5f),
                  live ? Green : Subtle, false);
        d->AddText(ImVec2(pp.x + ctx.S(22), pp.y + ctx.S(4)), IM_COL32(78, 126, 104, 255), pill);

        // top-right tools
        const float tool_h = ctx.S(28);
        float tx = b.x - ctx.S(17) - tool_h * 4 - ctx.S(9);
        const float ty = a.y + ctx.S(8);
        auto tool = [&](const char* id, Icon ic, const char* tip, float x) -> bool {
            ImGui::SetCursorScreenPos(ImVec2(x, ty));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            const bool r = IconButton(id, ic, tip, tool_h, tool_h, IM_COL32(92, 106, 95, 255));
            ImGui::PopStyleColor();
            return r;
        };
        if (tool("##au_zoom_out", Icon::Minus, "缩小画面", tx) && ctx.act_zoom_out) ctx.act_zoom_out();
        tx += tool_h + 3;
        if (tool("##au_zoom_in", Icon::Plus, "放大画面", tx) && ctx.act_zoom_in) ctx.act_zoom_in();
        tx += tool_h + 3;
        if (tool("##au_rot_btn", Icon::Rotate, "旋转画面", tx)) ImGui::OpenPopup("##au_rot");
        tx += tool_h + 3;
        if (tool("##au_more", Icon::More, "画面设置", tx)) view.page = ViewState::Screen;
        ImGui::SetNextWindowPos(ImVec2(tx, ty + tool_h + ctx.S(4)), ImGuiCond_Appearing, ImVec2(1, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Sp2, Sp1));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, RCard);
        if (ImGui::BeginPopup("##au_rot")) {
            static const char* modes[] = {"自动（跟随设备）", "手动：0°", "手动：90°", "手动：180°", "手动：270°"};
            for (int i = 0; i < 5; ++i)
                if (ImGui::MenuItem(modes[i], nullptr, ctx.rotation == i - 1) && ctx.act_rotation)
                    ctx.act_rotation(i - 1);
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);

        // centered empty states (only when no live video fills the stage)
        if (!ctx.stage_video) {
            if (!ctx.service_on) {
                CenterNote(a, b, Icon::Power, "接收服务已停止", "开启服务以继续镜像", "开启服务");
            } else if (ctx.device_name.empty()) {
                CenterNote(a, b, Icon::Airplay, "等待 iPhone 连接",
                           "在 iPhone 的屏幕镜像中选择本电脑", nullptr);
            } else if (ctx.device_kind == "Audio") {
                CenterNote(a, b, Icon::Music, "正在接收音频",
                           "设备声音正在通过电脑播放", nullptr);
            } else {
                CenterNote(a, b, Icon::Airplay, "画面同步中",
                           "正在等待视频画面…", nullptr);
            }
        }
        ImGui::PopFont();

        // bottom strip (over the video — subtle scrim for legibility)
        char line[128];
        std::snprintf(line, sizeof(line), "低延迟连接 · %d × %d · %d FPS",
                      ctx.video_w, ctx.video_h, ctx.fps_cap);
        const float strip_y = b.y - ctx.S(30);
        d->AddRectFilled(ImVec2(a.x + ctx.S(10), strip_y), ImVec2(b.x - ctx.S(10), b.y - ctx.S(8)),
                         IM_COL32(255, 255, 255, 140), ctx.S(10));
        d->AddText(ImVec2(a.x + ctx.S(20), strip_y + ctx.S(6)), IM_COL32(96, 110, 100, 255), line);
        const char* right = !ctx.service_on ? "接收已停止" :
                            ctx.device_name.empty() ? "无活动设备" :
                            ctx.video_live ? "画面实时同步" : "画面待恢复";
        const ImVec2 rs = ImGui::CalcTextSize(right);
        d->AddText(ImVec2(b.x - ctx.S(20) - rs.x, strip_y + ctx.S(6)),
                   IM_COL32(96, 110, 100, 255), right);

        ImGui::SetCursorScreenPos(ImVec2(b.x, b.y));
        ImGui::Dummy(ImVec2(1, 1));
    }

    void CenterNote(ImVec2 a, ImVec2 b, Icon ic, const char* title,
                    const char* sub, const char* btn) {
        const float box = ctx.S(54);
        const float cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2 - ctx.S(20);
        auto* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(ImVec2(cx - box / 2, cy - box / 2 - ctx.S(28)),
                         ImVec2(cx + box / 2, cy + box / 2 - ctx.S(28)),
                         IM_COL32(255, 255, 255, 168), ctx.S(16));
        DrawIcon(d, ic, ImVec2(cx - ctx.S(13), cy - box / 2 - ctx.S(15)), ctx.S(26), Muted);
        ImGui::PushFont(ctx.heading);
        const ImVec2 ts = ImGui::CalcTextSize(title);
        d->AddText(ImVec2(cx - ts.x / 2, cy + ctx.S(6)), IM_COL32(69, 78, 71, 255), title);
        ImGui::PopFont();
        ImGui::PushFont(ctx.caption);
        const ImVec2 ss = ImGui::CalcTextSize(sub);
        d->AddText(ImVec2(cx - ss.x / 2, cy + ctx.S(30)), Muted, sub);
        ImGui::PopFont();
        if (btn) {
            ImGui::SetCursorScreenPos(ImVec2(cx - ctx.S(52), cy + ctx.S(58)));
            if (PrimaryButton(btn, ImVec2(ctx.S(104), ctx.S(33))) && ctx.act_service)
                ctx.act_service(true);
        }
    }

    // ---- detail pages share a scrollable card canvas -----------------------------
    // Top-level window (not BeginChild): child-window names carry a hash
    // suffix, which makes them unstable to address from the test engine
    // and from window queries. A positioned top-level window keeps the
    // plain "##au_detail" name.
    void DetailBegin(float w) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetContentRegionAvail().y;
        ImGui::SetNextWindowPos(p);
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##au_detail", nullptr, kScroll | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ctx.S(12), ctx.S(12)));
    }
    void DetailEnd() {
        ImGui::PopStyleVar();
        ImGui::End();
    }

    // two half-width cards on one row
    template <typename A, typename B>
    void CardRow(float w, const char* ta, const char* da, Icon ia, A&& a,
                 const char* tb, const char* db, Icon ib, B&& b) {
        const float col = (w - ctx.S(12)) / 2;
        const ImVec2 row0 = ImGui::GetCursorScreenPos();
        const Card ca = CardBegin(ta, da, col, ia);
        a();
        const float ba = CardEnd(ca);
        ImGui::SetCursorScreenPos(ImVec2(row0.x + col + ctx.S(12), row0.y));
        const Card cb = CardBegin(tb, db, col, ib);
        b();
        const float bb = CardEnd(cb);
        ImGui::SetCursorScreenPos(ImVec2(row0.x, std::max(ba, bb) + ctx.S(12)));
    }

    // ---- 画面 page -----------------------------------------------------------------
    void PageScreen(float w) {
        PageHead("WORKSPACE / DISPLAY", "画面设置", "控制镜像尺寸、方向和设备外观。");
        DetailBegin(w);
        // bezel card (wide)
        {
            const Card c = CardBegin("镜像预览", "控制 iPhone 外观轮廓与机型显示。", w, Icon::Screen);
            bool bezel = ctx.frame_style != -1;
            if (ToggleRow("显示设备边框", "在预览中显示 iPhone 外观轮廓", &bezel) && ctx.act_frame_style)
                ctx.act_frame_style(bezel ? (ctx.frame_style <= 0 ? 0 : ctx.frame_style) : -1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##au_frame_sel", FrameLabel())) {
                if (ImGui::Selectable("自动（跟随机型）", ctx.frame_style == 0) && ctx.act_frame_style)
                    ctx.act_frame_style(0);
                if (ImGui::Selectable("关闭（显示原始画面）", ctx.frame_style == -1) && ctx.act_frame_style)
                    ctx.act_frame_style(-1);
                for (int i = 0; i < (int)ctx.frame_labels.size(); ++i)
                    if (ImGui::Selectable(ctx.frame_labels[i].c_str(), ctx.frame_style == i + 1) && ctx.act_frame_style)
                        ctx.act_frame_style(i + 1);
                ImGui::EndCombo();
            }
            ImGui::Dummy(ImVec2(0, ctx.S(4)));
            CardEnd(c);
        }
        CardRow(w,
            "画面缩放", "调整设备画面的显示比例。", Icon::Sliders, [&] {
                int zoom_pct = static_cast<int>(ctx.stage_zoom * 100.0f + 0.5f);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##au_zoom", &zoom_pct, 30, 300, "%d%%") && ctx.write_zoom)
                    ctx.write_zoom(zoom_pct / 100.0f);
                ImGui::Dummy(ImVec2(0, ctx.S(4)));
                if (Button("缩小##au_zo2", ImVec2(ctx.S(56), 0)) && ctx.act_zoom_out) ctx.act_zoom_out();
                ImGui::SameLine(0, ctx.S(6));
                if (Button("放大##au_zi2", ImVec2(ctx.S(56), 0)) && ctx.act_zoom_in) ctx.act_zoom_in();
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            },
            "画面方向", "自动跟随设备方向，或手动指定角度。", Icon::Rotate, [&] {
                ImGui::SetNextItemWidth(-1);
                static const char* modes[] = {"自动（跟随设备）", "手动：0°", "手动：90°", "手动：180°", "手动：270°"};
                int rsel = ctx.rotation + 1;
                if (ImGui::Combo("##au_rot_set", &rsel, modes, 5) && ctx.act_rotation)
                    ctx.act_rotation(rsel - 1);
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            });
        DetailEnd();
    }

    const char* FrameLabel() {
        static std::string label;
        if (ctx.frame_style == 0) return "自动（跟随机型）";
        if (ctx.frame_style == -1) return "关闭（显示原始画面）";
        if (ctx.frame_style >= 1 && ctx.frame_style <= (int)ctx.frame_labels.size())
            label = ctx.frame_labels[ctx.frame_style - 1];
        else label = "自动（跟随机型）";
        return label.c_str();
    }

    // ---- 音乐 page -----------------------------------------------------------------
    void PageMusic(float w) {
        const bool audio = ctx.device_kind == "Audio";
        PageHead("WORKSPACE / AUDIO", "音乐接收",
                 audio ? "接收 iPhone 音乐，并在桌面查看播放信息。"
                       : "连接 iPhone 并开始播放音乐后，这里显示曲目信息。");
        DetailBegin(w);
        // now playing (wide)
        {
            const Card c = CardBegin("正在播放", nullptr, w, Icon::Music);
            const float cover = ctx.S(110);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float cx = at.x, cy = at.y;
            auto* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cover, cy + cover), Panel, ctx.S(20));
            if (ctx.has_cover)
                d->AddImage(ctx.cover, ImVec2(cx, cy), ImVec2(cx + cover, cy + cover),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
            else
                DrawIcon(d, Icon::Music, ImVec2(cx + cover / 2 - ctx.S(20), cy + cover / 2 - ctx.S(20)),
                         ctx.S(40), Subtle);
            const float tx = cx + cover + Sp6;
            ImGui::SetCursorScreenPos(ImVec2(tx, cy + ctx.S(2)));
            ImGui::PushFont(ctx.caption);
            ImGui::TextColored(Rgba(Subtle), "NOW PLAYING");
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(tx, cy + ctx.S(22)));
            ImGui::PushFont(ctx.display);
            ImGui::PushTextWrapPos(c.p.x + c.w - ctx.S(16));
            ImGui::TextUnformatted(ctx.track_title.empty() ? "—" : ctx.track_title.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(tx, cy + ctx.S(58)));
            ImGui::PushFont(ctx.body);
            ImGui::TextColored(Rgba(Muted), "%s",
                               ctx.track_artist.empty() ? "等待曲目信息" : ctx.track_artist.c_str());
            ImGui::PopFont();
            if (!ctx.track_album.empty()) {
                ImGui::SetCursorScreenPos(ImVec2(tx, cy + ctx.S(82)));
                ImGui::PushFont(ctx.caption);
                ImGui::TextColored(Rgba(Subtle), "%s", ctx.track_album.c_str());
                ImGui::PopFont();
            }
            // progress + transport below the art row
            const float py = cy + cover + ctx.S(10);
            ImGui::SetCursorScreenPos(ImVec2(cx, py));
            const float frac = ctx.prog_total > 0
                ? static_cast<float>(static_cast<double>(ctx.prog_elapsed) / ctx.prog_total) : 0.f;
            ProgressBar(frac, c.w - ctx.S(32), ctx.S(4));
            char t1[16], t2[16];
            std::snprintf(t1, sizeof(t1), "%u:%02u", ctx.prog_elapsed / 60000, (ctx.prog_elapsed / 1000) % 60);
            std::snprintf(t2, sizeof(t2), "%u:%02u", ctx.prog_total / 60000, (ctx.prog_total / 1000) % 60);
            ImGui::PushFont(ctx.caption);
            ImGui::TextColored(Rgba(Subtle), "%s", t1);
            ImGui::SameLine(c.w - ctx.S(16) - ImGui::CalcTextSize(t2).x);
            ImGui::TextColored(Rgba(Subtle), "%s", t2);
            ImGui::PopFont();
            // transport: centered prev/play/next
            const float big = ctx.S(40), small = ctx.S(34);
            const float total = small * 2 + big + Sp4 * 2;
            const float qx = c.p.x + (c.w - total) / 2;
            const float qy = ImGui::GetCursorScreenPos().y + ctx.S(2);
            ImGui::BeginDisabled(!ctx.audio_ready || ctx.audio_pending);
            ImGui::SetCursorScreenPos(ImVec2(qx, qy + (big - small) / 2));
            if (IconButton("##au_prev2", Icon::Prev, "上一首", small) && ctx.act_prev) ctx.act_prev();
            const ImVec2 pb(qx + small + Sp4, qy);
            ImGui::SetCursorScreenPos(pb);
            d->AddCircleFilled(ImVec2(pb.x + big / 2, pb.y + big / 2), big / 2, Ink, 28);
            if (IconButton("##au_play2",
                           ctx.playing ? Icon::Pause : Icon::Play,
                           ctx.playing ? "暂停" : "播放", big, big, White) && ctx.act_play_pause)
                ctx.act_play_pause();
            ImGui::SetCursorScreenPos(ImVec2(qx + small + Sp4 + big + Sp4, qy + (big - small) / 2));
            if (IconButton("##au_next2", Icon::Next, "下一首", small) && ctx.act_next) ctx.act_next();
            ImGui::EndDisabled();
            ImGui::Dummy(ImVec2(1, qy - cy + big + ctx.S(2)));
            CardEnd(c);
        }
        CardRow(w,
            "音频接收", "将 iPhone 音频推送到电脑播放。", Icon::Volume, [&] {
                Caption("音频随音乐 App 的 AirPlay 会话自动接收。");
                if (ctx.audio_pending) Caption("正在发送指令…");
                else if (!ctx.audio_ready) Caption("远程控制不可用");
                else if (ctx.audio_failed) Caption("指令失败或不受支持");
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            },
            "遥控说明", nullptr, Icon::Info, [&] {
                Caption("播放控制来自 iPhone 的 DACP 遥控协议；");
                Caption("确认 iPhone 音乐 App 正在前台播放。");
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            });
        DetailEnd();
    }

    // ---- 录制 page -----------------------------------------------------------------
    void PageRecording(float w) {
        PageHead("WORKSPACE / CAPTURE", "录制设置", "控制录制格式、编码性能和文件保存位置。");
        DetailBegin(w);
        // status (wide)
        {
            const Card c = CardBegin("录制状态", "录制当前镜像画面并导出为 MP4 文件。", w, Icon::Record);
            ImGui::PushFont(ctx.caption);
            const char* st = ctx.recording ? "正在录制" : "准备就绪";
            const ImVec2 sts = ImGui::CalcTextSize(st);
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - sts.x);
            ImGui::TextColored(ctx.recording ? Rgba(Red) : Rgba(Muted), "%s", st);
            ImGui::PopFont();
            char info[160];
            std::snprintf(info, sizeof(info), "%s · %s · %d Mbps",
                          ctx.rec_codec_sel == 0 ? "H.264" : "H.265",
                          ctx.rec_encoder_sel == 0 ? "自动" : ctx.rec_encoder_sel == 1 ? "GPU" : "CPU",
                          ctx.rec_bitrate);
            Caption("%s", info);
            if (ctx.recording) {
                const int64_t s = ctx.rec_elapsed_ms / 1000;
                char tbuf[24];
                std::snprintf(tbuf, sizeof(tbuf), "%02lld:%02lld",
                              static_cast<long long>(s / 60), static_cast<long long>(s % 60));
                ImGui::PushFont(ctx.body);
                ImGui::TextColored(Rgba(Red), "已录制 %s", tbuf);
                ImGui::PopFont();
            } else if (!ctx.rec_path.empty()) {
                ImGui::PushFont(ctx.caption);
                ImGui::PushTextWrapPos(c.p.x + c.w - ctx.S(16));
                ImGui::TextColored(Rgba(Subtle), "%s", ctx.rec_path.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
            }
            if (!ctx.rec_error.empty()) {
                ImGui::PushFont(ctx.caption);
                ImGui::TextColored(Rgba(Danger), "%s", ctx.rec_error.c_str());
                ImGui::PopFont();
            }
            ImGui::Dummy(ImVec2(0, ctx.S(6)));
            if (RecordButton(ctx.recording ? "停止录制" : "开始录制",
                             ImVec2(ctx.S(110), ctx.S(30)), ctx.recording) && ctx.act_toggle_record)
                ctx.act_toggle_record();
            ImGui::Dummy(ImVec2(0, ctx.S(2)));
            CardEnd(c);
        }
        CardRow(w,
            "编码选项", "在质量、体积与编码性能之间取得平衡。", Icon::Sliders, [&] {
                ImGui::SetNextItemWidth(-1);
                static const char* codecs[] = {"H.264 · 兼容性优先", "H.265 / HEVC · 体积更小"};
                if (ImGui::Combo("##au_rcodec", &ctx.rec_codec_sel, codecs, 2) && ctx.write_rec_codec)
                    ctx.write_rec_codec(ctx.rec_codec_sel);
                ImGui::SetNextItemWidth(-1);
                static const char* encs[] = {"自动选择", "GPU 硬件编码", "CPU 软件编码"};
                if (ImGui::Combo("##au_renc", &ctx.rec_encoder_sel, encs, 3) && ctx.write_rec_encoder)
                    ctx.write_rec_encoder(ctx.rec_encoder_sel);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##au_rbr", &ctx.rec_bitrate, 2, 40, "%d Mbps") && ctx.write_rec_bitrate)
                    ctx.write_rec_bitrate(ctx.rec_bitrate);
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            },
            "保存位置", "录制文件默认保存的目录。", Icon::Folder, [&] {
                ImGui::SetNextItemWidth(-ctx.S(80));
                if (ImGui::InputText("##au_recdir", ctx.rec_dir_buf, sizeof(ctx.rec_dir_buf)) && ctx.write_rec_dir)
                    ctx.write_rec_dir(ctx.rec_dir_buf);
                ImGui::SameLine(0, ctx.S(6));
                if (Button("浏览…##au_browse", ImVec2(ctx.S(72), 0)) && ctx.act_browse_dir) ctx.act_browse_dir();
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            });
        DetailEnd();
    }

    // ---- 设置 page -----------------------------------------------------------------
    void PageSettings(float w) {
        PageHead("PREFERENCES / DEVICE", "设置", "更改接收端名称、镜像质量和渲染行为。");
        DetailBegin(w);
        CardRow(w,
            "接收设备", "设置电脑在 iPhone 屏幕镜像列表中显示的名称。", Icon::Airplay, [&] {
                if (!view.name_dirty && view.name_buf[0] == '\0')
                    std::snprintf(view.name_buf, sizeof(view.name_buf), "%s", ctx.receiver_name.c_str());
                ImGui::SetNextItemWidth(-1);
                const bool enter = ImGui::InputText("##au_name", view.name_buf, sizeof(view.name_buf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
                const bool deactivate = ImGui::IsItemDeactivatedAfterEdit();
                std::string cand(view.name_buf);
                const auto first = cand.find_first_not_of(' ');
                if (first == std::string::npos) cand.clear();
                else cand = cand.substr(first, cand.find_last_not_of(' ') - first + 1);
                const bool valid = !cand.empty() && cand.size() <= 50 &&
                                   cand.find('.') == std::string::npos &&
                                   cand.find('\\') == std::string::npos;
                const bool changed = valid && cand != ctx.receiver_name;
                ImGui::BeginDisabled(!valid || !changed);
                if ((Button("应用名称", ImVec2(0, ctx.S(32)), 1) || enter || deactivate) &&
                    valid && changed && ctx.write_name) {
                    ctx.write_name(cand);
                    std::snprintf(view.name_buf, sizeof(view.name_buf), "%s", cand.c_str());
                    view.name_dirty = false;
                }
                ImGui::EndDisabled();
                ImGui::PushFont(ctx.caption);
                if (!valid && cand.size() > 50)
                    ImGui::TextColored(Rgba(Muted), "名称过长（最多 50 字节，约 16 个汉字）。");
                else if (!valid)
                    ImGui::TextColored(Rgba(Muted), "名称不能包含点和反斜杠，也不能全是空格。");
                else
                    ImGui::TextColored(Rgba(Muted), "最多 50 字节。修改后自动保存。");
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            },
            "镜像分辨率", "选择预设分辨率，或设置自定义尺寸。", Icon::Screen, [&] {
                char cur[64];
                std::snprintf(cur, sizeof(cur), "%d × %d", ctx.mirror_w, ctx.mirror_h);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##au_res2", cur)) {
                    for (int i = 0; i < (int)ctx.res_labels.size(); ++i)
                        if (ImGui::Selectable(ctx.res_labels[i].c_str()) && ctx.act_resolution)
                            ctx.act_resolution(i, 1);
                    if (ImGui::Selectable("自定义…")) view.custom_res = true;
                    ImGui::EndCombo();
                }
                if (view.custom_res) {
                    ImGui::SetNextItemWidth(ctx.S(88));
                    ImGui::InputInt("宽##au_cw", &view.custom_w, 0, 0);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ctx.S(88));
                    ImGui::InputInt("高##au_ch", &view.custom_h, 0, 0);
                    if (view.custom_w >= 320 && view.custom_w <= 7680 &&
                        view.custom_h >= 240 && view.custom_h <= 4320 && ctx.on_custom_res)
                        ctx.on_custom_res(view.custom_w, view.custom_h);
                    ImGui::PushFont(ctx.caption);
                    ImGui::TextColored(Rgba(Subtle), "范围：320-7680 × 240-4320");
                    ImGui::PopFont();
                }
                Caption("实际分辨率由设备决定，下次连接时生效。");
                if (ctx.video_w > 0) {
                    ImGui::PushFont(ctx.caption);
                    ImGui::TextColored(Rgba(Subtle), "实际接收：%d × %d", ctx.video_w, ctx.video_h);
                    ImGui::PopFont();
                }
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            });
        // rendering & performance (wide)
        {
            const Card c = CardBegin("渲染与性能", "按电脑性能调整视频解码与同步策略。", w, Icon::Sliders);
            bool hw = ctx.gpu_decode;
            if (ToggleRow("启用 GPU 硬件解码", hw ? "利用显卡降低 CPU 占用" : "使用处理器解码", &hw) && ctx.write_gpu)
                ctx.write_gpu(hw);
            bool vs = ctx.vsync;
            if (ToggleRow("垂直同步", vs ? "减少画面撕裂" : "延迟最低，可能撕裂", &vs) && ctx.write_vsync)
                ctx.write_vsync(vs);
            bool hevc = ctx.hevc;
            if (ToggleRow("HEVC 解码支持", hevc ? "接收并解码 HEVC 视频流" : "强制 H.264（更耗带宽）", &hevc) && ctx.write_hevc)
                ctx.write_hevc(hevc);
            ImGui::SetNextItemWidth(ctx.S(200));
            if (ImGui::SliderInt("##au_fps", &ctx.fps_cap, 1, 60, "%d FPS") && ctx.write_fps)
                ctx.write_fps(ctx.fps_cap);
            Caption("iOS 镜像最高 60 fps；调低更稳定。下次连接时生效。");
            ImGui::Dummy(ImVec2(0, ctx.S(2)));
            CardEnd(c);
        }
        DetailEnd();
    }

    // ---- 系统与网络 page --------------------------------------------------------------
    void PageSystem(float w) {
        PageHead("SYSTEM / NETWORK", "系统与网络", "接收服务、网络权限和应用诊断。");
        DetailBegin(w);
        CardRow(w,
            "防火墙", "检查 Windows 防火墙是否允许接收局域网连接。", Icon::Shield, [&] {
                const bool allowed = ctx.firewall_state == 1;
                const bool need = ctx.firewall_state == 2 || ctx.firewall_state == 4;
                if (ctx.firewall_state == 3) Caption("防火墙由管理员策略管理。");
                else if (allowed) Caption("已放行局域网连接。");
                else if (ctx.firewall_busy) Caption("正在请求授权…");
                else Caption("尚未放行，可能导致设备搜不到或连不上。");
                if (!ctx.firewall_message.empty() && need) {
                    ImGui::PushFont(ctx.caption);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                    ImGui::TextColored(Rgba(Muted), "%s", ctx.firewall_message.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopFont();
                }
                ImGui::Dummy(ImVec2(0, ctx.S(4)));
                ImGui::BeginDisabled(!need || ctx.firewall_busy);
                if (PrimaryButton(need ? "一键放行" : "已放行", ImVec2(-1, ctx.S(33))) && ctx.act_allow_firewall)
                    ctx.act_allow_firewall();
                ImGui::EndDisabled();
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            },
            "诊断与维护", "调试信息与设置恢复。", Icon::Info, [&] {
                bool dg = ctx.diagnostics;
                if (ToggleRow("显示诊断信息", "在窗口底部显示帧率与接收统计", &dg) && ctx.write_diag)
                    ctx.write_diag(dg);
                if (Button("恢复默认设置", ImVec2(-1, ctx.S(32)), 2) && ctx.act_reset_settings)
                    ctx.act_reset_settings();
                if (ImGui::GetTime() < view.reset_notice_until) Caption("已恢复默认设置。");
                ImGui::Dummy(ImVec2(0, ctx.S(2)));
            });
        // network info (wide)
        {
            const Card c = CardBegin("网络信息", nullptr, w, Icon::Wifi);
            ImGui::PushFont(ctx.caption);
            char line[160];
            std::snprintf(line, sizeof(line), "RTSP 端口：7000 · HLS 端口：7100 · mDNS 广播中");
            ImGui::TextColored(Rgba(Muted), "%s", line);
            std::snprintf(line, sizeof(line), "当前画面：%d × %d @ %.0f fps · %llu 帧 · %.1f Mbps",
                          ctx.video_w, ctx.video_h, ctx.video_fps,
                          static_cast<unsigned long long>(ctx.frames_total),
                          ctx.video_live ? ctx.payload_mbps : 0.0f);
            ImGui::TextColored(Rgba(Muted), "%s", line);
            if (!ctx.payload_total.empty())
                ImGui::TextColored(Rgba(Subtle), "累计接收：%s", ctx.payload_total.c_str());
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, ctx.S(2)));
            CardEnd(c);
        }
        DetailEnd();
    }

    // ---- right rail (overview only) --------------------------------------------
    void DrawRightRail(const Layout& lay) {
        const float rx = lay.main_x + lay.main_w + ctx.S(13);
        ImGui::SetNextWindowPos(ImVec2(rx, lay.main_y));
        ImGui::SetNextWindowSize(ImVec2(lay.rail_w, lay.main_h));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##au_rail", nullptr, kScroll);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ctx.S(10)));
        const float w = ImGui::GetContentRegionAvail().x;
        const bool connected = !ctx.device_name.empty();

        // --- connection card ---
        {
            const Card c = CardBegin("连接设备", nullptr, w, Icon::Airplay);
            auto* d = ImGui::GetWindowDrawList();
            const float art = ctx.S(38);
            const ImVec2 dp = ImGui::GetCursorScreenPos();
            d->AddRectFilled(dp, ImVec2(dp.x + art, dp.y + ctx.S(45)),
                             IM_COL32(41, 42, 40, 255), ctx.S(9));
            DrawIcon(d, Icon::Phone, ImVec2(dp.x + 4.0f * ctx.scale, dp.y + 4.5f * ctx.scale),
                     ctx.S(20), White);
            ImGui::SetCursorScreenPos(ImVec2(dp.x + art + Sp3, dp.y + ctx.S(2)));
            ImGui::PushFont(ctx.caption);
            ImGui::TextUnformatted(connected ? ctx.device_name.c_str() : "等待 iPhone");
            ImGui::SetCursorScreenPos(ImVec2(dp.x + art + Sp3, dp.y + ctx.S(20)));
            ImGui::TextColored(Rgba(Muted), connected ? "AirPlay · 本地网络" : "打开控制中心以连接");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(1, ctx.S(50)));
            ImGui::SetCursorScreenPos(ImVec2(c.p.x + ctx.S(16), ImGui::GetCursorScreenPos().y + ctx.S(2)));
            if (Button("断开##au_disc", ImVec2((w - ctx.S(38)) / 2, ctx.S(30)),
                       connected ? 2 : 0) && ctx.act_disconnect)
                ctx.act_disconnect();
            ImGui::SameLine(0, ctx.S(6));
            if (PrimaryButton(connected ? "已镜像" : "开始镜像",
                              ImVec2((w - ctx.S(38)) / 2, ctx.S(30)))) {
                view.page = ViewState::Screen;
            }
            ImGui::Dummy(ImVec2(0, ctx.S(2)));
            CardEnd(c);
        }

        // --- now playing card ---
        {
            const Card c = CardBegin("正在播放", nullptr, w, Icon::Music);
            auto* d = ImGui::GetWindowDrawList();
            const float art = ctx.S(46);
            const ImVec2 dp = ImGui::GetCursorScreenPos();
            d->AddRectFilled(dp, ImVec2(dp.x + art, dp.y + art), Panel, ctx.S(10));
            if (ctx.has_cover)
                d->AddImage(ctx.cover, dp, ImVec2(dp.x + art, dp.y + art),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
            else
                DrawIcon(d, Icon::Music, ImVec2(dp.x + art / 2 - ctx.S(11), dp.y + art / 2 - ctx.S(11)),
                         ctx.S(22), Subtle);
            ImGui::SetCursorScreenPos(ImVec2(dp.x + art + Sp3, dp.y + ctx.S(1)));
            ImGui::PushFont(ctx.caption);
            ImGui::PushTextWrapPos(c.p.x + w - ctx.S(16));
            ImGui::TextUnformatted(ctx.track_title.empty() ? "未在播放" : ctx.track_title.c_str());
            ImGui::PopTextWrapPos();
            ImGui::SetCursorScreenPos(ImVec2(dp.x + art + Sp3, dp.y + ctx.S(22)));
            ImGui::TextColored(Rgba(Muted), "%s",
                               ctx.track_artist.empty() ? "—" : ctx.track_artist.c_str());
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(1, art + ctx.S(4)));
            const float frac = ctx.prog_total > 0
                ? static_cast<float>(static_cast<double>(ctx.prog_elapsed) / ctx.prog_total) : 0.f;
            ProgressBar(frac, w - ctx.S(32), ctx.S(3));
            const float big = ctx.S(32), small = ctx.S(26);
            const float total = small * 2 + big + Sp3 * 2;
            const float qx = c.p.x + (w - total) / 2;
            const float qy = ImGui::GetCursorScreenPos().y + ctx.S(6);
            ImGui::BeginDisabled(!ctx.audio_ready || ctx.audio_pending);
            ImGui::SetCursorScreenPos(ImVec2(qx, qy + (big - small) / 2));
            if (IconButton("##au_rprev", Icon::Prev, "上一首", small) && ctx.act_prev) ctx.act_prev();
            const ImVec2 pb(qx + small + Sp3, qy);
            ImGui::SetCursorScreenPos(pb);
            d->AddCircleFilled(ImVec2(pb.x + big / 2, pb.y + big / 2), big / 2, Ink, 28);
            if (IconButton("##au_rplay",
                           ctx.playing ? Icon::Pause : Icon::Play,
                           ctx.playing ? "暂停" : "播放", big, big, White) && ctx.act_play_pause)
                ctx.act_play_pause();
            ImGui::SetCursorScreenPos(ImVec2(qx + small + Sp3 + big + Sp3, qy + (big - small) / 2));
            if (IconButton("##au_rnext", Icon::Next, "下一首", small) && ctx.act_next) ctx.act_next();
            ImGui::EndDisabled();
            ImGui::Dummy(ImVec2(1, qy - c.p.y + big + ctx.S(10)));
            CardEnd(c);
        }

        // --- recording card ---
        {
            const Card c = CardBegin("屏幕录制", nullptr, w, Icon::Record);
            ImGui::PushFont(ctx.caption);
            const char* st = ctx.recording ? "正在录制" : "准备就绪";
            const ImVec2 sts = ImGui::CalcTextSize(st);
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - sts.x);
            ImGui::TextColored(ctx.recording ? Rgba(Red) : Rgba(Muted), "%s", st);
            ImGui::PopFont();
            char info[96];
            std::snprintf(info, sizeof(info), "%s · %s · %d Mbps",
                          ctx.rec_codec_sel == 0 ? "H.264" : "H.265",
                          ctx.rec_encoder_sel == 0 ? "自动" : ctx.rec_encoder_sel == 1 ? "GPU" : "CPU",
                          ctx.rec_bitrate);
            Caption("%s", info);
            if (ctx.recording) {
                const int64_t s = ctx.rec_elapsed_ms / 1000;
                char tbuf[24];
                std::snprintf(tbuf, sizeof(tbuf), "%02lld:%02lld",
                              static_cast<long long>(s / 60), static_cast<long long>(s % 60));
                ImGui::PushFont(ctx.caption);
                ImGui::TextColored(Rgba(Red), "录制时长 %s", tbuf);
                ImGui::PopFont();
            }
            ImGui::Dummy(ImVec2(0, ctx.S(4)));
            if (RecordButton(ctx.recording ? "停止" : "开始录制",
                             ImVec2(-1, ctx.S(29)), ctx.recording) && ctx.act_toggle_record)
                ctx.act_toggle_record();
            ImGui::Dummy(ImVec2(0, ctx.S(2)));
            CardEnd(c);
        }

        ImGui::PopStyleVar(2);
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // ---- footer (doubles as diagnostics HUD) -------------------------------------
    void DrawFooter(float fw, float fh) {
        ImGui::SetNextWindowPos(ImVec2(ctx.S(16), fh - ctx.S(15) - ctx.S(FooterH)));
        ImGui::SetNextWindowSize(ImVec2(fw - ctx.S(32), ctx.S(FooterH)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctx.S(5), 0));
        ImGui::Begin("##au_footer", nullptr, kPanel);
        ImGui::PushFont(ctx.caption);
        char left[192];
        if (ctx.diagnostics) {
            std::snprintf(left, sizeof(left),
                          "%.0f fps · %d × %d · %llu 帧 · %.1f Mbps · 累计 %s",
                          ctx.video_fps, ctx.video_w, ctx.video_h,
                          static_cast<unsigned long long>(ctx.frames_total),
                          ctx.video_live ? ctx.payload_mbps : 0.0f,
                          ctx.payload_total.c_str());
        } else {
            std::snprintf(left, sizeof(left), "%s  ·  本地网络  ·  RTSP :7000",
                          ctx.service_on ? "服务运行中" : "服务已停止");
        }
        ImGui::TextColored(Rgba(Subtle), "%s", left);
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x
                        - ImGui::CalcTextSize("AirMirror 1.0").x);
        ImGui::TextColored(Rgba(Subtle), "AirMirror 1.0");
        ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleVar();
    }
};

} // namespace ap::video::app_ui
