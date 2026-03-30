// flux/flux_keys.hpp
#pragma once

// ============================================================================
// PLATFORM-NEUTRAL KEY CODES
// ============================================================================
// All widget code uses Key::* constants exclusively.
// Platform-specific values are mapped here — never leak VK_* or XKB syms
// into the widget layer.
// ============================================================================

namespace Key {

#ifdef _WIN32
#include <windows.h>

// ── Navigation ────────────────────────────────────────────────────────────
inline constexpr int Left = VK_LEFT;
inline constexpr int Right = VK_RIGHT;
inline constexpr int Up = VK_UP;
inline constexpr int Down = VK_DOWN;
inline constexpr int Home = VK_HOME;
inline constexpr int End = VK_END;
inline constexpr int PageUp = VK_PRIOR;
inline constexpr int PageDown = VK_NEXT;
inline constexpr int Tab = VK_TAB;

// ── Editing ───────────────────────────────────────────────────────────────
inline constexpr int Backspace = VK_BACK;
inline constexpr int Delete = VK_DELETE;
inline constexpr int Insert = VK_INSERT;

// ── Confirmation / cancellation ───────────────────────────────────────────
inline constexpr int Return = VK_RETURN;
inline constexpr int Escape = VK_ESCAPE;
inline constexpr int Space = VK_SPACE;

// ── Modifiers (for query, not handleKeyDown dispatch) ─────────────────────
inline constexpr int Shift = VK_SHIFT;
inline constexpr int Control = VK_CONTROL;
inline constexpr int Alt = VK_MENU;

// ── Function keys ─────────────────────────────────────────────────────────
inline constexpr int F1 = VK_F1;
inline constexpr int F2 = VK_F2;
inline constexpr int F3 = VK_F3;
inline constexpr int F4 = VK_F4;
inline constexpr int F5 = VK_F5;
inline constexpr int F6 = VK_F6;
inline constexpr int F7 = VK_F7;
inline constexpr int F8 = VK_F8;
inline constexpr int F9 = VK_F9;
inline constexpr int F10 = VK_F10;
inline constexpr int F11 = VK_F11;
inline constexpr int F12 = VK_F12;

inline constexpr int OemPlus = VK_OEM_PLUS;     // main keyboard =/+
inline constexpr int OemMinus = VK_OEM_MINUS;   // main keyboard -/_
inline constexpr int NumpadPlus = VK_ADD;       // numpad +
inline constexpr int NumpadMinus = VK_SUBTRACT; // numpad -
inline constexpr int NumpadMul =
    VK_MULTIPLY;                            // numpad * (add now, costs nothing)
inline constexpr int NumpadDiv = VK_DIVIDE; // numpad / (same)
inline constexpr int Oem1         = VK_OEM_1;      // ; /: on US layout
inline constexpr int Oem2         = VK_OEM_2;      // / /? on US layout
inline constexpr int Oem3         = VK_OEM_3;      // ` /~ on US layout
inline constexpr int Oem4         = VK_OEM_4;      // [ /{ on US layout
inline constexpr int Oem5         = VK_OEM_5;      // \ /| on US layout
inline constexpr int Oem6         = VK_OEM_6;      // ] /} on US layout
inline constexpr int Oem7         = VK_OEM_7;      // ' /" on US layout

// ── Modifier state helpers ────────────────────────────────────────────────
// Use these instead of calling GetKeyState() directly in widget code.
inline bool isShiftDown() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }
inline bool isControlDown() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
inline bool isAltDown() { return (GetKeyState(VK_MENU) & 0x8000) != 0; }

#elif defined(__APPLE__)
// Placeholder — fill in with Carbon/Cocoa keycodes when targeting macOS
inline constexpr int Left = 0x7B;
inline constexpr int Right = 0x7C;
inline constexpr int Up = 0x7E;
inline constexpr int Down = 0x7D;
inline constexpr int Home = 0x73;
inline constexpr int End = 0x77;
inline constexpr int PageUp = 0x74;
inline constexpr int PageDown = 0x79;
inline constexpr int Tab = 0x30;
inline constexpr int Backspace = 0x33;
inline constexpr int Delete = 0x75;
inline constexpr int Insert = -1; // no Insert on macOS — handle with care
inline constexpr int Return = 0x24;
inline constexpr int Escape = 0x35;
inline constexpr int Space = 0x31;
inline constexpr int Shift = 0x38;
inline constexpr int Control = 0x3B;
inline constexpr int Alt = 0x3A; // Option key
inline constexpr int F1 = 0x7A;
inline constexpr int F2 = 0x78;
inline constexpr int F3 = 0x63;
inline constexpr int F4 = 0x76;
inline constexpr int F5 = 0x60;
inline constexpr int F6 = 0x61;
inline constexpr int F7 = 0x62;
inline constexpr int F8 = 0x64;
inline constexpr int F9 = 0x65;
inline constexpr int F10 = 0x6D;
inline constexpr int F11 = 0x67;
inline constexpr int F12 = 0x6F;

inline bool isShiftDown() { return false; } // TODO: CGEventSourceKeyState
inline bool isControlDown() { return false; }
inline bool isAltDown() { return false; }

#elif defined(__linux__)
// XKB keysyms — standard set, works with X11 and most Wayland compositors
#include <X11/keysym.h>

inline constexpr int Left = XK_Left;
inline constexpr int Right = XK_Right;
inline constexpr int Up = XK_Up;
inline constexpr int Down = XK_Down;
inline constexpr int Home = XK_Home;
inline constexpr int End = XK_End;
inline constexpr int PageUp = XK_Page_Up;
inline constexpr int PageDown = XK_Page_Down;
inline constexpr int Tab = XK_Tab;
inline constexpr int Backspace = XK_BackSpace;
inline constexpr int Delete = XK_Delete;
inline constexpr int Insert = XK_Insert;
inline constexpr int Return = XK_Return;
inline constexpr int Escape = XK_Escape;
inline constexpr int Space = XK_space;
inline constexpr int Shift = XK_Shift_L;
inline constexpr int Control = XK_Control_L;
inline constexpr int Alt = XK_Alt_L;
inline constexpr int F1 = XK_F1;
inline constexpr int F2 = XK_F2;
inline constexpr int F3 = XK_F3;
inline constexpr int F4 = XK_F4;
inline constexpr int F5 = XK_F5;
inline constexpr int F6 = XK_F6;
inline constexpr int F7 = XK_F7;
inline constexpr int F8 = XK_F8;
inline constexpr int F9 = XK_F9;
inline constexpr int F10 = XK_F10;
inline constexpr int F11 = XK_F11;
inline constexpr int F12 = XK_F12;

inline bool isShiftDown() { return false; } // TODO: XQueryKeymap
inline bool isControlDown() { return false; }
inline bool isAltDown() { return false; }

#else
#error "Unsupported platform — add Key:: mappings to flux_keys.hpp"
#endif

} // namespace Key