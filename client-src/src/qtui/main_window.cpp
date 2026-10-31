// main_window.cpp — AirMirror application window. Layout mirrors the
// airmirror prototype; every widget is standard Qt so sizing/position
// is the layout engine's job (no hand-computed pixels anywhere).

#include "qtui/main_window.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include "qtui/theme.h"
#include "qtui/video_canvas.h"
#include "qtui/controls.h"
#include "qtui/motion.h"

#include <components/basicinput/Button.h>
#include <components/basicinput/ComboBox.h>
#include <components/basicinput/ToggleSwitch.h>
#include <components/basicinput/Slider.h>
#include <components/textfields/LineEdit.h>
#include <components/status_info/InfoBadge.h>
#include <components/layout/Card.h>
#include <components/foundation/ThemeRegistry.h>
#include <components/navigation/SelectorBar.h>
#include <components/status_info/InfoBar.h>
#include <components/status_info/ProgressBar.h>
#include "airplay/live_settings.h"
#include "log.h"

#include <QSystemTrayIcon>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QSlider>
#include <QTimer>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QTimerEvent>
#include <QCloseEvent>
#include <QFileDialog>
#include <QWindow>
#include <QApplication>
#include <QCoreApplication>
#include <QShortcut>
#include <QCheckBox>
#include <QMessageBox>
#include <QScreen>
#include <QPainter>
#include <QStackedLayout>
#include <cmath>
#include <vector>
#include <QMenu>
#include <QCursor>

namespace ap::ui {

static QLabel* mk_label(const QString& text, const char* cls = nullptr,
                        QWidget* parent = nullptr) {
    auto* l = new QLabel(text, parent);
    if (cls) l->setProperty("class", cls);
    return l;
}

static QFrame* hline() {
    auto* f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Sunken);
    f->setFixedHeight(1);
    return f;
}

// "Nothing connected yet" state for the mirror stage. Painted rather than
// composed from labels so the phone outline and the two text lines stay
// optically centred at any window size, and so the whole thing weighs a few
// hundred bytes instead of a widget tree.
//
// The outline breathes (2.8 s, ping-pong) so an idle window still feels alive
// without a spinner. Suppressed entirely when motion is reduced.
class StageEmptyPanel : public QWidget {
public:
    explicit StageEmptyPanel(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        if (!motion_enabled()) return;
        breathe_ = new QVariantAnimation(this);
        breathe_->setDuration(2800);
        breathe_->setStartValue(0.0);
        breathe_->setKeyValueAt(0.5, 1.0);
        breathe_->setEndValue(0.0);
        breathe_->setLoopCount(-1);
        QObject::connect(breathe_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant& v) { phase_ = v.toReal(); update(); });
        breathe_->start();
    }
    QSize sizeHint() const override { return {420, 340}; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const qreal cx = width() / 2.0;
        const qreal cy = height() / 2.0 - 30.0;

        // Device outline — alpha eases between 175 and 235 with the breath.
        QColor stroke("#CFCFC9");
        stroke.setAlpha(int(175 + 60 * phase_));
        QRectF phone(cx - 39, cy - 76, 78, 152);
        QPen pen(stroke);
        pen.setWidthF(1.6);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(phone, 17, 17);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#E6E6E1"));
        p.drawRoundedRect(QRectF(cx - 12, cy - 67, 24, 5), 2.5, 2.5);   // island
        p.drawRoundedRect(QRectF(cx - 14, cy + 63, 28, 4), 2.0, 2.0);   // home bar

        QFont f = font();
        f.setPixelSize(15);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.setPen(QColor("#8C8C8C"));
        p.drawText(QRectF(0, cy + 98, width(), 26),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QStringLiteral("等待 iPhone 连接"));

        f.setPixelSize(12);
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor("#B8B8B8"));
        p.drawText(QRectF(0, cy + 126, width(), 22),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QStringLiteral("控制中心 → 屏幕镜像 → 选择本机"));
    }

private:
    QVariantAnimation* breathe_ = nullptr;
    qreal phase_ = 0.0;
};

static QWidget* toggle_row_widget(const QString& label, const QString& hint,
                                  fluent::basicinput::ToggleSwitch** sw,
                                  QWidget* parent = nullptr) {
    // Pill row: a light-grey rounded block inside the white card, matching the
    // reference's "everything is a pill" language. Scoped selector so the
    // background does not cascade into the labels and switch.
    auto* row = new QWidget(parent);
    row->setObjectName("settingRow");
    row->setStyleSheet(QString(
        "QWidget#settingRow { background:%1; border-radius:16px; }").arg(ColBg));
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(14, 10, 14, 10);
    auto* col = new QVBoxLayout;
    col->setSpacing(1);
    col->addWidget(mk_label(label));
    if (!hint.isEmpty()) col->addWidget(mk_label(hint, "hint"));
    h->addLayout(col, 1);
    auto* t = new fluent::basicinput::ToggleSwitch;
    t->setOnContent("开");
    t->setOffContent("关");
    *sw = t;
    h->addWidget(t, 0, Qt::AlignVCenter);
    return row;
}

// Wraps a block of content in the same light-grey pill surface used by the
// setting rows, so every interactive band inside a card reads as one language.
static QWidget* pill_surface(QWidget* body, int vPad = 12, int hPad = 14) {
    body->setObjectName("settingRow");
    body->setStyleSheet(QString(
        "QWidget#settingRow { background:%1; border-radius:16px; }").arg(ColBg));
    if (auto* lay = body->layout())
        lay->setContentsMargins(hPad, vPad, hPad, vPad);
    return body;
}

