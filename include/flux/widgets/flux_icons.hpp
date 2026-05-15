#pragma once

// ============================================================================
// FluxIcons — cross-platform icon glyph codepoints
//
// Windows : Segoe MDL2 Assets  (ships with Windows 10/11)
// Android : Material Design Icons webfont v5+ (Templarian/MaterialDesign-Webfont)
//           Codepoints are in the Unicode Supplementary Private Use Area,
//           range U+F0001–U+F1AF0. These require 4-byte UTF-8 encoding and
//           wchar_t values above 0xFFFF (use char32_t or surrogate pairs
//           on platforms where wchar_t is 16-bit — see flux_painter_android.cpp).
//
// Usage:
//   Icon(FluxIcons::Add)              — uses platform default font
//   Icon(FluxIcons::Add, 20)          — explicit size
// ============================================================================

// ── Platform font name ────────────────────────────────────────────────────────
#ifdef _WIN32
    inline constexpr const char* kIconFont = "Segoe MDL2 Assets";
#else
    inline constexpr const char* kIconFont = "Material Design Icons";
#endif

namespace FluxIcons {

// Each constant is a struct carrying both platform codepoints.
// On Android/Linux the linux_ value is a full Unicode codepoint (may exceed
// 0xFFFF for MDI v5+) stored as uint32_t so it survives on 16-bit wchar_t
// platforms. The Icon() factory and glyph() helper use it correctly.
struct IconGlyph {
    wchar_t  win;     // Segoe MDL2 Assets codepoint (all fit in 16-bit)
    uint32_t linux_;  // MDI v5+ codepoint (U+F0001 range, needs uint32_t)
};

// ── Helper: returns the platform codepoint as uint32_t ───────────────────────
inline uint32_t glyph(const IconGlyph& g) {
#ifdef _WIN32
    return static_cast<uint32_t>(g.win);
#else
    return g.linux_;
#endif
}

// ── Actions ───────────────────────────────────────────────────────────────────
//                               Segoe          MDI v5+ webfont
inline constexpr IconGlyph Add        = { 0xE710, 0xF0415 }; // plus
inline constexpr IconGlyph Remove     = { 0xE738, 0xF0374 }; // minus
inline constexpr IconGlyph Edit       = { 0xE70F, 0xF03EB }; // pencil
inline constexpr IconGlyph Delete     = { 0xE74D, 0xF01C0 }; // delete / trash
inline constexpr IconGlyph Save       = { 0xE74E, 0xF0193 }; // content-save
inline constexpr IconGlyph Refresh    = { 0xE72C, 0xF0450 }; // refresh
inline constexpr IconGlyph Download   = { 0xE896, 0xF01DA }; // download
inline constexpr IconGlyph Upload     = { 0xE898, 0xF0552 }; // upload
inline constexpr IconGlyph Search     = { 0xE721, 0xF0349 }; // magnify
inline constexpr IconGlyph Filter     = { 0xE71C, 0xF0232 }; // filter
inline constexpr IconGlyph Settings   = { 0xE713, 0xF0493 }; // cog / settings
inline constexpr IconGlyph More       = { 0xE712, 0xF03C7 }; // dots-vertical

// ── Navigation ────────────────────────────────────────────────────────────────
inline constexpr IconGlyph Menu         = { 0xE700, 0xF035C }; // menu
inline constexpr IconGlyph Back         = { 0xE72B, 0xF004D }; // arrow-left
inline constexpr IconGlyph Forward      = { 0xE72A, 0xF0054 }; // arrow-right
inline constexpr IconGlyph Up           = { 0xE74A, 0xF005D }; // arrow-up
inline constexpr IconGlyph Down         = { 0xE74B, 0xF0045 }; // arrow-down
inline constexpr IconGlyph ChevronLeft  = { 0xE76B, 0xF0141 }; // chevron-left
inline constexpr IconGlyph ChevronRight = { 0xE76C, 0xF0142 }; // chevron-right

// ── Status ────────────────────────────────────────────────────────────────────
inline constexpr IconGlyph Check   = { 0xE73E, 0xF012C }; // check
inline constexpr IconGlyph Close   = { 0xE711, 0xF0156 }; // close
inline constexpr IconGlyph Cancel  = { 0xE711, 0xF0156 }; // close (alias)
inline constexpr IconGlyph Warning = { 0xE7BA, 0xF05D7 }; // alert
inline constexpr IconGlyph Error   = { 0xE783, 0xF0159 }; // close-circle
inline constexpr IconGlyph Info    = { 0xE946, 0xF02FC }; // information

// ── Media ─────────────────────────────────────────────────────────────────────
inline constexpr IconGlyph Play   = { 0xE768, 0xF040A }; // play
inline constexpr IconGlyph Pause  = { 0xE769, 0xF03E4 }; // pause
inline constexpr IconGlyph Stop   = { 0xE71A, 0xF04F1 }; // stop
inline constexpr IconGlyph Volume = { 0xE767, 0xF057E }; // volume-high
inline constexpr IconGlyph Mute   = { 0xE74F, 0xF057A }; // volume-off

// ── File / Folder ─────────────────────────────────────────────────────────────
inline constexpr IconGlyph Folder     = { 0xE8B7, 0xF024B }; // folder
inline constexpr IconGlyph FolderOpen = { 0xE838, 0xF024C }; // folder-open
inline constexpr IconGlyph Document   = { 0xE8A5, 0xF0224 }; // file-document
inline constexpr IconGlyph Copy       = { 0xE8C8, 0xF018F }; // content-copy
inline constexpr IconGlyph Paste      = { 0xE77F, 0xF0192 }; // content-paste

// ── UI Controls ───────────────────────────────────────────────────────────────
inline constexpr IconGlyph Eye        = { 0xE890, 0xF0208 }; // eye
inline constexpr IconGlyph EyeOff     = { 0xE891, 0xF0209 }; // eye-off
inline constexpr IconGlyph Lock       = { 0xE72E, 0xF033E }; // lock
inline constexpr IconGlyph Unlock     = { 0xE785, 0xF033F }; // lock-open
inline constexpr IconGlyph Calendar   = { 0xE787, 0xF00ED }; // calendar
inline constexpr IconGlyph Clock      = { 0xE823, 0xF0150 }; // clock
inline constexpr IconGlyph User       = { 0xE77B, 0xF0004 }; // account
inline constexpr IconGlyph Home       = { 0xE80F, 0xF02DC }; // home
inline constexpr IconGlyph Star       = { 0xE734, 0xF04CE }; // star-outline
inline constexpr IconGlyph StarFilled = { 0xE735, 0xF04D2 }; // star

// ── Window Controls ───────────────────────────────────────────────────────────
inline constexpr IconGlyph Minimize    = { 0xE921, 0xF0374 }; // minus (closest)
inline constexpr IconGlyph Maximize    = { 0xE922, 0xF016B }; // fullscreen
inline constexpr IconGlyph Restore     = { 0xE923, 0xF016C }; // fullscreen-exit
inline constexpr IconGlyph CloseWindow = { 0xE8BB, 0xF0156 }; // close

// ── Misc ──────────────────────────────────────────────────────────────────────
inline constexpr IconGlyph Grid      = { 0xE80A, 0xF0301 }; // view-grid
inline constexpr IconGlyph Dashboard = { 0xF246, 0xF0200 }; // view-dashboard
inline constexpr IconGlyph ChartBar  = { 0xE9D2, 0xF0080 }; // chart-bar

} // namespace FluxIcons