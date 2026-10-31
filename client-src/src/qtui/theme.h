// theme.cpp/.h — airmirror palette translated to QSS + a few runtime-accessible
// constants. Everything visual derives from here.
//
// Palette follows the Monocle reference: a pure-white page with light-grey
// containers sitting on top of it (the inverse of the earlier "grey page, white
// card" layout), near-black text, and exactly one saturated accent (blue) plus
// a warm orange reserved for highlights. Depth comes from value steps, not
// borders — there are no 1px outlines anywhere in the reference.

#pragma once

#include <QString>

namespace ap::ui {

// ---- palette ---------------------------------------------------------
inline constexpr const char* ColBg        = "#F6F6F4";  // page
inline constexpr const char* ColCanvas    = "#FFFFFF";  // raised surface / rows
inline constexpr const char* ColSurface   = "#FBFBF9";  // secondary surface
inline constexpr const char* ColPanel     = "#EDEDEA";  // sidebar shell
inline constexpr const char* ColPanelDeep = "#EAEAE8";  // pressed / track
inline constexpr const char* ColInk       = "#0A0A0A";
inline constexpr const char* ColInkSoft   = "#3C3C3C";
inline constexpr const char* ColMuted     = "#8C8C8C";
inline constexpr const char* ColSubtle    = "#B8B8B8";
inline constexpr const char* ColBorder    = "#EFEFEF";  // hairlines only, used sparingly
inline constexpr const char* ColAccent    = "#E8823C";  // warm orange highlight
inline constexpr const char* ColAccentSoft= "#FDF1E9";
inline constexpr const char* ColBlue      = "#0A7CFF";  // the one interactive accent
inline constexpr const char* ColBlueSoft  = "#EAF2FF";
inline constexpr const char* ColGreen     = "#2FA36B";
inline constexpr const char* ColGreenSoft = "#EAF6EF";
inline constexpr const char* ColRed       = "#E05A55";
inline constexpr const char* ColRedSoft   = "#FFF8F7";
inline constexpr const char* ColDanger    = "#C14E49";

// Corner radii: the reference leans on very large radii (24) for containers and
// full-round pills for every control.
inline constexpr int RadiusCard = 24;
inline constexpr int RadiusRow  = 16;

// QSS for the whole application. Controls: buttons, inputs, list nav,
// scrollbars — everything styled in one place.
QString app_style_sheet();

}  // namespace ap::ui