// App mark painted straight from the QIcon. A QLabel + setPixmap stretches
// the bitmap to the label's geometry (the icon came out 70x37 instead of a
// square), whereas QIcon::paint honours the aspect ratio and renders the SVG
// at whatever size the widget ends up with.
class BrandMark : public QWidget {
public:
    explicit BrandMark(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(26, 26);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
    void setIcon(const QIcon& icon) { icon_ = icon; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        if (icon_.isNull()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        icon_.paint(&p, rect(), Qt::AlignCenter);
    }

private:
    QIcon icon_;
};

MainWindow::MainWindow(ap::video::StreamHub& hub, QWidget* parent)
    : QMainWindow(parent), hub_(hub) {
    setObjectName("root");
    setWindowTitle("AirMirror — iPhone 投屏");
    resize(1220, 800);
    setMinimumSize(960, 720);

    // Re-theme the component library to the reference palette. Fluent's stock
    // accent is Windows blue; the reference uses one saturated blue for every
    // interactive control and nothing else, so push it into ThemeRegistry and
    // all switches/sliders/progress follow.
    {
        auto snap = fluent::ThemeRegistry::defaultSnapshot();
        auto c = snap.lightColors;
        const QColor accent(ColBlue);           // #0A7CFF
        c.accentDefault   = accent;
        c.accentSecondary = QColor("#0069E0");  // hover
        c.accentTertiary  = QColor("#0059C2");  // pressed
        c.accentDisabled  = QColor("#B9D8FF");
        // Standard buttons/inputs default to near-white, which disappears on
        // the now-white cards. The reference draws every control as a light
        // grey pill, so push the control fills one step down.
        c.controlDefault   = QColor("#F0F0EE");
        c.controlSecondary = QColor("#E8E8E5");  // hover
        c.controlTertiary  = QColor("#DEDEDB");  // pressed
        c.controlDisabled  = QColor("#F5F5F3");
        // Slider tracks paint from controlAltSecondary. It defaults to a
        // near-white that vanished against the pill surface, leaving only the
        // blue knob visible. One step darker and the track reads again.
        c.controlAltSecondary = QColor("#DCDCD8");
        c.controlAltTertiary  = QColor("#CFCFCA");
        fluent::ThemeRegistry::instance()
            .setColors(fluent::FluentElement::Theme::Light, c);
    }
    // Translucent from birth: toggling WA_TranslucentBackground on a live
    // window leaves stale opaque compositing (black pixels where the GL
    // surface is transparent). With the surface layered from the start,
    // pure mode only toggles chrome visibility + geometry — the alpha
    // path never changes. Chrome mode paints opaque backgrounds anyway.
    setAttribute(Qt::WA_TranslucentBackground, true);

    auto* central = build_root();
    setCentralWidget(central);

    live_ = hub.live_settings();
    if (live_) close_action_ = live_->ui_preferences().close_action;

    // settings page: load persisted state
    if (live_) {
        name_edit_->setText(QString::fromStdString(live_->receiver_name()));
    }

    // H: chromeless toggle; Esc: peel back (fullscreen -> chromeless -> quit)
    auto* hk = new QShortcut(QKeySequence("H"), this);
    hk->setContext(Qt::ApplicationShortcut);
    connect(hk, &QShortcut::activated, this, &MainWindow::toggle_pure_view);
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    esc->setContext(Qt::ApplicationShortcut);
    connect(esc, &QShortcut::activated, this, [this] {
        if (isFullScreen()) showNormal();
        else if (pure_mode_) toggle_pure_view();
    });
    canvas_->installEventFilter(this);
    // Right-click on the canvas offers an exit even without a keyboard.
    canvas_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(canvas_, &QWidget::customContextMenuRequested, this, [this](const QPoint&) {
        QMenu m(this);
        if (pure_mode_) m.addAction("退出纯画面 (H)", this, &MainWindow::toggle_pure_view);
        m.addAction(isFullScreen() ? "退出全屏" : "全屏", this, [this] {
            isFullScreen() ? showNormal() : showFullScreen();
        });
        m.exec(QCursor::pos());
    });

    // 30 fps state poll: StreamHub snapshots + LiveSettings + firewall.
    tick_timer_id_ = startTimer(33);

    // App QSS lives on qApp, NOT on the window: toggle_pure_view swaps the
    // window-level stylesheet for "background: transparent" in pure mode,
    // which would otherwise WIPE the whole app stylesheet for every child
    // (black chrome after restoring, unstyled controls in pure mode).
    qApp->setStyleSheet(app_style_sheet());
}

MainWindow::~MainWindow() = default;

void MainWindow::handleCloseRequested() {
    if (closing_to_exit_) {
        hide();
        QApplication::quit();
        return;
    }
    if (close_action_ == 1) {          // remembered: tray
        hide();
        ensure_tray();
        return;
    }
    if (close_action_ == 2) {          // remembered: exit
        closing_to_exit_ = true;
        hide();
        QApplication::quit();
        return;
    }
    // Ask. Hide synchronously (no modal loop here), then open the dialog
    // from the event loop - running it inside the close pipeline deadlocks
    // with the GL canvas.
    hide();
    ensure_tray();
    QTimer::singleShot(120, this, [this] {
        QMessageBox box(QMessageBox::NoIcon,
                        QStringLiteral("关闭 AirMirror"),
                        QStringLiteral("接收服务仍在后台运行。下次关闭窗口时："),
                        QMessageBox::NoButton, nullptr);
        auto* dont_ask = new QCheckBox(QStringLiteral("不再询问"), &box);
        box.setCheckBox(dont_ask);
        box.addButton(QStringLiteral("保持托盘运行"), QMessageBox::AcceptRole);
        auto* quit_btn = box.addButton(QStringLiteral("退出程序"),
                                       QMessageBox::DestructiveRole);
        box.exec();
        if (dont_ask->isChecked()) {
            close_action_ = (box.clickedButton() == quit_btn) ? 2 : 1;
            if (live_) {
                auto prefs = live_->ui_preferences();
                prefs.close_action = close_action_;
                live_->set_ui_preferences(prefs);
            }
        }
        if (box.clickedButton() == quit_btn) {
            closing_to_exit_ = true;
            hide();
            QApplication::quit();
        }
    });
}

// WM_CLOSE never reaches QWidget::close() in this app (it hangs before the
// event filter sees QEvent::Close - see close_probe.log traces), so the
// message is intercepted here and routed through the same logic the tray
// menu uses.
bool MainWindow::nativeEvent(const QByteArray& type, void* message,
                             qintptr* result) {
    if (type == "windows_generic_MSG") {
        auto* m = static_cast<MSG*>(message);
        if (m->message == WM_CLOSE) {
            handleCloseRequested();
            *result = 0;
            return true;                  // consumed
        }
    }
    return QMainWindow::nativeEvent(type, message, result);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    // Explicit quit (tray menu): go straight through, no prompts.
    if (closing_to_exit_) {
        e->accept();
        QApplication::quit();
        return;
    }
    if (close_action_ == 1) {          // remembered: minimize to tray
        e->ignore();
        hide();
        ensure_tray();
        return;
    }
    if (close_action_ == 2) {          // remembered: exit
        closing_to_exit_ = true;
        e->accept();
        QApplication::quit();
        return;
    }
    // Ask. Hide to the tray FIRST (synchronously, no modal loop inside the
    // close handler) and open the dialog from the event loop afterwards -
    // running QMessageBox::exec() directly inside closeEvent deadlocked the
    // UI thread with the GL canvas repaint, so the dialog never appeared and
    // the whole app froze.
    e->ignore();
    hide();
    ensure_tray();
    QTimer::singleShot(120, this, [this] {
        QMessageBox box(QMessageBox::NoIcon,
                        QStringLiteral("关闭 AirMirror"),
                        QStringLiteral("接收服务仍在后台运行。下次关闭窗口时："),
                        QMessageBox::NoButton, nullptr);
        auto* dont_ask = new QCheckBox(QStringLiteral("不再询问"), &box);
        box.setCheckBox(dont_ask);
        box.addButton(QStringLiteral("保持托盘运行"), QMessageBox::AcceptRole);
        auto* quit_btn = box.addButton(QStringLiteral("退出程序"),
                                       QMessageBox::DestructiveRole);
        box.exec();
        if (dont_ask->isChecked()) {
            close_action_ = (box.clickedButton() == quit_btn) ? 2 : 1;
            if (live_) {
                auto prefs = live_->ui_preferences();
                prefs.close_action = close_action_;
                live_->set_ui_preferences(prefs);
            }
        }
        if (box.clickedButton() == quit_btn) {
            closing_to_exit_ = true;
            close();
        }
    });
}

void MainWindow::ensure_tray() {
    if (tray_) return;
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(QApplication::windowIcon());   // same unified artwork
    tray_->setToolTip("AirMirror — iPhone 投屏");
    auto* menu = new QMenu(this);
    menu->addAction("显示窗口", this, [this] {
        show();
        setWindowState(windowState() & ~Qt::WindowMinimized);
        activateWindow();
    });
    menu->addAction("退出（停止投屏服务）", this, [this] {
        closing_to_exit_ = true;
        hide();
        QApplication::quit();
    });
    tray_->setContextMenu(menu);
    QObject::connect(tray_, &QSystemTrayIcon::activated, this,
                     [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
            show();
            setWindowState(windowState() & ~Qt::WindowMinimized);
            activateWindow();
        }
    });
    tray_->show();
}

// ---------------------------------------------------------------------------
// root: top bar / [sidebar | pages] / footer
// ---------------------------------------------------------------------------
QWidget* MainWindow::build_root() {
    auto* root = new QWidget;
    root->setObjectName("root");
    root_widget_ = root;
    auto* v = new QVBoxLayout(root);
    v->setContentsMargins(16, 14, 16, 12);
    v->setSpacing(12);

    // Static lift for the structural surfaces (top bar, sidebar). The content
    // cards get the animated one from HoverLift; these two never move, so a
    // plain effect is enough and costs a single render pass each.
    auto lift = [](QWidget* w, qreal blur, int dy, int alpha) {
        auto* eff = new QGraphicsDropShadowEffect(w);
        eff->setBlurRadius(blur);
        eff->setOffset(0, dy);
        eff->setColor(QColor(24, 24, 20, alpha));
        w->setGraphicsEffect(eff);
    };

    top_bar_ = build_top_bar();
    lift(top_bar_, 22, 4, 38);
    v->addWidget(top_bar_);

    side_host_ = new QWidget;
    auto* body = new QHBoxLayout(side_host_);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);
    sidebar_ = build_sidebar();
    lift(sidebar_, 22, 4, 34);
    body->addWidget(sidebar_, 0);
    body->addWidget(build_pages_host(), 1);
    v->addWidget(side_host_, 1);

    footer_ = build_footer();
    v->addWidget(footer_);
    return root;
}

QWidget* MainWindow::build_pages_host() {
    pages_ = new QStackedWidget;
    pages_->addWidget(build_overview_page());   // Overview
    pages_->addWidget(build_screen_page());     // Screen
    pages_->addWidget(build_music_page());      // Music
    pages_->addWidget(build_recording_page());  // Recording
    pages_->addWidget(build_settings_page());   // Settings
    pages_->addWidget(build_system_page());     // System

    auto* host = new QWidget;
    auto* h = new QHBoxLayout(host);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);
    h->addWidget(pages_, 1);
    rail_host_ = build_right_rail();
    h->addWidget(rail_host_, 0);
    return host;
}

QFrame* MainWindow::build_top_bar() {
    auto* bar = new QFrame;
    bar->setObjectName("topBar");
    bar->setFixedHeight(54);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(16, 8, 16, 8);
    h->setSpacing(10);

    auto* mark = new BrandMark;
    // Same artwork as the exe/taskbar/tray, but drawn from a vector master.
    // The shipped app.ico only ever contained a single 16x16 bitmap, so asking
    // Qt for a 30dp icon meant upscaling 16px by ~2.4x — that was the blur.
    // The SVG is the same three shapes, so it renders clean at any size.
    {
        wchar_t exe_path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        std::wstring dir(exe_path);
        const size_t slash = dir.find_last_of(L'\\');
        if (slash != std::wstring::npos) dir.resize(slash + 1);

        const QString svg = QString::fromStdWString(dir + L"assets\\app-icon.svg");
        const QString ico = QString::fromStdWString(dir + L"app.ico");
        const QString source = QFile::exists(svg) ? svg : ico;

        if (QFile::exists(source)) {
            const QIcon app_icon(source);
            mark->setIcon(app_icon);
            setWindowIcon(app_icon);
            QApplication::setWindowIcon(app_icon);
        }
    }
    brand_mark_ = mark;
    h->addWidget(brand_mark_, 0, Qt::AlignVCenter);

    auto* brand_col = new QVBoxLayout;
    brand_col->setSpacing(0);
    brand_col->addWidget(mk_label("AirMirror", "brandName"));
    brand_col->addWidget(mk_label("iPhone Receiver", "brandSub"));
    h->addLayout(brand_col);

    h->addStretch();
    status_dot_ = mk_label("●");
    status_dot_->setStyleSheet(QString("color:%1;font-size:9px;").arg(ColGreen));
    h->addWidget(status_dot_);
    status_text_ = mk_label("等待设备连接", "statusText");
    h->addWidget(status_text_);
    h->addStretch();

    // No service button here: the sidebar mini-card owns the on/off
    // switch (one control per function). The top bar only reports state.
    service_btn_ = nullptr;
    return bar;
}

