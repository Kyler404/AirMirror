#pragma once

// app_ui/widgets.h — controls matching the airmirror prototype:
// pill buttons, quiet nav rows, tiny switches, cards, icon buttons.

#include "theme.h"
#include "icons.h"

#include <cstring>

namespace ap::video::app_ui {

// ImGui hides everything from "##" onward when IT draws a label; our
// custom draw-list rendering must do the same or IDs leak onto buttons.
inline const char* LabelEnd(const char* label) {
    const char* h = std::strstr(label, "##");
    return h ? h : label + std::strlen(label);
}

inline bool ItemHovered() { return ImGui::IsItemHovered(); }
inline bool ItemFocused() { return ImGui::IsItemFocused(); }
inline bool ItemDisabled() { return ImGui::GetStyle().Alpha < 0.9f; }

inline void CenterLabel(ImDrawList* d, ImVec2 a, ImVec2 b, const char* label, ImU32 color) {
    const char* le = LabelEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, le);
    d->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
               ImVec2(a.x + (b.x - a.x - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f),
               color, label, le);
}

inline void FocusRing(ImDrawList* d, ImVec2 a, ImVec2 b, float rounding) {
    if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)
        d->AddRect(a, b, Blue, rounding, 0, 1.4f);
}

// Soft card shadow (few low-alpha layers; no texture pass needed).
inline void CardShadow(ImDrawList* d, ImVec2 a, ImVec2 b, float rounding) {
    for (int i = 3; i >= 1; --i) {
        const float pad = 5.0f * (static_cast<float>(i) / 3.0f);
        d->AddRectFilled(ImVec2(a.x - pad, a.y - pad + 2),
                         ImVec2(b.x + pad, b.y + pad + 3),
                         IM_COL32(24, 25, 22, 2 + i), rounding + pad);
    }
}

// ---- primitives -----------------------------------------------------------------

// Invisible interactive rect; callers paint on top.
inline bool Hit(const char* id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    const bool pressed = ImGui::Button(id, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return pressed;
}

inline ImVec2 ResolveSize(const char* label, ImVec2 size) {
    const ImVec2 ts = ImGui::CalcTextSize(label, LabelEnd(label));
    if (size.x < 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.x <= 0) size.x = ts.x + Sp4 * 2;
    if (size.y <= 0) size.y = ts.y + Sp2 + 4;
    return size;
}

// ---- buttons ---------------------------------------------------------------------

// .btn — white pill with 1px border. cls: 0 normal, 1 primary(dark), 2 danger.
inline bool Button(const char* label, ImVec2 size = ImVec2(0, 0), int variant = 0) {
    const ImVec2 sz = ResolveSize(label, size);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, sz);
    const ImVec2 b(p.x + sz.x, p.y + sz.y);
    auto* d = ImGui::GetWindowDrawList();
    const bool disabled = ItemDisabled();
    float& a = Anim(ImGui::GetItemID());
    a = Ease(a, ItemHovered() ? 1.0f : 0.0f, 18);
    ImU32 fill, text, edge;
    if (variant == 1)      { fill = Ink; text = White; edge = Ink; }
    else if (variant == 2) { fill = Mix(RedSoft, Surface, a); text = Danger; edge = Mix(IM_COL32(243,221,218,255), Panel, a); }
    else                   { fill = Mix(Canvas, Surface, a); text = InkSoft; edge = Mix(Border, Panel, a); }
    if (disabled) { fill = Panel; text = Disabled; edge = Panel; }
    d->AddRectFilled(p, b, fill, RControl);
    if (variant != 1) d->AddRect(p, b, edge, RControl, 0, 1.0f);
    FocusRing(d, p, b, RControl);
    CenterLabel(d, p, b, label, text);
    return pressed;
}

// .btn.primary — dark filled button.
inline bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    const ImVec2 sz = ResolveSize(label, size);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, sz);
    const ImVec2 b(p.x + sz.x, p.y + sz.y);
    auto* d = ImGui::GetWindowDrawList();
    const bool disabled = ItemDisabled();
    float& a = Anim(ImGui::GetItemID());
    a = Ease(a, ItemHovered() ? 1.0f : 0.0f, 18);
    d->AddRectFilled(p, b, disabled ? Disabled : Mix(Ink, IM_COL32(56, 57, 52, 255), a), RControl);
    FocusRing(d, p, b, RControl);
    CenterLabel(d, p, b, label, White);
    return pressed;
}

