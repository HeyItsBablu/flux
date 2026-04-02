#pragma once

// ============================================================================
// FluxIcons — cross-platform icon glyph codepoints
//
// Windows : Segoe MDL2 Assets  (ships with Windows 10/11)
// Linux   : Material Design Icons webfont
//           pkg: fonts-materialdesignicons-webfont  (Ubuntu/Debian)
//           or:  https://github.com/Templarian/MaterialDesign-Webfont
//           Install: drop .ttf in ~/.local/share/fonts && fc-cache -f
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

// Each constant is a struct so we can carry both codepoints in one name.
// The Icon() factory reads the right one for the current platform.
struct IconGlyph {
    wchar_t win;    // Segoe MDL2 Assets codepoint
    wchar_t linux_; // Material Design Icons webfont codepoint
};

// ── Helper: returns the platform codepoint ────────────────────────────────────
inline wchar_t glyph(const IconGlyph& g) {
#ifdef _WIN32
    return g.win;
#else
    return g.linux_;
#endif
}

// ── Actions ───────────────────────────────────────────────────────────────────
//                               Segoe          MDI webfont
inline constexpr IconGlyph Add        = { 0xE710, 0xF415 }; // plus
inline constexpr IconGlyph Remove     = { 0xE738, 0xF374 }; // minus
inline constexpr IconGlyph Edit       = { 0xE70F, 0xF3EB }; // pencil
inline constexpr IconGlyph Delete     = { 0xE74D, 0xF1C0 }; // delete / trash
inline constexpr IconGlyph Save       = { 0xE74E, 0xF193 }; // content-save
inline constexpr IconGlyph Refresh    = { 0xE72C, 0xF450 }; // refresh
inline constexpr IconGlyph Download   = { 0xE896, 0xF1DA }; // download
inline constexpr IconGlyph Upload     = { 0xE898, 0xF552 }; // upload
inline constexpr IconGlyph Search     = { 0xE721, 0xF349 }; // magnify
inline constexpr IconGlyph Filter     = { 0xE71C, 0xF232 }; // filter
inline constexpr IconGlyph Settings   = { 0xE713, 0xF493 }; // cog / settings
inline constexpr IconGlyph More       = { 0xE712, 0xF3C7 }; // dots-vertical

// ── Navigation ────────────────────────────────────────────────────────────────
inline constexpr IconGlyph Menu         = { 0xE700, 0xF35C }; // menu
inline constexpr IconGlyph Back         = { 0xE72B, 0xF04D }; // arrow-left
inline constexpr IconGlyph Forward      = { 0xE72A, 0xF054 }; // arrow-right
inline constexpr IconGlyph Up           = { 0xE74A, 0xF05D }; // arrow-up
inline constexpr IconGlyph Down         = { 0xE74B, 0xF045 }; // arrow-down
inline constexpr IconGlyph ChevronLeft  = { 0xE76B, 0xF141 }; // chevron-left
inline constexpr IconGlyph ChevronRight = { 0xE76C, 0xF142 }; // chevron-right

// ── Status ────────────────────────────────────────────────────────────────────
inline constexpr IconGlyph Check   = { 0xE73E, 0xF12C }; // check
inline constexpr IconGlyph Close   = { 0xE711, 0xF156 }; // close
inline constexpr IconGlyph Cancel  = { 0xE711, 0xF156 }; // close (alias)
inline constexpr IconGlyph Warning = { 0xE7BA, 0xF5D7 }; // alert
inline constexpr IconGlyph Error   = { 0xE783, 0xF159 }; // close-circle
inline constexpr IconGlyph Info    = { 0xE946, 0xF2FC }; // information

// ── Media ─────────────────────────────────────────────────────────────────────
inline constexpr IconGlyph Play   = { 0xE768, 0xF40A }; // play
inline constexpr IconGlyph Pause  = { 0xE769, 0xF3E4 }; // pause
inline constexpr IconGlyph Stop   = { 0xE71A, 0xF4F1 }; // stop
inline constexpr IconGlyph Volume = { 0xE767, 0xF57E }; // volume-high
inline constexpr IconGlyph Mute   = { 0xE74F, 0xF57A }; // volume-off

// ── File / Folder ─────────────────────────────────────────────────────────────
inline constexpr IconGlyph Folder     = { 0xE8B7, 0xF24B }; // folder
inline constexpr IconGlyph FolderOpen = { 0xE838, 0xF24C }; // folder-open
inline constexpr IconGlyph Document   = { 0xE8A5, 0xF224 }; // file-document
inline constexpr IconGlyph Copy       = { 0xE8C8, 0xF18F }; // content-copy
inline constexpr IconGlyph Paste      = { 0xE77F, 0xF192 }; // content-paste

// ── UI Controls ───────────────────────────────────────────────────────────────
inline constexpr IconGlyph Eye        = { 0xE890, 0xF208 }; // eye
inline constexpr IconGlyph EyeOff     = { 0xE891, 0xF209 }; // eye-off
inline constexpr IconGlyph Lock       = { 0xE72E, 0xF33E }; // lock
inline constexpr IconGlyph Unlock     = { 0xE785, 0xF33F }; // lock-open
inline constexpr IconGlyph Calendar   = { 0xE787, 0xF0ED }; // calendar
inline constexpr IconGlyph Clock      = { 0xE823, 0xF150 }; // clock
inline constexpr IconGlyph User       = { 0xE77B, 0xF004 }; // account
inline constexpr IconGlyph Home       = { 0xE80F, 0xF2DC }; // home
inline constexpr IconGlyph Star       = { 0xE734, 0xF4CE }; // star-outline
inline constexpr IconGlyph StarFilled = { 0xE735, 0xF4D2 }; // star

// ── Window Controls ───────────────────────────────────────────────────────────
inline constexpr IconGlyph Minimize    = { 0xE921, 0xF374 }; // minus (closest)
inline constexpr IconGlyph Maximize    = { 0xE922, 0xF16B }; // fullscreen
inline constexpr IconGlyph Restore     = { 0xE923, 0xF16C }; // fullscreen-exit
inline constexpr IconGlyph CloseWindow = { 0xE8BB, 0xF156 }; // close

// ── Misc ──────────────────────────────────────────────────────────────────────
inline constexpr IconGlyph Grid      = { 0xE80A, 0xF301 }; // view-grid
inline constexpr IconGlyph Dashboard = { 0xF246, 0xF200 }; // view-dashboard
inline constexpr IconGlyph ChartBar  = { 0xE9D2, 0xF080 }; // chart-bar

} // namespace FluxIcons