QWidget* MainWindow::build_sidebar() {
    auto* side = new QWidget;
    side->setFixedWidth(190);
    // Expand vertically to the body height: the QListWidget sizeHint
    // (6 items) would otherwise cap the card at ~324 px and hide the
    // bottom service mini-card entirely.
    side->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    side->setMinimumHeight(560);   // nav (6 items) + service mini-card
    // Scope the card chrome to the sidebar itself. An unscoped stylesheet on a
    // QWidget behaves like `*` and cascades background *and border* into every
    // child — that is what drew the stray 1px rounded outline around the
    // service row (and its wrapper) at the bottom.
    side->setObjectName("sidebar");
    side->setStyleSheet(QString(
        "QWidget#sidebar { background:%1; border:none; border-radius:24px; }")
        .arg(ColPanel));
    auto* v = new QVBoxLayout(side);
    v->setContentsMargins(10, 14, 10, 14);
    v->setSpacing(2);

    auto group = [v](const char* text) {
        auto* l = mk_label(text);
        l->setProperty("class", "navGroup");
        l->setStyleSheet("color:%subtle%;font-size:10px;font-weight:600;"
                         "letter-spacing:1px;padding:8px 12px 2px 12px;");
        v->addWidget(l);
    };
    group("WORKSPACE");
    v->addSpacing(2);

    // Vertical page list. SelectorBar is a HORIZONTAL tab strip — dropped
    // into this 190 px sidebar it clipped to a single visible item, hiding
    // every page but 总览 from the user.
    auto* nav_wrap = new QWidget;
    auto* nw = new QVBoxLayout(nav_wrap);
    nw->setContentsMargins(0, 0, 0, 0);
    auto* nav_list = new QListWidget;
    nav_list->setObjectName("nav");
    nav_list->setFrameShape(QFrame::NoFrame);
    // The selection is painted by nav_pill_ *underneath* the list, so the list
    // and its viewport must not paint a background of their own.
    nav_list->setAttribute(Qt::WA_TranslucentBackground, true);
    nav_list->viewport()->setAutoFillBackground(false);
    for (const char* label : {"总览", "画面", "音乐", "录制", "设置", "系统与网络"})
        nav_list->addItem(QString::fromUtf8(label));
    nav_list->setCurrentRow(Page::Overview);
    connect(nav_list, &QListWidget::currentRowChanged,
            this, [this](int row) {
        if (row >= 0 && row < 6) switch_page(row);
    });
    nw->addWidget(nav_list);
    v->addWidget(nav_wrap, 1);

    // Sliding selection pill. A list-item highlight cannot animate (QSS has no
    // transitions), so the white pill is a sibling stacked beneath the list and
    // moved with a geometry animation instead.
    nav_pill_ = new QWidget(nav_wrap);
    nav_pill_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    nav_pill_->setStyleSheet("background:#FFFFFF;border-radius:14px;");
    nav_pill_->raise();
    nav_pill_->stackUnder(nav_list);
    nav_pill_->hide();
    nav_list_ = nav_list;

    // Bottom service row. A hairline divider gives it its own zone — without
    // one the row floats under the nav list and reads as a stray control.
    // The switch is the design-system one (green #24966B, 44x26): the Fluent
    // default is Windows blue, which was the only cool-coloured element in an
    // otherwise warm palette.
    auto* rule_wrap = new QWidget;
    rule_wrap->setStyleSheet("background: transparent; border: none;");
    auto* rl = new QHBoxLayout(rule_wrap);
    rl->setContentsMargins(8, 6, 8, 0);
    auto* rule = new QFrame;
    rule->setFixedHeight(1);
    rule->setStyleSheet(QString("background:%1;border:none;").arg(ColBorder));
    rl->addWidget(rule);
    v->addWidget(rule_wrap);

    auto* mini = new QWidget;
    mini->setStyleSheet("background: transparent; border: none;");
    mini->setFixedHeight(46);
    auto* mh = new QHBoxLayout(mini);
    mh->setContentsMargins(12, 0, 8, 0);
    mh->setSpacing(8);

    mini_status_ = mk_label("接收服务", "svcLabel");
    mh->addWidget(mini_status_, 1);
    mini_switch_ = new ToggleSwitch;
    mini_switch_->setChecked(service_on_);
    connect(mini_switch_, &QAbstractButton::toggled, this, [this](bool on) {
        if (service_toggle_ && service_toggle_(on)) {
            set_service_running(on);
        } else {
            const QSignalBlocker b(mini_switch_);
            mini_switch_->setChecked(service_on_);   // revert on failure
        }
    });
    mh->addWidget(mini_switch_, 0, Qt::AlignVCenter);
    v->addWidget(mini);

    // Group headers inside the list would need custom items; simpler:
    // paint them as fixed labels above the list and between sections.
    // (Prototype fidelity kept with two static labels.)
    return side;
}

QWidget* MainWindow::build_overview_page() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    // page head (hidden in pure mode)
    overview_head_ = new QWidget;
    auto* hh = new QHBoxLayout(overview_head_);
    hh->setContentsMargins(4, 0, 4, 0);
    auto* hcol = new QVBoxLayout;
    hcol->setSpacing(2);
    hcol->addWidget(mk_label("WORKSPACE / OVERVIEW", "eyebrow"));
    hcol->addWidget(mk_label("屏幕镜像", "pageTitle"));
    hcol->addWidget(mk_label("把 iPhone 画面自然地延伸到桌面。", "pageSub"));
    hh->addLayout(hcol, 1);
    auto* full_btn = new fluent::basicinput::Button("全屏");
    auto* pure_btn = new fluent::basicinput::Button("纯画面");
    connect(full_btn, &QPushButton::clicked, this, [this] {
        if (isFullScreen()) showNormal(); else showFullScreen();
    });
    connect(pure_btn, &QPushButton::clicked, this, &MainWindow::toggle_pure_view);
    hh->addWidget(full_btn);
    hh->addWidget(pure_btn);
    v->addWidget(overview_head_);

    // stage card with video canvas (card chrome hidden in pure mode)
    overview_stage_ = new QFrame;
    overview_stage_->setObjectName("stageCard");
    // StackAll so the idle panel can float above the GL surface. The panel is
    // mouse-transparent and hides itself the moment frames arrive.
    auto* stage_stack = new QStackedLayout(overview_stage_);
    stage_stack->setStackingMode(QStackedLayout::StackAll);
    stage_stack->setContentsMargins(1, 1, 1, 1);
    canvas_ = new VideoCanvas(hub_);
    stage_stack->addWidget(canvas_);
    stage_empty_ = new StageEmptyPanel;
    stage_stack->addWidget(stage_empty_);
    stage_stack->setCurrentWidget(stage_empty_);
    v->addWidget(overview_stage_, 1);

    // bottom strip (hidden in pure mode)
    overview_strip_ = new QWidget;
    auto* sh = new QHBoxLayout(overview_strip_);
    sh->setContentsMargins(8, 0, 8, 0);
    stage_meta_ = mk_label("低延迟连接", "hint");
    sh->addWidget(stage_meta_);
    sh->addStretch();
    stage_state_ = mk_label("无活动设备", "hint");
    sh->addWidget(stage_state_);
    v->addWidget(overview_strip_);
    return page;
}

QWidget* MainWindow::build_screen_page() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(14, 6, 14, 14);
    v->setSpacing(10);
    v->addWidget(mk_label("WORKSPACE / DISPLAY", "eyebrow"));
    v->addWidget(mk_label("画面设置", "pageTitle"));
    v->addWidget(mk_label("控制镜像尺寸、方向和设备外观。", "pageSub"));

    // wide bezel card
    auto bezel_body = new QWidget;
    auto* bv = new QVBoxLayout(bezel_body);
    bv->setSpacing(8);
    bv->addWidget(toggle_row_widget("显示设备边框", "为画面套上对应机型的手机外壳", &bezel_switch_));
    auto* bezel = card("镜像预览", "控制 iPhone 外观轮廓与机型显示。", bezel_body);
    v->addWidget(bezel);

    // two half cards: zoom + rotation
    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    auto zoom_body = new QWidget;
    auto* zv = new QVBoxLayout(zoom_body);
    zoom_slider_ = new fluent::basicinput::Slider(Qt::Horizontal);
    zoom_slider_->setRange(30, 300);
    zoom_label_ = mk_label("100%", "hint");
    auto* zrow = new QHBoxLayout;
    zrow->addWidget(zoom_slider_, 1);
    zrow->addWidget(zoom_label_);
    zv->addLayout(zrow);
    zv->addStretch();
    pill_surface(zoom_body);
    auto* zoom_card = card("画面缩放", "调整设备画面的显示比例。", zoom_body);
    row->addWidget(zoom_card, 1);

    auto rot_body = new QWidget;
    auto* rv = new QVBoxLayout(rot_body);
    rv->setSpacing(6);
    // A segmented control instead of the library ComboBox: five mutually
    // exclusive angles are exactly what segments are for, and the sliding
    // pill gives the transition some motion. The ComboBox painted as a bare
    // underline, which looked unfinished next to the rounded cards.
    auto* rot_seg = new SegmentedControl;
    for (const char* label : {"自动", "0°", "90°", "180°", "270°"})
        rot_seg->addItem(QString::fromUtf8(label));
    rv->addWidget(rot_seg);
    rot_seg->setCurrentIndex(0);
    rv->addStretch();
    pill_surface(rot_body);
    rotation_combo_ = rot_seg;
    auto* rot_card = card("画面方向", "自动跟随设备方向，或手动指定角度。", rot_body);
    row->addWidget(rot_card, 1);
    v->addLayout(row);
    v->addStretch();

    // wiring
    connect(bezel_switch_, &fluent::basicinput::ToggleSwitch::toggled, this, [this](bool on) {
        if (applying_ui_ || !live_) return;
        const int cur = live_->frame_style.load();
        live_->frame_style.store(on ? (cur <= 0 ? 0 : cur) : -1);
    });
    connect(zoom_slider_, &QSlider::valueChanged, this, [this](int v) {
        zoom_label_->setText(QString::number(v) + "%");
        emit zoom_changed(v / 100.0);
    });
    connect(rot_seg, &SegmentedControl::currentChanged, this, [this](int i) {
        if (applying_ui_ || !live_) return;
        live_->video_rotation.store(i - 1);
    });
    return page;
}