// .btn.accent — soft warm orange chip.
inline bool AccentButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    const ImVec2 sz = ResolveSize(label, size);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, sz);
    const ImVec2 b(p.x + sz.x, p.y + sz.y);
    auto* d = ImGui::GetWindowDrawList();
    float& a = Anim(ImGui::GetItemID());
    a = Ease(a, ItemHovered() ? 1.0f : 0.0f, 18);
    d->AddRectFilled(p, b, Mix(AccentSoft, Panel, a), RControl);
    d->AddRect(p, b, Mix(IM_COL32(240, 221, 212, 255), Panel, a), RControl, 0, 1.0f);
    FocusRing(d, p, b, RControl);
    CenterLabel(d, p, b, label, IM_COL32(182, 92, 61, 255));
    return pressed;
}

// .record-button — soft red pill; active = solid red.
inline bool RecordButton(const char* label, ImVec2 size = ImVec2(0, 0), bool active = false) {
    const ImVec2 sz = ResolveSize(label, size);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, sz);
    const ImVec2 b(p.x + sz.x, p.y + sz.y);
    auto* d = ImGui::GetWindowDrawList();
    float& a = Anim(ImGui::GetItemID());
    a = Ease(a, ItemHovered() ? 1.0f : 0.0f, 18);
    if (active) d->AddRectFilled(p, b, Mix(Red, IM_COL32(198, 70, 66, 255), a), RControl);
    else {
        d->AddRectFilled(p, b, Mix(IM_COL32(253, 245, 243, 255), Surface, a), RControl);
        d->AddRect(p, b, Mix(IM_COL32(237, 216, 211, 255), Panel, a), RControl, 0, 1.0f);
    }
    FocusRing(d, p, b, RControl);
    CenterLabel(d, p, b, label, active ? White : IM_COL32(189, 85, 75, 255));
    return pressed;
}

// .iconbtn — quiet 31px square; hover shows grey pill.
inline bool IconButton(const char* id, Icon kind, const char* tooltip,
                       float h = 31, float w = 0, ImU32 tint = 0) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz(w > 0 ? w : h, h);
    const bool pressed = Hit(id, sz);
    auto* d = ImGui::GetWindowDrawList();
    const bool disabled = ItemDisabled();
    float& a = Anim(ImGui::GetItemID());
    a = Ease(a, ItemHovered() ? 1.0f : 0.0f, 18);
    if (a > 0.004f)
        d->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y),
                         Mix(IM_COL32(240, 240, 238, 0), Panel, a), RControl);
    FocusRing(d, p, ImVec2(p.x + sz.x, p.y + sz.y), RControl);
    const float isz = 15.0f * (h / 31.0f);
    DrawIcon(d, kind,
             ImVec2(p.x + (sz.x - isz) / 2, p.y + (sz.y - isz) / 2),
             isz, disabled ? Disabled : (tint ? tint : IM_COL32(101, 103, 96, 255)));
    if (ItemHovered() && tooltip) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

// .navitem — sidebar row; active = soft grey fill + dark text.
inline bool NavItem(const char* id, const char* label, Icon kind, bool selected,
                    float width, ImFont* font, float height = 37) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = Hit(id, ImVec2(width, height));
    auto* d = ImGui::GetWindowDrawList();
    float& a = Anim(ImGui::GetItemID());
    a = Ease(a, selected ? 1.0f : (ItemHovered() ? 0.55f : 0.0f), 15);
    if (a > 0.004f)
        d->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                         Mix(IM_COL32(245, 245, 243, 0), Panel, a), RControl);
    FocusRing(d, p, ImVec2(p.x + width, p.y + height), RControl);
    const ImU32 icol = Mix(IM_COL32(146, 147, 140, 255), Ink, selected ? 1.0f : a * 0.8f);
    const ImU32 tcol = Mix(Muted, Ink, selected ? 1.0f : a * 0.7f);
    DrawIcon(d, kind, ImVec2(p.x + Sp3, p.y + (height - 15.0f) / 2), 15.0f, icol);
    d->AddText(font, font->FontSize, ImVec2(p.x + Sp3 + 15 + Sp2 + 1, p.y + (height - font->FontSize) / 2),
               tcol, label);
    return clicked;
}

// .segmented — grouped mini tabs used on detail pages.
inline bool SegItem(const char* label, bool selected, float width, float height = 25) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(width, height));
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + width, p.y + height);
    float& a = Anim(ImGui::GetItemID());
    a = Ease(a, selected ? 1.0f : (ItemHovered() ? 0.4f : 0.0f), 16);
    if (a > 0.004f)
        d->AddRectFilled(p, b, Mix(IM_COL32(241, 241, 239, 0), White, a), 6);
    CenterLabel(d, p, b, label, Mix(Muted, Ink, a));
    FocusRing(d, p, b, 6);
    return pressed;
}

