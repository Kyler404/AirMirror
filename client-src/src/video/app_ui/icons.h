#pragma once

// app_ui/icons.h — hand-drawn linear icon set (24x24 grid, 1.7px strokes).
// Mirror of the airmirror prototype SVG symbols. One style, no fills
// except tiny accents; pages must not hand-draw their own glyphs.

#include "imgui.h"
#include <cmath>

namespace ap::video::app_ui {

enum class Icon {
    Airplay, Screen, Music, Record, Settings, Grid, Wifi, Volume,
    Expand, Rotate, Plus, Minus, Check, Chevron, Play, Pause, Next, Prev,
    Folder, Sliders, Shield, Power, More, Info, X, Phone
};

inline void DrawIcon(ImDrawList* d, Icon kind, ImVec2 p, float size, ImU32 color) {
    auto v = [&](float x, float y) { return ImVec2(p.x + x * size / 24, p.y + y * size / 24); };
    auto s = [&](float x, float y, float xx, float yy) { d->AddLine(v(x, y), v(xx, yy), color, 1.7f); };
    auto c = [&](float cx, float cy, float r) { d->AddCircle(v(cx, cy), r * size / 24, color, 24, 1.7f); };
    auto r_ = [&](float x, float y, float w, float h, float rad) {
        d->AddRect(v(x, y), v(x + w, y + h), color, rad, 0, 1.7f);
    };
    switch (kind) {
    case Icon::Airplay:  // screen with airplay triangle below
        r_(3, 3, 18, 12, 2);
        s(5, 18, 3.5, 18); s(20.5, 18, 19, 18);
        s(8, 22, 12, 17); s(12, 17, 16, 22);
        s(8, 22, 16, 22);
        break;
    case Icon::Screen:   // monitor
        r_(3, 3, 18, 14, 2); s(8, 21, 16, 21); s(12, 17, 12, 21);
        break;
    case Icon::Music:    // two-note
        s(9, 18, 9, 5); s(9, 5, 21, 3); s(21, 3, 21, 16);
        c(6, 18, 3); c(18, 16, 3);
        break;
    case Icon::Record:   // double circle
        c(12, 12, 9); c(12, 12, 4);
        break;
    case Icon::Settings: // gear: circle + 8 teeth
        c(12, 12, 4);
        for (int i = 0; i < 8; ++i) {
            const float a = i * 45.0f * 3.14159265f / 180.0f;
            const float x1 = 12 + 6.2f * std::cos(a), y1 = 12 + 6.2f * std::sin(a);
            const float x2 = 12 + 9.6f * std::cos(a), y2 = 12 + 9.6f * std::sin(a);
            s(x1, y1, x2, y2);
        }
        break;
    case Icon::Grid:     // four rounded squares
        r_(3, 3, 7, 7, 1.5f); r_(14, 3, 7, 7, 1.5f);
        r_(3, 14, 7, 7, 1.5f); r_(14, 14, 7, 7, 1.5f);
        break;
    case Icon::Wifi:
        // three arcs + dot
        d->PathArcTo(v(12, 20), 9.5f * size / 24, 4.05f, 5.38f, 12); d->PathStroke(color, 0, 1.7f);
        d->PathArcTo(v(12, 20), 5.8f * size / 24, 4.2f, 5.23f, 10); d->PathStroke(color, 0, 1.7f);
        d->AddCircleFilled(v(12, 19), 0.8f * size / 24, color);
        break;
    case Icon::Volume:
        s(11, 5, 6, 9); s(6, 9, 3, 9); s(3, 9, 3, 15); s(3, 15, 6, 15); s(6, 15, 11, 19);
        d->PathArcTo(v(11, 12), 6.5f * size / 24, -0.9f, 0.9f, 10); d->PathStroke(color, 0, 1.7f);
        d->PathArcTo(v(11, 12), 10.5f * size / 24, -0.9f, 0.9f, 12); d->PathStroke(color, 0, 1.7f);
        break;
    case Icon::Expand:   // four corners
        s(8, 3, 5, 3); s(5, 3, 5, 8);
        s(16, 3, 19, 3); s(19, 3, 19, 8);
        s(5, 16, 5, 21); s(5, 21, 8, 21);
        s(19, 16, 19, 21); s(19, 21, 16, 21);
        break;
    case Icon::Rotate:
        s(20, 7, 20, 12); s(20, 12, 15, 12);
        s(4, 17, 4, 12); s(4, 12, 9, 12);
        d->PathArcTo(v(12, 12), 7.5f * size / 24, -1.2f, 0.6f, 12); d->PathStroke(color, 0, 1.7f);
        d->PathArcTo(v(12, 12), 7.5f * size / 24, 1.94f, 3.74f, 12); d->PathStroke(color, 0, 1.7f);
        break;
    case Icon::Plus: s(12, 5, 12, 19); s(5, 12, 19, 12); break;
    case Icon::Minus: s(5, 12, 19, 12); break;
    case Icon::Check: s(5, 12.5f, 9.5, 17); s(9.5, 17, 19, 6); break;
    case Icon::Chevron: s(9, 18, 15, 12); s(15, 12, 9, 6); break;
    case Icon::Play: d->AddTriangleFilled(v(8, 5), v(20, 12), v(8, 19), color); break;
    case Icon::Pause: d->AddRectFilled(v(8, 5), v(11, 19), color, 1); d->AddRectFilled(v(15, 5), v(18, 19), color, 1); break;
    case Icon::Next: d->AddTriangleFilled(v(5, 5), v(15, 12), v(5, 19), color); s(19, 5, 19, 19); break;
    case Icon::Prev: d->AddTriangleFilled(v(19, 5), v(9, 12), v(19, 19), color); s(5, 5, 5, 19); break;
    case Icon::Folder:
        s(3, 7, 5, 5); s(5, 5, 9, 5); s(9, 5, 11, 7); s(11, 7, 19, 7);
        s(19, 7, 21, 9); s(21, 9, 21, 18); s(21, 18, 19, 20); s(19, 20, 5, 20);
        s(5, 20, 3, 18); s(3, 18, 3, 7);
        break;
    case Icon::Sliders:
        s(4, 21, 4, 14); s(4, 10, 4, 3); s(12, 21, 12, 12); s(12, 8, 12, 3);
        s(20, 21, 20, 16); s(20, 12, 20, 3);
        s(1, 14, 7, 14); s(9, 10, 15, 10); s(17, 16, 23, 16);
        break;
    case Icon::Shield:
        s(12, 22, 4, 18.5f); s(4, 18.5f, 4, 5); s(4, 5, 12, 2); s(12, 2, 20, 5);
        s(20, 5, 20, 18.5f); s(20, 18.5f, 12, 22);
        s(9, 12, 11, 14); s(11, 14, 15, 10);
        break;
    case Icon::Power:
        s(12, 2, 12, 12);
        d->PathArcTo(v(12, 13), 8.2f * size / 24, 2.62f, 6.81f, 20); d->PathStroke(color, 0, 1.7f);
        break;
    case Icon::More:
        d->AddCircleFilled(v(5, 12), 1.1f * size / 24, color);
        d->AddCircleFilled(v(12, 12), 1.1f * size / 24, color);
        d->AddCircleFilled(v(19, 12), 1.1f * size / 24, color);
        break;
    case Icon::Info:
        c(12, 12, 10);
        s(12, 11, 12, 16); d->AddCircleFilled(v(12, 8), 0.9f * size / 24, color);
        break;
    case Icon::X: s(18, 6, 6, 18); s(6, 6, 18, 18); break;
    case Icon::Phone:   // smartphone (brand)
        r_(6, 2.5f, 12, 19, 3); s(10, 5, 14, 5); s(10, 18.5f, 14, 18.5f);
        c(12, 12, 2.2f);
        break;
    }
}

} // namespace ap::video::app_ui