QWidget* MainWindow::build_music_page() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(14, 6, 14, 14);
    v->setSpacing(10);
    v->addWidget(mk_label("WORKSPACE / AUDIO", "eyebrow"));
    v->addWidget(mk_label("音乐接收", "pageTitle"));
    v->addWidget(mk_label("连接 iPhone 并开始播放音乐后，这里显示曲目信息。", "pageSub"));

    // wide now-playing card
    auto np_body = new QWidget;
    auto* nh = new QHBoxLayout(np_body);
    nh->setSpacing(16);
    auto* art = mk_label("♪");
    art->setFixedSize(96, 96);
    art->setAlignment(Qt::AlignCenter);
    art->setStyleSheet(QString(
        "background:%1;border-radius:17px;font-size:34px;color:%2;")
        .arg(ColPanel).arg(ColSubtle));
    nh->addWidget(art);
    auto* tcol = new QVBoxLayout;
    tcol->setSpacing(4);
    tcol->addWidget(mk_label("NOW PLAYING", "eyebrow"));
    music_track_ = mk_label("—", "pageTitle");
    music_artist_ = mk_label("等待曲目信息");
    music_album_ = mk_label("", "hint");
    tcol->addWidget(music_track_);
    tcol->addWidget(music_artist_);
    tcol->addWidget(music_album_);
    tcol->addStretch();
    music_progress_ = new fluent::status_info::ProgressBar;
    music_progress_->setRange(0, 1000);
    tcol->addWidget(music_progress_);
    music_time_ = mk_label("0:00 / 0:00", "hint");
    music_time_->setAlignment(Qt::AlignRight);
    tcol->addWidget(music_time_);
    nh->addLayout(tcol, 1);
    auto* np_card = card("正在播放", {}, np_body);
    v->addWidget(np_card);

    // transport row
    auto* transport = new QHBoxLayout;
    transport->addStretch();
    auto* prev = new fluent::basicinput::Button("⏮");
    music_play_ = new fluent::basicinput::Button("播放");
    music_play_->setObjectName("primary");
    auto* next = new fluent::basicinput::Button("⏭");
    transport->addWidget(prev);
    transport->addWidget(music_play_);
    transport->addWidget(next);
    transport->addStretch();
    v->addLayout(transport);

    // two half cards
    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    auto a_body = new QWidget;
    auto* av = new QVBoxLayout(a_body);
    av->addWidget(mk_label("音频随音乐 App 的 AirPlay 会话自动接收。", "hint"));
    audio_state_ = mk_label("", "hint");
    av->addWidget(audio_state_);
    row->addWidget(card("音频接收", "将 iPhone 音频推送到电脑播放。", a_body), 1);

    auto d_body = new QWidget;
    auto* dv = new QVBoxLayout(d_body);
    dv->addWidget(mk_label("播放控制来自 iPhone 的 DACP 遥控协议；", "hint"));
    dv->addWidget(mk_label("确认 iPhone 音乐 App 正在前台播放。", "hint"));
    row->addWidget(card("遥控说明", {}, d_body), 1);
    v->addLayout(row);
    v->addStretch();

    connect(prev, &QPushButton::clicked, this, [this] {
        hub_.audio_command_pending_.store(true);
        if (hub_.audio_control_handler) hub_.audio_control_handler(ap::airplay::MediaCommand::Previous, 0);
    });
    connect(music_play_, &QPushButton::clicked, this, [this] {
        hub_.audio_command_pending_.store(true);
        if (hub_.audio_control_handler) hub_.audio_control_handler(ap::airplay::MediaCommand::PlayPause, 0);
    });
    connect(next, &QPushButton::clicked, this, [this] {
        hub_.audio_command_pending_.store(true);
        if (hub_.audio_control_handler) hub_.audio_control_handler(ap::airplay::MediaCommand::Next, 0);
    });
    return page;
}

QWidget* MainWindow::build_recording_page() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(14, 6, 14, 14);
    v->setSpacing(10);
    v->addWidget(mk_label("WORKSPACE / CAPTURE", "eyebrow"));
    v->addWidget(mk_label("录制设置", "pageTitle"));
    v->addWidget(mk_label("控制录制格式、编码性能和文件保存位置。", "pageSub"));

    // Recording status moved to the overview rail card.

    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    auto enc_body = new QWidget;
    auto* ev = new QVBoxLayout(enc_body);
    rec_codec_ = new fluent::basicinput::ComboBox;
    rec_codec_->addItems({"H.264 · 兼容性优先", "H.265 / HEVC · 体积更小"});
    rec_encoder_ = new fluent::basicinput::ComboBox;
    rec_encoder_->addItems({"自动选择", "GPU 硬件编码", "CPU 软件编码"});
    ev->addWidget(rec_codec_);
    ev->addWidget(rec_encoder_);
    auto* brow = new QHBoxLayout;
    brow->addWidget(mk_label("码率", "hint"));
    rec_bitrate_ = new fluent::basicinput::Slider(Qt::Horizontal);
    rec_bitrate_->setRange(2, 40);
    brow->addWidget(rec_bitrate_, 1);
    ev->addLayout(brow);
    row->addWidget(card("编码选项", "在质量、体积与编码性能之间取得平衡。", enc_body), 1);

    auto dir_body = new QWidget;
    auto* dv = new QVBoxLayout(dir_body);
    auto* drow = new QHBoxLayout;
    rec_dir_ = new fluent::textfields::LineEdit;
    // Fill the default unconditionally: live_ is still null while the pages are
    // being built, so the previous "if (live_)" guard left this field blank and
    // the user could not see where recordings go.
    rec_dir_->setText(
        QString::fromStdString(ap::video::VideoRecorder::default_directory()));
    if (live_) {
        const auto prefs = live_->ui_preferences();
        const QString saved = QString::fromStdString(prefs.recording_directory);
        // Older builds defaulted to the executable's folder and persisted it.
        // Treat that as "never chosen" so existing installs move to
        // <Videos>\AirMirror instead of keeping recordings next to the exe.
        if (!saved.isEmpty() &&
            QDir(saved) != QDir(QCoreApplication::applicationDirPath())) {
            rec_dir_->setText(saved);
        }
        rec_codec_->setCurrentIndex(prefs.recording_codec);
        rec_encoder_->setCurrentIndex(prefs.recording_encoder);
        rec_bitrate_->setValue(prefs.recording_bitrate);
    }
    drow->addWidget(rec_dir_, 1);
    rec_browse_ = new fluent::basicinput::Button("浏览…");
    drow->addWidget(rec_browse_);
    dv->addLayout(drow);
    row->addWidget(card("保存位置", "录制文件默认保存的目录。", dir_body), 1);
    v->addLayout(row);
    v->addStretch();

    connect(rec_browse_, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "选择录制保存文件夹", rec_dir_->text());
        if (!dir.isEmpty()) rec_dir_->setText(dir);
    });
    connect(rec_codec_, &QComboBox::activated, this, [this](int) { save_rec_options(); });
    connect(rec_encoder_, &QComboBox::activated, this, [this](int) { save_rec_options(); });
    connect(rec_bitrate_, &QSlider::valueChanged, this, [this](int) { save_rec_options(); });
    return page;
}

