#pragma once

// main_window.h — the whole AirMirror application window (Qt Widgets).
//
// Layout mirrors the airmirror prototype: rounded top bar, left nav,
// stacked detail pages, right rail on the overview, footer strip.
// The video canvas is a QOpenGLWidget layered under the overview page;
// StreamHub (data plane) is polled by a 30 fps timer on the UI thread.

#include "video/stream_hub.h"
#include "video/video_recorder.h"
#include <QMainWindow>
#include <QRect>
#include <QTimer>
#include <functional>

class QLabel;
class QListWidget;
class QSystemTrayIcon;
class QStackedWidget;
class QShortcut;
class QFrame;

namespace fluent::basicinput { class ToggleSwitch; class Button; class ComboBox; class Slider; }
namespace fluent::textfields { class LineEdit; }
namespace fluent::layout { class Card; }
namespace fluent::navigation { class SelectorBar; }
namespace fluent::status_info { class ProgressBar; }

namespace ap::airplay { struct LiveSettings; }

namespace ap::ui {

class VideoCanvas;
class ToggleSwitch;
class SegmentedControl;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(ap::video::StreamHub& hub, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Wiring from main(): service on/off returning success, and the
    // firewall banner state provider (polled).
    void set_service_toggle(std::function<bool(bool)> fn) { service_toggle_ = std::move(fn); }
    void set_service_running(bool on);
    void set_firewall_provider(std::function<int()> fn, std::function<bool()> busy,
                               std::function<std::string()> msg,
                               std::function<void()> allow);
    void set_reset_settings(std::function<void()> fn) { reset_settings_ = std::move(fn); }
    void set_canvas_assets_dir(const std::wstring& dir);

    // --demo support: mark a fake device active from main() before show().
    void seed_demo_device() { demo_seeded_ = true; }

signals:
    void window_close_requested();
    void zoom_changed(float z);

public slots:
    void toggle_pure_view();
    void snap_window_to_shell();
    void snap_window_to_shell_scaled();
    void apply_window_region();
    void apply_rounded_clip();
    void clear_window_region();
    // Startup straight into the frameless bezel window (--pure). Translucent
    // attributes are applied BEFORE show() so the native window is created
    // layered from the start — runtime toggling WA_TranslucentBackground on
    // an already-composited QOpenGLWidget leaves the DWM blend opaque.
    void enter_pure_view();

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:

protected:
    void closeEvent(QCloseEvent* e) override;
    bool nativeEvent(const QByteArray& type, void* message,
                     qintptr* result) override;
    void handleCloseRequested();
    void timerEvent(QTimerEvent* e) override;

private:
    // ---- UI construction ---------------------------------------------
    QWidget*  build_root();
    QFrame*   build_top_bar();
    QWidget*  build_sidebar();
    QWidget*  build_pages_host();
    QWidget*  build_overview_page();
    QWidget*  build_screen_page();
    QWidget*  build_music_page();
    QWidget*  build_recording_page();
    QWidget*  build_settings_page();
    QWidget*  build_system_page();
    QWidget*  build_right_rail();
    QWidget*  build_footer();

    QFrame*   card(const QString& title, const QString& desc, QWidget* body);
    QLabel*   chip(const QString& text, const char* object);
    ap::video::VideoRecorder::Options current_rec_options() const;
    void save_rec_options();

    // top bar extras
    fluent::basicinput::Button* service_btn_ = nullptr;
    QFrame* top_bar_ = nullptr;
    QWidget* side_host_ = nullptr;
    QWidget* root_widget_ = nullptr;    // central "#root" (QSS bg target)
    QWidget* sidebar_ = nullptr;
    QWidget* footer_ = nullptr;
    bool pure_mode_ = false;
    bool snap_pending_ = false;
    float pure_scale_ = 1.0f;
    float snapped_aspect_ = 0.f;        // aspect the window was last sized to
    float last_applied_scale_ = 1.0f;
    QPoint drag_from_;
    bool dragging_ = false;
    Qt::WindowFlags saved_flags_;
    QRect saved_geo_;
    // tray (close-to-tray while mirroring)
    QSystemTrayIcon* tray_ = nullptr;
    int  close_action_    = 0;   // 0: ask / 1: tray / 2: exit
    bool closing_to_exit_ = false;
    void ensure_tray();

    // sidebar mini-card
    QLabel* mini_status_ = nullptr;
    ToggleSwitch* mini_switch_ = nullptr;   // design-system green switch
    // right rail host (visibility tied to overview page)
    QWidget* rail_host_ = nullptr;
    // music page extras
    QLabel* audio_state_ = nullptr;
    // settings page extras
    QLabel* res_hint_ = nullptr;

    // ---- state sync -----------------------------------------------------
    void tick();                       // 30 fps poll of StreamHub + settings
    void apply_device_state();
    void switch_page(int index);

    ap::video::StreamHub& hub_;
    ap::airplay::LiveSettings* live_ = nullptr;

    // top bar
    QLabel* status_text_ = nullptr;
    QLabel* status_dot_  = nullptr;
    QWidget* brand_mark_  = nullptr;   // custom-painted app mark

    // nav
    QListWidget* nav_list_ = nullptr;   // vertical page nav (was SelectorBar)
    QWidget* nav_pill_ = nullptr;       // sliding white selection pill
    QRect nav_pill_geom_;               // last list geometry we positioned for
    void update_nav_pill(bool animate);
    QStackedWidget* pages_ = nullptr;
    enum Page { Overview = 0, Screen, Music, Recording, Settings, System };

    // overview (chrome parts hidden in pure mode)
    VideoCanvas* canvas_ = nullptr;
    QWidget* overview_head_ = nullptr;
    QFrame*  overview_stage_ = nullptr;
    QWidget* stage_empty_ = nullptr;   // idle "waiting for iPhone" panel
    QWidget* overview_strip_ = nullptr;
    QLabel*  stage_hint_ = nullptr;       // centered empty-state text
    QLabel*  stage_meta_ = nullptr;       // bottom-left info line
    QLabel*  stage_state_= nullptr;       // bottom-right state line
    QLabel*  live_pill_  = nullptr;

    // right rail
    QLabel*  rail_device_ = nullptr;
    QLabel*  rail_device_sub_ = nullptr;
    QLabel*  rail_device_detail_ = nullptr;
    fluent::basicinput::Button* rail_record_ = nullptr;
    QLabel*  rail_stats_ = nullptr;

    // music page
    QLabel*  music_track_ = nullptr;
    QLabel*  music_artist_ = nullptr;
    QLabel*  music_album_ = nullptr;
    fluent::status_info::ProgressBar* music_progress_ = nullptr;
    QLabel*  music_time_ = nullptr;
    fluent::basicinput::Button* music_play_ = nullptr;

    // screen page
    fluent::basicinput::ToggleSwitch* bezel_switch_ = nullptr;
    SegmentedControl* rotation_combo_ = nullptr;   // segmented control, not a combo
    fluent::basicinput::Slider* zoom_slider_ = nullptr;
    QLabel*  zoom_label_ = nullptr;

    // recording page
    QLabel*  rec_state_ = nullptr;
    QLabel*  rec_info_ = nullptr;
    QLabel*  rail_rec_dot_ = nullptr;   // status dot on the overview card
    QLabel*  rail_rec_state_ = nullptr; // status line on the overview card
    fluent::basicinput::ComboBox* rec_codec_ = nullptr;
    fluent::basicinput::ComboBox* rec_encoder_ = nullptr;
    fluent::basicinput::ComboBox* close_combo_ = nullptr;
    fluent::basicinput::Slider* rec_bitrate_ = nullptr;
    fluent::textfields::LineEdit* rec_dir_ = nullptr;
    fluent::basicinput::Button* rec_browse_ = nullptr;

    // settings page
    fluent::textfields::LineEdit* name_edit_ = nullptr;
    fluent::basicinput::Button* name_apply_ = nullptr;
    QLabel*  name_hint_ = nullptr;
    fluent::navigation::SelectorBar* res_seg_ = nullptr;
    fluent::basicinput::ToggleSwitch* gpu_switch_ = nullptr;
    fluent::basicinput::ToggleSwitch* vsync_switch_ = nullptr;
    fluent::basicinput::ToggleSwitch* hevc_switch_ = nullptr;
    fluent::basicinput::Slider* fps_slider_ = nullptr;
    QLabel*  fps_label_ = nullptr;

    // system page
    QLabel*  fw_state_ = nullptr;
    fluent::basicinput::Button* fw_allow_ = nullptr;
    QLabel*  fw_msg_ = nullptr;
    fluent::basicinput::ToggleSwitch* diag_switch_ = nullptr;
    fluent::basicinput::Button* reset_btn_ = nullptr;
    QLabel*  net_info_ = nullptr;

    // footer
    QLabel* footer_left_ = nullptr;

    bool service_on_ = true;
    bool demo_seeded_ = false;
    bool applying_ui_ = false;      // suppress write-back during tick refresh
    std::function<bool(bool)> service_toggle_;
    std::function<int()> firewall_state_;
    std::function<bool()> firewall_busy_;
    std::function<std::string()> firewall_message_;
    std::function<void()> firewall_allow_;
    std::function<void()> reset_settings_;
    int tick_timer_id_ = 0;   // QObject::startTimer(33) → timerEvent
};

} // namespace ap::ui
