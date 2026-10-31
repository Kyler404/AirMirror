#pragma once

// app_ui: the receiver's application UI, rebuilt from scratch as a small
// immediate-mode design system, following the airmirror HTML prototype:
//   theme.h    — colors, type scale, spacing, animation helpers
//   icons.h    — hand-drawn linear icon set (prototype SVG symbols)
//   widgets.h  — reusable controls (buttons, nav, tabs, switches, fields)
//   app_ui.h   — AppUI state machine + Frame(); pages live here
// video_renderer.cpp only fills the Context and calls Frame().

#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace ap::video::app_ui {

// ---- palette (airmirror prototype) -------------------------------------------
constexpr ImU32 Bg          = IM_COL32(245, 245, 243, 255);  // --bg
constexpr ImU32 Canvas      = IM_COL32(255, 255, 255, 255);  // --surface
constexpr ImU32 Surface     = IM_COL32(247, 247, 246, 255);  // --surface-soft
constexpr ImU32 Panel       = IM_COL32(239, 239, 237, 255);  // --surface-muted
constexpr ImU32 Ink         = IM_COL32(32, 33, 31, 255);     // --text
constexpr ImU32 InkSoft     = IM_COL32(80, 81, 75, 255);
constexpr ImU32 Muted       = IM_COL32(133, 134, 129, 255);  // --muted
constexpr ImU32 Subtle      = IM_COL32(170, 170, 165, 255);  // --subtle
constexpr ImU32 Border      = IM_COL32(233, 233, 230, 255);  // --line
constexpr ImU32 Accent      = IM_COL32(216, 110, 73, 255);   // --accent
constexpr ImU32 AccentSoft  = IM_COL32(248, 236, 231, 255);
constexpr ImU32 Blue        = IM_COL32(58, 136, 245, 255);   // --blue
constexpr ImU32 Green       = IM_COL32(36, 150, 107, 255);   // --green
constexpr ImU32 GreenSoft   = IM_COL32(234, 246, 239, 255);
constexpr ImU32 Red         = IM_COL32(224, 90, 85, 255);    // --red
constexpr ImU32 RedSoft     = IM_COL32(255, 248, 247, 255);
constexpr ImU32 White       = IM_COL32(255, 255, 255, 255);
constexpr ImU32 Disabled    = IM_COL32(170, 170, 165, 255);
constexpr ImU32 Danger      = IM_COL32(193, 78, 73, 255);

// ---- spacing (4px grid) -------------------------------------------------------
constexpr float Sp1 = 4, Sp2 = 8, Sp3 = 12, Sp4 = 16, Sp5 = 20, Sp6 = 24,
                Sp8 = 32, Sp10 = 40, Sp12 = 48;
constexpr float RControl = 10, RCard = 17, RPanel = 22;

// ---- layout (prototype grid) ---------------------------------------------------
constexpr float SidebarW  = 190;
constexpr float RightRailW = 292;
constexpr float TopbarH   = 54;
constexpr float FooterH   = 23;

inline ImVec4 Rgba(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

inline ImU32 Mix(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const ImVec4 ca = Rgba(a), cb = Rgba(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
        ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
}

// Frame-rate independent exponential approach for hover/press/progress.
inline float Ease(float current, float target, float speed = 16.0f) {
    float dt = ImGui::GetIO().DeltaTime;
    dt = std::clamp(dt, 0.0f, 0.05f);
    return current + (target - current) * (1.0f - std::exp(-speed * dt));
}

// Per-widget animated scalar stored in ImGui storage, keyed by item ID.
inline float& Anim(ImGuiID id) {
    return *ImGui::GetStateStorage()->GetFloatRef(id, 0.0f);
}

inline void ApplyTheme(ImGuiStyle& s) {
    ImGui::StyleColorsLight();
    s = ImGuiStyle();
    s.WindowRounding = 0; s.ChildRounding = RCard; s.FrameRounding = RControl;
    s.PopupRounding = RCard; s.GrabRounding = RControl; s.TabRounding = RControl;
    s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.PopupBorderSize = 0;
    s.FrameBorderSize = 0; s.TabBorderSize = 0;
    s.WindowPadding = ImVec2(Sp4, Sp4);
    s.FramePadding = ImVec2(Sp3, Sp2);
    s.ItemSpacing = ImVec2(Sp3, Sp2);
    s.ItemInnerSpacing = ImVec2(Sp2, Sp2);
    s.ScrollbarSize = 10;
    auto* c = s.Colors;
    c[ImGuiCol_Text] = Rgba(Ink);
    c[ImGuiCol_TextDisabled] = Rgba(Muted);
    c[ImGuiCol_WindowBg] = Rgba(Bg);
    c[ImGuiCol_ChildBg] = Rgba(Canvas);
    c[ImGuiCol_PopupBg] = Rgba(Canvas);
    c[ImGuiCol_Border] = Rgba(Border);
    c[ImGuiCol_FrameBg] = Rgba(Surface);
    c[ImGuiCol_FrameBgHovered] = Rgba(Panel);
    c[ImGuiCol_FrameBgActive] = Rgba(Panel);
    c[ImGuiCol_Button] = Rgba(Canvas);
    c[ImGuiCol_ButtonHovered] = Rgba(Surface);
    c[ImGuiCol_ButtonActive] = Rgba(Panel);
    c[ImGuiCol_Header] = Rgba(Surface);
    c[ImGuiCol_HeaderHovered] = Rgba(Panel);
    c[ImGuiCol_HeaderActive] = Rgba(Panel);
    c[ImGuiCol_CheckMark] = Rgba(Blue);
    c[ImGuiCol_SliderGrab] = Rgba(Blue);
    c[ImGuiCol_SliderGrabActive] = Rgba(Blue);
    c[ImGuiCol_Separator] = Rgba(Border);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = Rgba(IM_COL32(217, 217, 212, 255));
    c[ImGuiCol_ScrollbarGrabHovered] = Rgba(IM_COL32(190, 190, 185, 255));
    c[ImGuiCol_ScrollbarGrabActive] = Rgba(IM_COL32(170, 170, 165, 255));
    c[ImGuiCol_TextSelectedBg] = Rgba(IM_COL32(58, 136, 245, 50));
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.12f);
}

} // namespace ap::video::app_ui