QWidget* MainWindow::build_settings_page() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(14, 6, 14, 14);
    v->setSpacing(10);
    v->addWidget(mk_label("PREFERENCES / DEVICE", "eyebrow"));
    v->addWidget(mk_label("设置", "pageTitle"));
    v->addWidget(mk_label("更改接收端名称、镜像质量和渲染行为。", "pageSub"));

    // receiver name
    auto name_body = new QWidget;
    auto* nv = new QVBoxLayout(name_body);
    name_edit_ = new fluent::textfields::LineEdit;
    name_apply_ = new fluent::basicinput::Button("应用名称");
    name_apply_->setObjectName("primary");
    name_hint_ = mk_label("最多 50 字节。修改后自动保存。", "hint");
    nv->addWidget(name_edit_);
    nv->addWidget(name_apply_);
    nv->addWidget(name_hint_);
    auto* name_card = card("接收设备",
                           "设置电脑在 iPhone 屏幕镜像列表中显示的名称。",
                           pill_surface(name_body));

    // resolution: segmented presets (few options -> no dropdown)
    auto res_body = new QWidget;
    auto* rv = new QVBoxLayout(res_body);
    res_seg_ = new fluent::navigation::SelectorBar;
    static const struct { int w, h; const char* label; } kSegRes[] = {
        {1920, 1080, "1080p"}, {2560, 1440, "2K"},
        {1920, 1920, "1:1"},   {3840, 2160, "4K"},
    };
    for (const auto& r : kSegRes) res_seg_->addItem(QString::fromUtf8(r.label));
    rv->addWidget(res_seg_);
    res_hint_ = mk_label("", "hint");
    rv->addWidget(res_hint_);
    auto* res_card = card("镜像分辨率", "实际接收分辨率由 iPhone 决定，此项是上限。",
                          pill_surface(res_body));

    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    row->addWidget(name_card, 1);
    row->addWidget(res_card, 1);
    v->addLayout(row);

    // rendering & performance (wide)
    auto perf_body = new QWidget;
    auto* pv = new QVBoxLayout(perf_body);
    pv->addWidget(toggle_row_widget("GPU 硬件解码", "用显卡解码，降低 CPU 占用", &gpu_switch_));
    pv->addWidget(toggle_row_widget("垂直同步", "减少画面撕裂", &vsync_switch_));
    pv->addWidget(toggle_row_widget("HEVC 解码", "接收 H.265 流，更省带宽", &hevc_switch_));
    auto* fps_row = new QWidget;
    auto* frow = new QHBoxLayout(fps_row);
    frow->addWidget(mk_label("帧率上限"));
    fps_slider_ = new fluent::basicinput::Slider(Qt::Horizontal);
    fps_slider_->setRange(1, 60);
    fps_label_ = mk_label("60 FPS", "hint");
    fps_label_->setMinimumWidth(52);
    fps_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    frow->addWidget(fps_slider_, 1);
    frow->addWidget(fps_label_);
    pv->addWidget(pill_surface(fps_row));
    auto* perf_card = card("渲染与性能", "按电脑性能调整视频解码与同步策略。", perf_body);
    v->addWidget(perf_card);
    v->addStretch();

    // name autosave on editing finished (matches old UX contract)
    connect(name_edit_, &QLineEdit::editingFinished, this, [this] {
        const std::string cand = name_edit_->text().trimmed().toStdString();
        const bool valid = !cand.empty() && cand.size() <= 50 &&
                           cand.find('.') == std::string::npos &&
                           cand.find('\\') == std::string::npos;
        if (!valid) {
            name_hint_->setText(cand.size() > 50
                ? "名称过长（最多 50 字节，约 16 个汉字）。"
                : "名称不能包含点和反斜杠，也不能全是空格。");
            return;
        }
        if (live_ && cand != live_->receiver_name()) {
            live_->set_receiver_name(cand);
            if (name_edit_) name_edit_->setText(QString::fromStdString(cand));
        }
        name_hint_->setText("最多 50 字节。修改后自动保存。");
    });
    connect(gpu_switch_, &fluent::basicinput::ToggleSwitch::toggled, this, [this](bool v) {
        if (!applying_ui_ && live_) live_->mirror_hwaccel.store(v);
    });
    connect(vsync_switch_, &fluent::basicinput::ToggleSwitch::toggled, this, [this](bool v) {
        if (!applying_ui_ && live_) live_->vsync_enabled.store(v);
    });
    connect(hevc_switch_, &fluent::basicinput::ToggleSwitch::toggled, this, [this](bool v) {
        if (!applying_ui_ && live_) live_->hevc_enabled.store(v);
    });
    connect(fps_slider_, &QSlider::valueChanged, this, [this](int v) {
        fps_label_->setText(QString::number(v) + " FPS");
        if (!applying_ui_ && live_) {
            live_->max_fps.store(v);
            live_->refresh_rate.store(v);
        }
    });
    connect(res_seg_, &fluent::navigation::SelectorBar::selectedIndexChanged, this, [this](int i) {
        if (applying_ui_ || !live_) return;
        static const int kW[] = {1920, 2560, 1920, 3840};
        static const int kH[] = {1080, 1440, 1920, 2160};
        if (i >= 0 && i < 4) {
            live_->mirror_width.store(kW[i]);
            live_->mirror_height.store(kH[i]);
        }
    });
    return page;
}

QWidget* MainWindow::build_system_page() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(14, 6, 14, 14);
    v->setSpacing(10);
    v->addWidget(mk_label("SYSTEM / NETWORK", "eyebrow"));
    v->addWidget(mk_label("系统与网络", "pageTitle"));
    v->addWidget(mk_label("接收服务、网络权限和应用诊断。", "pageSub"));

    auto fw_body = new QWidget;
    auto* fv = new QVBoxLayout(fw_body);
    fw_state_ = mk_label("检查中…", "hint");
    fw_msg_ = mk_label("", "hint");
    fw_msg_->setWordWrap(true);
    fw_allow_ = new fluent::basicinput::Button("一键放行");
    fw_allow_->setObjectName("primary");
    fv->addWidget(fw_state_);
    fv->addWidget(fw_msg_);
    fv->addWidget(fw_allow_);
    auto* fw_card = card("防火墙", "检查 Windows 防火墙是否允许接收局域网连接。", fw_body);

    // close behavior
    auto close_body = new QWidget;
    auto* cbv = new QVBoxLayout(close_body);
    cbv->setSpacing(6);
    cbv->addWidget(mk_label("点击窗口关闭按钮时：", "hint"));
    close_combo_ = new fluent::basicinput::ComboBox;
    close_combo_->addItem("每次询问");
    close_combo_->addItem("最小化到托盘，服务继续");
    close_combo_->addItem("直接退出程序");
    close_combo_->setCurrentIndex(std::clamp(close_action_, 0, 2));
    cbv->addWidget(close_combo_);
    auto* close_card = card("关闭行为",
        "选择「每次询问」时也可以在弹窗里勾选不再询问。", close_body);
    v->addWidget(close_card);
    connect(close_combo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        close_action_ = std::clamp(i, 0, 2);
        if (live_) {
            auto prefs = live_->ui_preferences();
            prefs.close_action = close_action_;
            live_->set_ui_preferences(prefs);
        }
    });

    auto diag_body = new QWidget;
    auto* dv = new QVBoxLayout(diag_body);
    diag_switch_ = new fluent::basicinput::ToggleSwitch;
    reset_btn_ = new fluent::basicinput::Button("恢复默认设置");
    reset_btn_->setObjectName("danger");
    auto* diag_row = new QHBoxLayout;
    diag_row->addWidget(mk_label("显示诊断信息"));
    diag_row->addStretch();
    diag_row->addWidget(diag_switch_);
    dv->addLayout(diag_row);
    dv->addWidget(reset_btn_);
    auto* diag_card = card("诊断与维护", "调试信息与设置恢复。", diag_body);

    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    row->addWidget(fw_card, 1);
    row->addWidget(diag_card, 1);
    v->addLayout(row);

    auto net_body = new QWidget;
    auto* nv = new QVBoxLayout(net_body);
    net_info_ = mk_label("", "hint");
    nv->addWidget(net_info_);
    v->addWidget(card("网络信息", {}, net_body));
    v->addStretch();

    connect(fw_allow_, &QPushButton::clicked, this, [this] {
        if (firewall_allow_) firewall_allow_();
    });
    connect(reset_btn_, &QPushButton::clicked, this, [this] {
        if (reset_settings_) reset_settings_();
    });
    connect(diag_switch_, &fluent::basicinput::ToggleSwitch::toggled, this, [this](bool) { /* tick() reads it */ });
    return page;
}

QWidget* MainWindow::build_right_rail() {
    auto* rail = new QWidget;
    rail->setFixedWidth(292);
    auto* v = new QVBoxLayout(rail);
    v->setContentsMargins(14, 6, 14, 14);
    v->setSpacing(10);

    // connection card
    auto conn_body = new QWidget;
    auto* cv = new QVBoxLayout(conn_body);
    cv->setSpacing(6);
    rail_device_ = mk_label("等待 iPhone");
    rail_device_sub_ = mk_label("打开控制中心以连接", "hint");
    cv->addWidget(rail_device_);
    cv->addWidget(rail_device_sub_);
    rail_device_detail_ = mk_label("", "hint");
    rail_device_detail_->setWordWrap(true);
    cv->addWidget(rail_device_detail_);
    v->addWidget(card("连接设备", {}, conn_body));

    // Recording card: status pill + a single action. Moved from the
    // recording page; fullscreen playback was dropped - mirroring has no
    // fullscreen-playback path, so the button only ever navigated.
    auto rec_body = new QWidget;
    auto* qv = new QVBoxLayout(rec_body);
    qv->setSpacing(8);

    auto* status = new QWidget;
    status->setObjectName("settingRow");
    status->setStyleSheet(QString(
        "QWidget#settingRow { background:%1; border-radius:16px; }").arg(ColBg));
    auto* sh = new QHBoxLayout(status);
    sh->setContentsMargins(14, 12, 14, 12);
    sh->setSpacing(10);
    rail_rec_dot_ = new QLabel;
    rail_rec_dot_->setFixedSize(10, 10);
    rail_rec_dot_->setStyleSheet(QString(
        "background:%1;border-radius:5px;").arg(ColMuted));
    sh->addWidget(rail_rec_dot_, 0, Qt::AlignVCenter);

    auto* scol = new QVBoxLayout;
    scol->setSpacing(1);
    rec_state_ = mk_label("空闲", "");
    rec_state_->setStyleSheet(QString(
        "color:%1;font-size:13px;font-weight:600;").arg(ColMuted));
    rec_info_ = mk_label("连接 iPhone 后可录制", "hint");
    scol->addWidget(rec_state_);
    scol->addWidget(rec_info_);
    sh->addLayout(scol, 1);
    qv->addWidget(status);

    rail_record_ = new fluent::basicinput::Button("开始录制");
    rail_record_->setFluentStyle(fluent::basicinput::Button::Accent);
    rail_record_->setFixedHeight(38);
    qv->addWidget(rail_record_);

    v->addWidget(card("录制", {}, rec_body));

    // mini status: stream stats at a glance
    auto stat_body = new QWidget;
    auto* sv = new QVBoxLayout(stat_body);
    sv->setSpacing(4);
    rail_stats_ = mk_label("—", "hint");
    rail_stats_->setAlignment(Qt::AlignLeft);
    sv->addWidget(rail_stats_);
    v->addWidget(card("本次连接", {}, stat_body));
    v->addStretch();

    connect(rail_record_, &QPushButton::clicked, this, [this] {
        if (!hub_.recorder()) return;
        if (hub_.recorder()->status().recording) hub_.recorder()->stop();
        else hub_.recorder()->start(rec_dir_->text().toUtf8().constData(),
                                    current_rec_options());
    });
    return rail;
}

