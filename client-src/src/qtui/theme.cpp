#include "qtui/theme.h"

namespace ap::ui {

QString app_style_sheet() {
    return QString(R"(

* {
    font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
    font-size: 13px;
    color: %ink%;
    selection-background-color: %blue%;
    selection-color: white;
}

QWidget#root {
    background: %bg%;
}

/* ---------- sidebar nav ----------
   Selected row is a white pill on the grey sidebar, exactly like the file
   list in the reference: no border, no gradient — the value jump does the
   work. */
QListWidget#nav {
    background: transparent;
    border: none;
    outline: none;
    padding: 4px 0;
}
QListWidget#nav::item {
    color: %muted%;
    padding: 9px 14px;
    margin: 2px 8px;
    border-radius: 14px;
}
QListWidget#nav::item:hover {
    background: %panelDeep%;
    color: %ink%;
}
QListWidget#nav::item:selected {
    /* The white pill is drawn by a sibling widget underneath the list; the
       item itself only tints the text so the pill shows through. */
    background: transparent;
    color: %ink%;
}
QLabel.navGroup {
    color: %subtle%;
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 1.2px;
    padding: 10px 20px 2px 20px;
}

/* ---------- containers (cards, bars) ----------
   No outlines anywhere: depth comes from value steps, as in the reference.
   Radius is large (24) to match the softened, pill-heavy language. */
QFrame#card {
    background: %canvas%;
    border: none;
    border-radius: 24px;
}
QFrame#card:hover {
    background: #FDFDFB;
}
QFrame#stageCard {
    background: %bg%;
    border: none;
    border-radius: 24px;
}
QFrame#topBar {
    background: %canvas%;
    border: none;
    border-radius: 20px;
}
QLabel.cardTitle {
    font-size: 14px;
    font-weight: 600;
    color: %ink%;
}
QLabel.cardDesc, QLabel.hint {
    font-size: 12px;
    color: %muted%;
}
QLabel.eyebrow {
    color: %subtle%;
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 1.2px;
}
QLabel.pageTitle {
    font-size: 26px;
    font-weight: 600;
}
QLabel.pageSub {
    font-size: 13px;
    color: %muted%;
}
QLabel.statusChipGreen {
    background: %greenSoft%;
    color: #2E7653;
    border-radius: 9px;
    padding: 2px 10px;
    font-size: 11px;
}

/* Sidebar bottom service row: quiet label, not a nav item. */
QLabel.svcLabel {
    font-size: 12px;
    color: %muted%;
    padding-left: 2px;
}

/* ---------- buttons ----------
   Only used where a real QPushButton survives; every other control is a
   FluentQt widget painted from ThemeRegistry. Pills, never rounded rects. */
QPushButton {
    background: %canvas%;
    border: none;
    border-radius: 16px;
    padding: 7px 18px;
}
QPushButton:hover { background: #F4F4F4; }
QPushButton:pressed { background: %panelDeep%; }
QPushButton:disabled { color: %subtle%; background: %panelDeep%; }

QPushButton#primary {
    background: %ink%;
    color: white;
}
QPushButton#primary:hover { background: #242424; }
QPushButton#primary:pressed { background: #000000; }

QPushButton#danger {
    background: %redSoft%;
    color: %danger%;
}
QPushButton#danger:hover { background: #FDEDEC; }

QPushButton#recordBtn {
    background: %accentSoft%;
    color: %accent%;
}
QPushButton#recordBtn:hover { background: #FBE7D8; }
QPushButton#recordBtn[recording="true"] {
    background: %red%;
    color: white;
}

QToolButton#iconBtn {
    background: transparent;
    border: none;
    border-radius: 14px;
    padding: 5px;
}
QToolButton#iconBtn:hover { background: %panelDeep%; }

/* ---------- inputs ----------
   Intentionally empty. Every input in the app is a FluentQt control
   (ComboBox / Slider / LineEdit / ToggleSwitch), and those paint themselves
   from ThemeRegistry colours. Styling them here fought the library's own
   painting — the global QComboBox rule is what flattened the dropdown to a
   bare underline. Accent colour is set once in MainWindow's constructor. */

/* ---------- scrollbars ---------- */
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical {
    background: transparent; width: 10px; margin: 4px 2px;
}
QScrollBar::handle:vertical {
    background: #DCDCDC; border-radius: 5px; min-height: 32px;
}
QScrollBar::handle:vertical:hover { background: #C4C4C4; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

/* ---------- footer ---------- */
QFrame#footer {
    background: transparent;
    border: none;
}
QLabel.footerText { font-size: 11px; color: %subtle%; }

/* ---------- top bar labels ---------- */
QLabel.brandMark {
    background: transparent;
    border: none;
}
QLabel.brandName { font-size: 14px; font-weight: 600; }
QLabel.brandSub  { font-size: 10px; color: %subtle%; }
QLabel.statusText { font-size: 12px; color: %muted%; }

)")
    .replace("%ink%",    ColInk)
    .replace("%bg%",     ColBg)
    .replace("%canvas%", ColCanvas)
    .replace("%surface%",ColSurface)
    .replace("%panelDeep%", ColPanelDeep)
    .replace("%panel%",  ColPanel)
    .replace("%muted%",  ColMuted)
    .replace("%subtle%", ColSubtle)
    .replace("%border%", ColBorder)
    .replace("%blue%",   ColBlue)
    .replace("%green%",  ColGreen)
    .replace("%greenSoft%", ColGreenSoft)
    .replace("%accent%", ColAccent)
    .replace("%accentSoft%", ColAccentSoft)
    .replace("%red%",    ColRed)
    .replace("%redSoft%",ColRedSoft)
    .replace("%danger%", ColDanger);
}

} // namespace ap::ui
