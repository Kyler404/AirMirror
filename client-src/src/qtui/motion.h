#pragma once

// motion.h — shared motion primitives for the Qt front end.
//
// Everything here defers to fluent::MotionPolicy, the library's app-wide
// motion policy: when the user runs with "reduce motion" the animations
// collapse to instant state changes instead of being silently skipped, so the
// UI stays consistent and accessible.
//
// Deliberately header-only and free of Q_OBJECT: these are plain QObject
// subclasses using functor connects, so no extra moc translation unit or
// CMake source entry is needed.

#include <QObject>
#include <QWidget>
#include <QEvent>
#include <QColor>
#include <QPointF>
#include <QVariantAnimation>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>

#include <components/foundation/MotionPolicy.h>

namespace ap::ui {

// ---- policy shims ---------------------------------------------------------

inline bool motion_enabled() {
    return fluent::MotionPolicy::instance().shouldAnimate(true);
}

// Returns 0 when motion is suppressed, so callers can pass the result straight
// into setDuration() without branching.
inline int motion_ms(int fullMs) {
    return fluent::MotionPolicy::instance().resolvedDuration(fullMs, true);
}

// ---- card hover lift ------------------------------------------------------

// A card that carries a faint shadow at rest and grows it under the pointer.
// The resting shadow is what makes the surfaces read as raised the moment the
// window opens; a hover-only shadow leaves the idle screen completely flat.
class HoverLift : public QObject {
public:
    HoverLift(QWidget* host,
              qreal idleBlur = 18.0, qreal idleDy = 3.0, int idleAlpha = 46,
              qreal hotBlur = 34.0, qreal hotDy = 9.0, int hotAlpha = 78)
        : QObject(host),
          host_(host),
          idleBlur_(idleBlur), idleDy_(idleDy), idleAlpha_(idleAlpha),
          hotBlur_(hotBlur), hotDy_(hotDy), hotAlpha_(hotAlpha) {
        eff_ = new QGraphicsDropShadowEffect(host);
        host->setGraphicsEffect(eff_);
        apply(0.0);   // resting state
        // WA_Hover is essential: with plain Enter/Leave the host reports Leave
        // as soon as the pointer crosses onto one of its child labels, so the
        // lift flickers off the moment the cursor lands anywhere meaningful.
        // HoverEnter/HoverLeave are bound to the widget's own rect instead.
        host->setAttribute(Qt::WA_Hover, true);
        host->installEventFilter(this);
    }

    // Lets the scene compose two effects on one widget (a widget may only own
    // a single QGraphicsEffect, so callers use this to add the opacity blend).
    QGraphicsDropShadowEffect* shadow() const { return eff_; }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj != host_) return QObject::eventFilter(obj, ev);
        switch (ev->type()) {
        case QEvent::HoverEnter:
        case QEvent::Enter:
            run(true);
            break;
        case QEvent::HoverLeave:
        case QEvent::Leave:
            run(false);
            break;
        default:
            break;
        }
        return QObject::eventFilter(obj, ev);
    }

private:
    void run(bool on) {
        if (!anim_) {
            anim_ = new QVariantAnimation(this);
            anim_->setEasingCurve(QEasingCurve::OutCubic);
            // Qualified: HoverLift has no Q_OBJECT macro, so unqualified
            // lookup does not reach QObject's template connect here.
            QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                             [this](const QVariant& v) { apply(v.toReal()); });
        }
        anim_->stop();
        anim_->setDuration(motion_ms(190));
        anim_->setStartValue(t_);
        anim_->setEndValue(on ? 1.0 : 0.0);
        anim_->start();
    }

    void apply(qreal t) {
        t_ = t;
        eff_->setBlurRadius(idleBlur_ + (hotBlur_ - idleBlur_) * t);
        eff_->setOffset(0.0, idleDy_ + (hotDy_ - idleDy_) * t);
        eff_->setColor(QColor(24, 24, 20,
                              int(idleAlpha_ + (hotAlpha_ - idleAlpha_) * t)));
    }

    QWidget* host_ = nullptr;
    QGraphicsDropShadowEffect* eff_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    qreal idleBlur_, idleDy_;
    int idleAlpha_;
    qreal hotBlur_, hotDy_;
    int hotAlpha_;
    qreal t_ = 0.0;
};

// ---- page transition ------------------------------------------------------

// Cross-fades a page in. Only ever used on pages that do NOT host the video
// canvas: applying a QGraphicsEffect to an ancestor of a QOpenGLWidget breaks
// its composition (the GL surface stops being painted).
inline void fade_in(QWidget* page, int ms = 220) {
    if (!page) return;
    const int dur = motion_ms(ms);
    if (dur <= 0) return;

    auto* eff = new QGraphicsOpacityEffect(page);
    eff->setOpacity(0.0);
    page->setGraphicsEffect(eff);

    auto* anim = new QPropertyAnimation(eff, "opacity", page);
    anim->setDuration(dur);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // Drop the effect once settled: a permanent opacity effect keeps the whole
    // page on the offscreen path for the rest of the session.
    QObject::connect(anim, &QPropertyAnimation::finished, page, [page] {
        page->setGraphicsEffect(nullptr);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

}  // namespace ap::ui