QWidget* MainWindow::build_footer() {
    auto* f = new QFrame;
    f->setObjectName("footer");
    f->setFixedHeight(22);
    auto* h = new QHBoxLayout(f);
    h->setContentsMargins(4, 0, 4, 0);
    footer_left_ = mk_label("服务运行中 · 本地网络 · RTSP :7000", "footerText");
    h->addWidget(footer_left_);
    h->addStretch();
    h->addWidget(mk_label("AirMirror 1.0", "footerText"));
    return f;
}

QFrame* MainWindow::card(const QString& title, const QString& desc, QWidget* body) {
    auto* c = new QFrame;
    c->setObjectName("card");
    auto* v = new QVBoxLayout(c);
    v->setContentsMargins(16, 14, 16, 14);
    v->setSpacing(6);
    v->addWidget(mk_label(title, "cardTitle"));
    if (!desc.isEmpty()) v->addWidget(mk_label(desc, "cardDesc"));
    v->addWidget(body, 1);
    // Every card in the app is built here, so the lift lives in one place:
    // a faint resting shadow (the reference's "floating on paper" look) that
    // deepens on hover.
    new HoverLift(c);
    return c;
}

void MainWindow::set_canvas_assets_dir(const std::wstring& dir) {
    if (canvas_) canvas_->set_assets_dir(dir);
}

void MainWindow::set_firewall_provider(std::function<int()> fn,
                                       std::function<bool()> busy,
                                       std::function<std::string()> msg,
                                       std::function<void()> allow) {
    firewall_state_ = std::move(fn);
    firewall_busy_  = std::move(busy);
    firewall_message_ = std::move(msg);
    firewall_allow_   = std::move(allow);
}

void MainWindow::enter_pure_view() {
    // Same end-state as toggle_pure_view(true), but applied BEFORE show():
    // the native window is created frameless+layered from the first expose,
    // so the DWM blend honors the GL alpha on every pixel. Also skips the
    // chrome flash and the saved-geometry bookkeeping.
    if (!pages_ || !canvas_ || pure_mode_) return;
    pure_mode_ = true;

    top_bar_->setVisible(false);
    footer_->setVisible(false);
    if (sidebar_) sidebar_->setVisible(false);
    if (rail_host_) rail_host_->setVisible(false);
    if (overview_head_)  overview_head_->setVisible(false);
    if (overview_strip_) overview_strip_->setVisible(false);
    if (overview_stage_) {
        overview_stage_->setProperty("class", "stagePure");
        overview_stage_->setStyleSheet(
            "QFrame { background: transparent; border: none; border-radius: 0; }");
        if (auto* sv = qobject_cast<QStackedLayout*>(overview_stage_->layout()))
            sv->setContentsMargins(0, 0, 0, 0);
        if (stage_empty_) stage_empty_->hide();   // nothing to wait for in pure view
    }
    switch_page(Page::Overview);
    if (auto* lay = qobject_cast<QVBoxLayout*>(centralWidget()->layout()))
        lay->setContentsMargins(0, 0, 0, 0);
    if (auto* page = pages_->currentWidget()) {
        if (auto* pv = qobject_cast<QVBoxLayout*>(page->layout()))
            pv->setContentsMargins(0, 0, 0, 0);
    }

    canvas_->set_fill_mode(true);
    canvas_->setAttribute(Qt::WA_AlwaysStackOnTop, true);
    if (centralWidget())
        centralWidget()->setStyleSheet("background: transparent");
    if (root_widget_)
        root_widget_->setStyleSheet("background: transparent");
    setStyleSheet("background: transparent");
    // Translucent is on from construction; only go frameless here.
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setMinimumSize(1, 1);
}

void MainWindow::toggle_pure_view() {
    // 纯画面（隐藏界面）模式 = legacy chromeless：只留视频画布，
    // 窗口吸附手机外壳比例（窗口=手机）。H 或右键菜单退出。
    if (!pages_ || !canvas_) return;

    const bool going_pure = !pure_mode_;
    pure_mode_ = going_pure;

    // chrome on/off — side_host_ HOLDS the canvas, never hide it; hide
    // the sidebar and right rail inside it instead.
    top_bar_->setVisible(!going_pure);
    footer_->setVisible(!going_pure);
    if (sidebar_) sidebar_->setVisible(!going_pure);
    if (rail_host_) rail_host_->setVisible(!going_pure);
    if (overview_head_)  overview_head_->setVisible(!going_pure);
    if (overview_strip_) overview_strip_->setVisible(!going_pure);
    // stage card border/margins disappear so the shell touches the edge
    if (overview_stage_) {
        overview_stage_->setProperty("class", going_pure ? "stagePure" : "");
        overview_stage_->setStyleSheet(going_pure
            ? "QFrame { background: transparent; border: none; border-radius: 0; }"
            : "");
        // The 1px card inset must go too: it leaves a 2px (physical) ring
        // around the canvas that neither GL nor transparency covers — it
        // renders black. Canvas must fill the window edge-to-edge.
        qobject_cast<QStackedLayout*>(overview_stage_->layout())
            ->setContentsMargins(going_pure ? 0 : 1, going_pure ? 0 : 1,
                                 going_pure ? 0 : 1, going_pure ? 0 : 1);
    }
    // pages: stay on Overview; kill the stacked-page margins
    switch_page(Page::Overview);
    if (auto* lay = qobject_cast<QVBoxLayout*>(centralWidget()->layout())) {
        lay->setContentsMargins(going_pure ? 0 : 16, going_pure ? 0 : 14,
                                going_pure ? 0 : 16, going_pure ? 0 : 12);
    }
    // page-internal margins
    if (auto* page = pages_->currentWidget()) {
        if (auto* pv = qobject_cast<QVBoxLayout*>(page->layout()))
            pv->setContentsMargins(0, 0, 0, 0);
    }

    canvas_->set_fill_mode(going_pure);
    // QOpenGLWidget composites its framebuffer OPAQUELY unless
    // WA_AlwaysStackOnTop is set — without it the transparent areas
    // render as black over the desktop.
    canvas_->setAttribute(Qt::WA_AlwaysStackOnTop, going_pure);
    // The root container paints the app background (#F5F5F3); in pure mode
    // it must go transparent or that light gray shows where the GL is
    // transparent (the white corner patches).
    if (centralWidget())
        centralWidget()->setStyleSheet(going_pure ? "background: transparent" : "");
    // #root is the central child that the app QSS paints with
    // `QWidget#root { background: %bg% }` — a child stylesheet only
    // overrides it if it targets the same widget, so set it here too or
    // the opaque app background keeps rendering under the canvas.
    if (root_widget_)
        root_widget_->setStyleSheet(going_pure ? "background: transparent" : "");
    // QMainWindow itself also paints its palette background — transparent
    // there too, or the white shows around the shell's rounded corners.
    setStyleSheet(going_pure ? "background: transparent" : "");
    if (going_pure) {
        saved_geo_ = geometry();
        saved_flags_ = windowFlags();
        // WA_TranslucentBackground is ON from construction — only the
        // frameless flag changes here (flags change hides the window).
        setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
        setMinimumSize(1, 1);   // phone-sized windows are far below chrome min
        show();
        snapped_aspect_ = 0.f;  // force re-snap with the current shell
        snap_window_to_shell();
    } else {
        hide();
        snapped_aspect_ = 0.f;
        setMinimumSize(960, 720);
        setWindowFlags(saved_flags_);
        if (saved_geo_.isValid()) {
            setGeometry(saved_geo_);
            saved_geo_ = QRect();
        }
        clear_window_region();
        pure_scale_ = last_applied_scale_ = 1.0f;
        QApplication::processEvents();
        canvas_->set_fill_mode(false);
        canvas_->setAttribute(Qt::WA_AlwaysStackOnTop, false);
        show();
        update();
        QApplication::processEvents();
        repaint();
    }
}

void MainWindow::apply_rounded_clip() {
    // True silhouette: the region follows the shell outline exactly
    // (rounded corners + side buttons), scaled to the window. Straight
    // edges stay clean because the region only bends where the outline
    // bends.
#ifdef _WIN32
    if (!canvas_) return;
    QRegion rgn = canvas_->shell_region(width(), height());
    if (rgn.isEmpty()) return;
    HRGN hrgn = CreateRectRgn(0, 0, 0, 0);
    for (const QRect& rc : rgn) {
        HRGN part = CreateRectRgn(rc.x(), rc.y(),
                                  rc.x() + rc.width(), rc.y() + rc.height());
        CombineRgn(hrgn, hrgn, part, RGN_OR);
        DeleteObject(part);
    }
    SetWindowRgn(reinterpret_cast<HWND>(winId()), hrgn, TRUE);
#endif
}

