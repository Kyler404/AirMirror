#pragma once

// controls.h — small custom widget library for the Qt front end.
// ToggleSwitch and SegmentedControl replace checkbox-as-switch and
// combo-for-two-options, which read as cheap and crowd the pages.

#include <QAbstractButton>
#include <QWidget>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QPainter>
#include <QEnterEvent>
#include <QMouseEvent>
#include <algorithm>
#include <vector>
#include <string>

namespace ap::ui {

// iOS-style sliding switch with an animated knob.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
    // QPropertyAnimation drives this by name; without the property it silently
    // animates nothing and the knob stays where it was, so the switch showed
    // the old state even though the toggle had already taken effect.
    Q_PROPERTY(qreal knobPos READ knobPos WRITE setKnobPos)
public:
    explicit ToggleSwitch(QWidget* parent = nullptr) : QAbstractButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(44, 26);
    }
    QSize sizeHint() const override { return {44, 26}; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal t = pos_;                       // 0..1 knob position
        // Track: grey → green; subtle inner shadow for depth.
        QColor track_off("#DCDCDC"), track_on("#0A7CFF");
        QColor track = lerp(track_off, track_on, t);
        p.setPen(Qt::NoPen);
        p.setBrush(track);
        p.drawRoundedRect(rect(), 13, 13);
        // Track inner highlight (top edge) for a 3D inset feel.
        if (isChecked()) {
            QLinearGradient g(0, 0, 0, height());
            g.setColorAt(0, QColor(255, 255, 255, 28));
            g.setColorAt(1, QColor(0, 0, 0, 14));
            p.setBrush(g);
            p.drawRoundedRect(rect(), 13, 13);
        }
        // Knob: white circle with a soft drop shadow.
        const qreal kw = 20, kh = 20;
        const qreal x = 3 + t * (width() - kw - 6);
        const qreal y = (height() - kh) / 2;
        p.setBrush(QColor(0, 0, 0, 30));
        p.drawEllipse(QRectF(x, y + 1.5, kw, kh));
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(x, y, kw, kh));
    }

    void enterEvent(QEnterEvent*) override { start_anim(); }
    void leaveEvent(QEvent*) override { start_anim(); }

    // Programmatic setChecked() must move the knob too — without this the
    // track recolours but the knob stays put (state desync on startup and
    // on every external state sync).
    void checkStateSet() override {
        if (!isVisible()) { setKnobPos(isChecked() ? 1.0 : 0.0); return; }
        start_anim();
    }
public:
    // Explicit entry point for external state sync. QAbstractButton's
    // checkStateSet() hook is not reached on every setChecked() path — the
    // button ended up with checked=false while the knob still sat at 1.0, so
    // the service toggled but the switch visibly did not. Callers drive the
    // visual from here instead of trusting the hook.
    void animateToState() {
        if (isVisible()) start_anim();
        else setKnobPos(isChecked() ? 1.0 : 0.0);
    }

private slots:
    void start_anim() {
        if (!anim_) {
            // QVariantAnimation + an explicit write instead of
            // QPropertyAnimation("knobPos"): the property route depends on the
            // meta-object, and when it fails it fails silently — the switch
            // kept showing the old state while the service had already
            // toggled. Driving the setter directly cannot desync.
            anim_ = new QVariantAnimation(this);
            anim_->setDuration(140);
            anim_->setEasingCurve(QEasingCurve::OutCubic);
            QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                             [this](const QVariant& v) { setKnobPos(v.toReal()); });
        }
        anim_->stop();
        anim_->setStartValue(pos_);
        anim_->setEndValue(isChecked() ? 1.0 : 0.0);
        anim_->start();
    }

public:
    // expose the animated position as a property for QPropertyAnimation
    void setKnobPos(qreal v) { pos_ = v; update(); }
    qreal knobPos() const { return pos_; }

private:
    static QColor lerp(QColor a, QColor b, qreal t) {
        return QColor(a.red()   + int((b.red()   - a.red())   * t),
                      a.green() + int((b.green() - a.green()) * t),
                      a.blue()  + int((b.blue()  - a.blue())  * t));
    }
    QVariantAnimation* anim_ = nullptr;
    qreal pos_ = 0.0;
};

// Segmented control (iOS style): n mutually-exclusive pills in a track.
// The selection pill slides between segments — a dropdown was used for the
// same job before, but a ComboBox that paints as a bare underline reads much
// cheaper than the rest of the surfaces.
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedControl(QWidget* parent = nullptr) : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(30);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        anim_ = new QVariantAnimation(this);
        anim_->setDuration(180);
        anim_->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant& v) { pos_ = v.toReal(); update(); });
    }

    void addItem(const QString& text) {
        items_.push_back(text);
        setMinimumWidth(int(items_.size()) * 52);
        if (items_.size() == 1) current_ = 0;
    }
    int currentIndex() const { return current_; }

signals:
    void currentChanged(int index);

public slots:
    void setCurrentIndex(int i) {
        if (i < 0 || i >= int(items_.size()) || i == current_) return;
        current_ = i;
        if (isVisible()) {
            anim_->stop();
            anim_->setStartValue(pos_);
            anim_->setEndValue(qreal(i));
            anim_->start();
        } else {
            pos_ = qreal(i);
        }
        emit currentChanged(i);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Track
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#E6E6E6"));
        p.drawRoundedRect(rect(), 9, 9);
        if (items_.empty()) return;
        const qreal seg_w = qreal(width()) / items_.size();
        // Selected segment: white pill + soft shadow, positioned by the
        // animation's current value rather than the raw index.
        QRectF sel(pos_ * seg_w + 2, 2, seg_w - 4, height() - 4);
        p.setPen(QColor(0, 0, 0, 12));
        p.setBrush(QColor(0, 0, 0, 14));
        p.drawRoundedRect(sel.translated(0, 1), 7, 7);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawRoundedRect(sel, 7, 7);
        // Labels
        for (int i = 0; i < int(items_.size()); ++i) {
            QFont f = p.font();
            f.setBold(i == current_);
            f.setPixelSize(12);
            p.setFont(f);
            p.setPen(i == current_ ? QColor("#0A0A0A") : QColor("#8C8C8C"));
            const QString text = p.fontMetrics().elidedText(
                items_[i], Qt::ElideRight, int(seg_w) - 12);
            p.drawText(QRectF(i * seg_w, 0, seg_w, height()),
                       Qt::AlignCenter, text);
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (!items_.empty()) {
            const int idx = int(e->position().x() * items_.size() / width());
            setCurrentIndex(std::clamp(idx, 0, int(items_.size()) - 1));
        }
    }

private:
    std::vector<QString> items_;
    int current_ = -1;
    qreal pos_ = 0.0;
    QVariantAnimation* anim_ = nullptr;
};

} // namespace ap::ui