// ---- data controls -----------------------------------------------------------------

// .switch — 31x18 pill.
inline bool Switch(const char* label, bool* value) {
    const float h = 18.0f, w = 31.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
    if (clicked) *value = !*value;
    float& a = Anim(ImGui::GetItemID());
    a = Ease(a, *value ? 1.0f : 0.0f, 20);
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                     Mix(IM_COL32(217, 218, 213, 255), Green, a), h * 0.5f);
    const float r = (h - 4) * 0.5f;
    d->AddCircleFilled(ImVec2(p.x + 4 + r + (w - 4 - 2 * r) * a, p.y + h / 2), r, White, 20);
    if (ItemFocused()) d->AddRect(p, ImVec2(p.x + w, p.y + h), Blue, h * 0.5f, 0, 1.2f);
    return clicked;
}

inline void ProgressBar(float fraction, float width, float height = 4) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, ImVec2(p.x + width, p.y + height), Panel, height);
    if (fraction > 0)
        d->AddRectFilled(p, ImVec2(p.x + width * std::clamp(fraction, 0.f, 1.f),
                                   p.y + height), Muted, height);
    ImGui::Dummy(ImVec2(width, height + 2));
}

// ---- containers ---------------------------------------------------------------------

// .card — white rounded card with hairline border (+optional shadow).
inline ImVec2 CardBegin(ImVec2 size, bool shadow = true, ImU32 fill = Canvas,
                        ImU32 edge = Border) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 b(p.x + size.x, p.y + size.y);
    auto* d = ImGui::GetWindowDrawList();
    if (shadow) CardShadow(d, p, b, RCard);
    d->AddRectFilled(p, b, fill, RCard);
    d->AddRect(p, b, edge, RCard, 0, 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + Sp4, p.y + Sp4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginGroup();
    ImGui::PushItemWidth(size.x - Sp4 * 2);
    return b;
}

inline void CardEnd(ImVec2 card_min, ImVec2 card_max) {
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_max.y));
    ImGui::Dummy(ImVec2(card_max.x - card_min.x, 1));
    ImGui::SameLine(0, 0);
    ImGui::Dummy(ImVec2(1, card_max.y - card_min.y));
}

// .stage-card — large media area; caller paints the inside.
// fill=false draws only the outline/scrim chrome (SDL video already
// rendered beneath this draw list must stay visible).
inline void StageCard(ImDrawList* d, ImVec2 a, ImVec2 b, bool fill = true) {
    CardShadow(d, a, b, RPanel);
    if (fill) {
        d->AddRectFilled(a, b, IM_COL32(232, 238, 234, 255), RPanel);
        // two soft radial glows approximated by layered translucent ellipses
        for (int i = 4; i >= 1; --i) {
            const float rr = 90.0f * i;
            d->AddCircleFilled(ImVec2(a.x + (b.x - a.x) * 0.2f, a.y + (b.y - a.y) * 0.24f),
                               rr, IM_COL32(255, 255, 255, 10), 40);
            d->AddCircleFilled(ImVec2(a.x + (b.x - a.x) * 0.87f, a.y + (b.y - a.y) * 0.77f),
                               rr * 0.85f, IM_COL32(214, 224, 217, 8), 40);
        }
    }
    d->AddRect(a, b, IM_COL32(230, 232, 227, 255), RPanel, 0, 1.0f);
}

inline bool LinkButton(const char* id, const char* label, ImFont* font) {
    const ImVec2 ts = font->CalcTextSizeA(font->FontSize, 10000, 0, label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = Hit(id, ImVec2(ts.x, ts.y + 4));
    auto* d = ImGui::GetWindowDrawList();
    float& a = Anim(ImGui::GetItemID());
    a = Ease(a, ItemHovered() ? 1.0f : 0.0f, 20);
    const ImU32 c = Mix(Blue, IM_COL32(46, 116, 214, 255), a);
    d->AddText(font, font->FontSize, ImVec2(p.x, p.y + 2), c, label);
    d->AddLine(ImVec2(p.x, p.y + ts.y + 2), ImVec2(p.x + ts.x, p.y + ts.y + 2), c, 1.2f);
    FocusRing(d, p, ImVec2(p.x + ts.x, p.y + ts.y + 4), 4);
    return clicked;
}

// .status-dot — small colored dot with halo.
inline void StatusDot(ImDrawList* d, ImVec2 center, float r, ImU32 color, bool halo = true) {
    if (halo) d->AddCircleFilled(center, r + 3, IM_COL32(36, 150, 107, 26), 16);
    d->AddCircleFilled(center, r, color, 16);
}

} // namespace ap::video::app_ui