void MainWindow::snap_window_to_shell() {
    // Size the window to the VISIBLE body (outline bounding box), not the
    // full PNG — transparent margins around the shell were showing as a
    // white halo. Canvas draws the shell stretched across the window with
    // the same mapping, so image and window line up.
    int bw = 0, bh = 0;
    if (canvas_) canvas_->body_bounds(bw, bh);
    if (bw <= 0 || bh <= 0) return;
    const float ar = float(bw) / float(bh);
    // Size BOTH orientations from one short-edge基准 so rotating feels
    // like the same phone turned sideways, not a different window: short
    // edge = 40% of the smaller available dimension, long edge follows
    // the body aspect (clamped to the screen just in case).
    const QRect avail = QApplication::primaryScreen()->availableGeometry();
    const int short_edge = int(float(std::min(avail.width(), avail.height())) * 0.40f);
    int w, h;
    if (ar < 1.f) { w = short_edge; h = int(short_edge / ar); }   // portrait
    else          { h = short_edge; w = int(short_edge * ar); }   // landscape
    w = std::clamp(w, 100, avail.width() - 40);
    h = std::clamp(h, 200, avail.height() - 40);
    resize(w, h);
    snapped_aspect_ = ar;
    // The auto re-snap (rotation) resizes the base window: the user's
    // wheel-zoom scale is reset with it, or the next wheel tick computes
    // from a stale last_applied_scale_ and jumps the size wildly.
    pure_scale_ = last_applied_scale_ = 1.0f;
    // Rotation can jump the window between phone-sized and screen-wide —
    // re-centre it in the available geometry so it never drifts off-screen
    // or pins itself to the top-left corner.
    move(avail.x() + (avail.width() - width()) / 2,
         avail.y() + (avail.height() - height()) / 2);
    LOG_INFO << "snap: body " << bw << "x" << bh << " ar=" << ar
             << " -> window " << w << "x" << h;
}

void MainWindow::snap_window_to_shell_scaled() {
    // Same body-bounds aspect as snap_window_to_shell: the full-PNG aspect
    // (shell_aspect) includes transparent margins and would drift the
    // window size away from the silhouette on every wheel tick.
    int bw = 0, bh = 0;
    if (canvas_) canvas_->body_bounds(bw, bh);
    const float ar = bw > 0 && bh > 0 ? float(bw) / float(bh) : -1.f;
    if (ar <= 0.f) return;
    int h = int(float(height()) * pure_scale_ / last_applied_scale_);
    int w = int(float(h) * ar);
    const QRect avail = QApplication::primaryScreen()->availableGeometry();
    h = std::clamp(h, 200, avail.height() - 40);
    w = std::clamp(w, 100, avail.width() - 40);
    resize(w, h);
    last_applied_scale_ = pure_scale_;
    // Wheel-zoom keeps the window anchored where the user left it, but
    // clamp it so it never grows past the screen edge.
    const QRect av = QApplication::primaryScreen()->availableGeometry();
    move(std::clamp(x(), av.x(), av.x() + av.width() - width()),
         std::clamp(y(), av.y(), av.y() + av.height() - height()));
}

void MainWindow::apply_window_region() {
    if (!canvas_) return;
    QRegion rgn = canvas_->shell_region(width(), height());
    if (rgn.isEmpty()) { clear_window_region(); return; }
#ifdef _WIN32
    HRGN hrgn = CreateRectRgn(0, 0, 0, 0);
    for (const QRect& rc : rgn) {
        HRGN part = CreateRectRgn(rc.x(), rc.y(),
                                  rc.x() + rc.width(), rc.y() + rc.height());
        CombineRgn(hrgn, hrgn, part, RGN_OR);
        DeleteObject(part);
    }
    SetWindowRgn(reinterpret_cast<HWND>(winId()), hrgn, TRUE);
#endif
}

void MainWindow::clear_window_region() {
#ifdef _WIN32
    SetWindowRgn(reinterpret_cast<HWND>(winId()), nullptr, TRUE);
#endif
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == canvas_ && pure_mode_) {
        if (ev->type() == QEvent::Wheel) {
            auto* we = static_cast<QWheelEvent*>(ev);
            const float step = we->angleDelta().y() > 0 ? 1.08f : 1.f / 1.08f;
            pure_scale_ = std::clamp(pure_scale_ * step, 0.3f, 3.0f);
            snap_window_to_shell_scaled();
            return true;
        }
        if (ev->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent*>(ev)->button() == Qt::LeftButton) {
            drag_from_ = QCursor::pos() - frameGeometry().topLeft();
            dragging_ = true;
            return true;
        }
        if (ev->type() == QEvent::MouseMove && dragging_) {
            move(QCursor::pos() - drag_from_);
            return true;
        }
        if (ev->type() == QEvent::MouseButtonRelease && dragging_) {
            dragging_ = false;
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

ap::video::VideoRecorder::Options MainWindow::current_rec_options() const {
    ap::video::VideoRecorder::Options o;
    // Persisted in LiveSettings UI preferences by save_rec_options();
    // fall back to defaults on first run.
    o.codec = static_cast<ap::video::VideoRecorder::VideoCodec>(rec_codec_->currentIndex());
    o.encoder = static_cast<ap::video::VideoRecorder::EncoderMode>(rec_encoder_->currentIndex());
    o.bitrate_mbps = rec_bitrate_->value();
    return o;
}

void MainWindow::save_rec_options() {
    if (!live_) return;
    auto prefs = live_->ui_preferences();
    prefs.recording_codec = rec_codec_->currentIndex();
    prefs.recording_encoder = rec_encoder_->currentIndex();
    prefs.recording_bitrate = rec_bitrate_->value();
    prefs.recording_directory = rec_dir_->text().toStdString();
    live_->set_ui_preferences(prefs);
}

// Places (and optionally animates) the white selection pill under the row
// that is currently active. Called on row change and whenever the list gets
// resized, since item rects are only meaningful after layout.
void MainWindow::update_nav_pill(bool animate) {
    if (!nav_list_ || !nav_pill_) return;
    const int row = nav_list_->currentRow();
    if (row < 0 || row >= nav_list_->count() ||
        !nav_list_->viewport() || nav_list_->viewport()->height() <= 0) {
        nav_pill_->hide();
        return;
    }
    const QRect r = nav_list_->visualItemRect(nav_list_->item(row));
    if (!r.isValid()) { nav_pill_->hide(); return; }

    const QPoint tl = nav_list_->viewport()->mapTo(
        nav_pill_->parentWidget(), r.topLeft());
    QRect target(tl, r.size());
    target.adjust(2, 1, -2, -1);   // inset so the pill sits inside the row margin

    if (!nav_pill_->isVisible()) {
        nav_pill_->setGeometry(target);
        nav_pill_->show();
        nav_pill_->stackUnder(nav_list_);
        return;
    }
    if (!animate || !motion_enabled()) {
        nav_pill_->setGeometry(target);
        return;
    }
    auto* a = new QPropertyAnimation(nav_pill_, "geometry", this);
    a->setDuration(motion_ms(220));
    a->setStartValue(nav_pill_->geometry());
    a->setEndValue(target);
    a->setEasingCurve(QEasingCurve::OutCubic);
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::switch_page(int index) {
    if (pages_) {
        pages_->setCurrentIndex(index);
        // Cross-fade every page except Overview. That one hosts the
        // QOpenGLWidget canvas, and a QGraphicsEffect on an ancestor of a GL
        // surface makes the surface stop painting entirely.
        if (index != Page::Overview && !pure_mode_) {
            if (auto* w = pages_->widget(index)) fade_in(w, 200);
        }
    }
    // Keep the vertical nav selection in sync when pages switch from
    // elsewhere (rail buttons, pure-mode exit, ...). Block signals so the
    // change doesn't recurse back into switch_page.
    if (nav_list_ && nav_list_->currentRow() != index) {
        const QSignalBlocker block(nav_list_);
        nav_list_->setCurrentRow(index);
    }
    update_nav_pill(true);
    if (rail_host_) rail_host_->setVisible(index == Page::Overview && !pure_mode_);
}

void MainWindow::set_service_running(bool on) {
    service_on_ = on;
    if (status_text_) status_text_->setText(on ? "等待设备连接" : "接收服务已停止");
    if (status_dot_) status_dot_->setStyleSheet(QString("color:%1;font-size:9px;")
        .arg(on ? ColBlue : ColSubtle));
    if (mini_status_) mini_status_->setText("接收服务");   // single-line row: label is static
    if (mini_switch_) {
        const QSignalBlocker b(mini_switch_);
        mini_switch_->setChecked(on);
        // Drive the knob explicitly: setChecked alone left the switch showing
        // the previous state while the service had already toggled.
        mini_switch_->animateToState();
    }
    if (service_btn_) service_btn_->setText(on ? "停止接收" : "启动接收服务");
}

void MainWindow::timerEvent(QTimerEvent* e) {
    if (e->timerId() == tick_timer_id_) {
        // iPhone rotation swaps the shell (portrait <-> landscape): the
        // window must follow, or the outline region and image drift apart.
        // Compare against the BODY aspect (what snap_window_to_shell
        // applies), not the full-PNG aspect which includes transparent
        // margins — otherwise every tick re-snaps and resizes the window.
        if (pure_mode_ && canvas_) {
            int bw = 0, bh = 0;
            canvas_->body_bounds(bw, bh);
            const float ar = bw > 0 && bh > 0 ? float(bw) / float(bh) : -1.f;
            if (ar > 0.f && std::fabs(ar - snapped_aspect_) > 0.01f)
                snap_window_to_shell();
        }
        tick();
    }
    QMainWindow::timerEvent(e);
}

void MainWindow::tick() {
    applying_ui_ = true;
    // --- device / service state -----------------------------------------
    ap::video::StreamHub::DeviceInfo dev;
    const bool connected = hub_.active_device_snapshot(dev);
    const bool on = service_on_;
    if (status_text_) {
        status_text_->setText(!on ? "接收服务已停止"
                              : connected ? "已连接并正在接收"
                                          : "等待设备连接");
    }
    apply_device_state();

    // --- stream snapshot → music + stage text ---------------------------
    const auto snap = hub_.ui_snapshot();
    const uint32_t total = snap.prog_total;
    uint32_t elapsed = snap.prog_elapsed;
    if (total > 0 && snap.playing && snap.progress_pushed_at_ns > 0) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        elapsed += static_cast<uint32_t>((now_ns - snap.progress_pushed_at_ns) / 1'000'000);
        if (elapsed > total) elapsed = total;
    }
    const auto fmt = [](uint32_t ms) {
        char b[16];
        std::snprintf(b, sizeof(b), "%u:%02u", ms / 60000, (ms / 1000) % 60);
        return QString(b);
    };
    if (music_track_) {
        music_track_->setText(snap.track_title.empty() ? "—" :
                              QString::fromStdString(snap.track_title));
        music_artist_->setText(snap.track_artist.empty() ? "等待曲目信息" :
                               QString::fromStdString(snap.track_artist));
        music_album_->setText(QString::fromStdString(snap.track_album));
        music_play_->setText(snap.playing ? "暂停" : "播放");
        music_progress_->setValue(total > 0 ? int(1000.0 * elapsed / total) : 0);
        music_time_->setText(fmt(elapsed) + " / " + fmt(total));
        audio_state_->setText(hub_.audio_command_pending_.load() ? "正在发送指令…"
                              : !hub_.audio_control_available_.load() ? "远程控制不可用"
                              : hub_.audio_command_failed_.load() ? "指令失败或不受支持" : "");
    }
    if (rail_stats_) {
        rail_stats_->setText(QString("%1 × %2 · %3 fps · 已收 %4 帧\n外壳: %5")
            .arg(hub_.video_w()).arg(hub_.video_h())
            .arg(hub_.video_fps(), 0, 'f', 0)
            .arg(hub_.frames_total())
            .arg(canvas_ ? canvas_->shell_name() : QString("—")));
    }

    // --- nav pill geometry ---------------------------------------------------
    // Item rects change on resize and when the sidebar gains its scrollbar;
    // re-place the pill (without animation) whenever the list geometry moves.
    if (nav_pill_ && nav_list_ && nav_list_->geometry() != nav_pill_geom_) {
        nav_pill_geom_ = nav_list_->geometry();
        update_nav_pill(false);
    }

    // --- stage strip -------------------------------------------------------
    if (stage_meta_) {
        stage_meta_->setText(QString("低延迟连接 · %1 × %2 · %3 FPS")
            .arg(hub_.video_w()).arg(hub_.video_h())
            .arg(live_ ? live_->max_fps.load() : 60));
        stage_state_->setText(!on ? "接收已停止"
                              : !connected ? "无活动设备"
                              : snap.has_frame ? "画面实时同步" : "画面待恢复");
        // The idle panel only makes sense while there is genuinely nothing to
        // show; the moment a frame lands it must get out of the way.
        if (stage_empty_) stage_empty_->setVisible(!(on && snap.has_frame));
    }

    // --- recording ----------------------------------------------------------
    const auto rec = hub_.recorder() ? hub_.recorder()->status()
                                     : ap::video::VideoRecorder::Status{};
    const QString rec_err = QString::fromStdString(rec.error);
    if (rec_state_) {
        if (!rec_err.isEmpty()) {
            rec_state_->setText(QString("录制失败：%1").arg(rec_err));
            rec_state_->setStyleSheet(QString(
                "color:%1;font-size:13px;font-weight:600;").arg(ColRed));
        } else if (rec.recording) {
            rec_state_->setText("正在录制");
            rec_state_->setStyleSheet(QString(
                "color:%1;font-size:13px;font-weight:600;").arg(ColInk));
        } else {
            rec_state_->setText("空闲");
            rec_state_->setStyleSheet(QString(
                "color:%1;font-size:13px;font-weight:600;").arg(ColMuted));
        }
    }
    if (rec_info_) {
        if (rec.recording) {
            const qint64 s = rec.elapsed_ms / 1000;
            rec_info_->setText(QString("%1:%2 · %3 帧")
                .arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                .arg(rec.frames));
        } else if (!rec_err.isEmpty()) {
            rec_info_->setText("点「开始录制」重试");
        } else {
            const bool has_feed = hub_.frames_total() > 0 && snap.has_frame;
            rec_info_->setText(has_feed ? "画面就绪，可以开始录制"
                                        : "连接 iPhone 后可录制");
        }
    }
    if (rail_rec_dot_) {
        const bool active = rec.recording && rec_err.isEmpty();
        rail_rec_dot_->setStyleSheet(QString("background:%1;border-radius:5px;")
            .arg(active ? QColor("#E5484D").name() : QColor("#D9D9D6").name()));
    }
    if (rail_record_) {
        rail_record_->setText(rec.recording ? "停止录制" : "开始录制");
        rail_record_->setFluentStyle(rec.recording
            ? fluent::basicinput::Button::Standard
            : fluent::basicinput::Button::Accent);
        const bool has_feed = hub_.frames_total() > 0 && snap.has_frame;
        rail_record_->setEnabled(rec.recording || has_feed);
        rail_record_->setToolTip(rec.recording || has_feed
                                     ? QString()
                                     : QStringLiteral("请先连接 iPhone 并开始镜像"));
    }

    // --- settings page from LiveSettings ------------------------------------
    if (live_) {
        const QSignalBlocker g(gpu_switch_), vs(vsync_switch_), hv(hevc_switch_), fs(fps_slider_);
        gpu_switch_->setIsOn(live_->mirror_hwaccel.load());
        vsync_switch_->setIsOn(live_->vsync_enabled.load());
        hevc_switch_->setIsOn(live_->hevc_enabled.load());
        fps_slider_->setValue(live_->max_fps.load());
        fps_label_->setText(QString::number(live_->max_fps.load()) + " FPS");
        if (bezel_switch_) {
            const QSignalBlocker bz(bezel_switch_);
            bezel_switch_->setIsOn(live_->frame_style.load() != -1);
        }
        rotation_combo_->setCurrentIndex(live_->video_rotation.load() + 1);
        if (res_seg_) {
            const QSignalBlocker rs(res_seg_);
            const int mw = live_->mirror_width.load(), mh = live_->mirror_height.load();
            if (mw == 1920 && mh == 1080) res_seg_->setSelectedIndex(0);
            else if (mw == 2560 && mh == 1440) res_seg_->setSelectedIndex(1);
            else if (mw == 1920 && mh == 1920) res_seg_->setSelectedIndex(2);
            else if (mw == 3840 && mh == 2160) res_seg_->setSelectedIndex(3);
        }
    }

    // --- firewall -------------------------------------------------------------
    if (firewall_state_) {
        const int st = firewall_state_();
        const bool need = st == 2 || st == 4;
        fw_state_->setText(st == 1 ? "已放行局域网连接。"
                          : st == 3 ? "防火墙由管理员策略管理。"
                          : firewall_busy_() ? "正在请求授权…"
                          : "尚未放行，可能导致设备搜不到或连不上。");
        fw_msg_->setText(QString::fromStdString(firewall_message_()));
        fw_allow_->setEnabled(need && !firewall_busy_());
        fw_allow_->setText(need ? "一键放行" : "已放行");
    }

    // --- footer ----------------------------------------------------------------
    if (footer_left_) {
        if (diag_switch_ && diag_switch_->isOn()) {
            footer_left_->setText(QString("%1 fps · %2 × %3 · %4 帧 · %5 Mbps")
                .arg(hub_.video_fps(), 0, 'f', 0)
                .arg(hub_.video_w()).arg(hub_.video_h())
                .arg(hub_.frames_total())
                .arg(hub_.video_live() ? hub_.payload_mbps() : 0.0f, 0, 'f', 1));
        } else {
            footer_left_->setText(QString("%1 · 本地网络 · RTSP :7000")
                .arg(on ? "服务运行中" : "服务已停止"));
        }
    }

    applying_ui_ = false;
}

void MainWindow::apply_device_state() {
    ap::video::StreamHub::DeviceInfo dev;
    const bool connected = hub_.active_device_snapshot(dev);
    if (rail_device_) {
        rail_device_->setText(connected ? QString::fromStdString(dev.name)
                                        : "等待 iPhone");
        rail_device_sub_->setText(connected ? "AirPlay · 本地网络"
                                            : "打开控制中心以连接");
        if (rail_device_detail_) {
            rail_device_detail_->setText(connected
                ? QString("型号: %1\nIP: %2\n会话: %3")
                    .arg(QString::fromStdString(dev.model),
                         QString::fromStdString(dev.peer_ip),
                         QString::fromStdString(dev.kind))
                : QString());
        }
    }
}

} // namespace ap::ui
