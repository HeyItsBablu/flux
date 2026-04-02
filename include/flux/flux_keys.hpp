// flux/flux_keys.hpp
#pragma once

// ============================================================================
// PLATFORM-NEUTRAL KEY CODES
// ============================================================================
// All widget code uses Key::* constants exclusively.
// Platform-specific values are mapped here — never leak VK_* or SDL scancodes
// into the widget layer.
// ============================================================================

namespace Key {

// ============================================================================
// Win32
// ============================================================================
#ifdef _WIN32
    #include <windows.h>

inline constexpr int Left      = VK_LEFT;
inline constexpr int Right     = VK_RIGHT;
inline constexpr int Up        = VK_UP;
inline constexpr int Down      = VK_DOWN;
inline constexpr int Home      = VK_HOME;
inline constexpr int End       = VK_END;
inline constexpr int PageUp    = VK_PRIOR;
inline constexpr int PageDown  = VK_NEXT;
inline constexpr int Tab       = VK_TAB;

inline constexpr int Backspace = VK_BACK;
inline constexpr int Delete    = VK_DELETE;
inline constexpr int Insert    = VK_INSERT;

inline constexpr int Return    = VK_RETURN;
inline constexpr int Escape    = VK_ESCAPE;
inline constexpr int Space     = VK_SPACE;

inline constexpr int Shift     = VK_SHIFT;
inline constexpr int Control   = VK_CONTROL;
inline constexpr int Alt       = VK_MENU;

inline constexpr int F1        = VK_F1;
inline constexpr int F2        = VK_F2;
inline constexpr int F3        = VK_F3;
inline constexpr int F4        = VK_F4;
inline constexpr int F5        = VK_F5;
inline constexpr int F6        = VK_F6;
inline constexpr int F7        = VK_F7;
inline constexpr int F8        = VK_F8;
inline constexpr int F9        = VK_F9;
inline constexpr int F10       = VK_F10;
inline constexpr int F11       = VK_F11;
inline constexpr int F12       = VK_F12;

inline constexpr int OemPlus   = VK_OEM_PLUS;
inline constexpr int OemMinus  = VK_OEM_MINUS;
inline constexpr int NumpadPlus  = VK_ADD;
inline constexpr int NumpadMinus = VK_SUBTRACT;
inline constexpr int NumpadMul   = VK_MULTIPLY;
inline constexpr int NumpadDiv   = VK_DIVIDE;
inline constexpr int Oem1      = VK_OEM_1;   // ; /: on US layout
inline constexpr int Oem2      = VK_OEM_2;   // / /? on US layout
inline constexpr int Oem3      = VK_OEM_3;   // ` /~ on US layout
inline constexpr int Oem4      = VK_OEM_4;   // [ /{ on US layout
inline constexpr int Oem5      = VK_OEM_5;   // \ /| on US layout
inline constexpr int Oem6      = VK_OEM_6;   // ] /} on US layout
inline constexpr int Oem7      = VK_OEM_7;   // ' /" on US layout

inline bool isShiftDown()   { return (GetKeyState(VK_SHIFT)   & 0x8000) != 0; }
inline bool isControlDown() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
inline bool isAltDown()     { return (GetKeyState(VK_MENU)    & 0x8000) != 0; }

// ============================================================================
// Linux (SDL2 scancodes — matches VK_* aliases in flux_platform.hpp)
// ============================================================================
#elif defined(__linux__) && !defined(__ANDROID__)
    #include <SDL2/SDL.h>

inline constexpr int Left      = SDL_SCANCODE_LEFT;
inline constexpr int Right     = SDL_SCANCODE_RIGHT;
inline constexpr int Up        = SDL_SCANCODE_UP;
inline constexpr int Down      = SDL_SCANCODE_DOWN;
inline constexpr int Home      = SDL_SCANCODE_HOME;
inline constexpr int End       = SDL_SCANCODE_END;
inline constexpr int PageUp    = SDL_SCANCODE_PAGEUP;
inline constexpr int PageDown  = SDL_SCANCODE_PAGEDOWN;
inline constexpr int Tab       = SDL_SCANCODE_TAB;

inline constexpr int Backspace = SDL_SCANCODE_BACKSPACE;
inline constexpr int Delete    = SDL_SCANCODE_DELETE;
inline constexpr int Insert    = SDL_SCANCODE_INSERT;

inline constexpr int Return    = SDL_SCANCODE_RETURN;
inline constexpr int Escape    = SDL_SCANCODE_ESCAPE;
inline constexpr int Space     = SDL_SCANCODE_SPACE;

inline constexpr int Shift     = SDL_SCANCODE_LSHIFT;
inline constexpr int Control   = SDL_SCANCODE_LCTRL;
inline constexpr int Alt       = SDL_SCANCODE_LALT;

inline constexpr int F1        = SDL_SCANCODE_F1;
inline constexpr int F2        = SDL_SCANCODE_F2;
inline constexpr int F3        = SDL_SCANCODE_F3;
inline constexpr int F4        = SDL_SCANCODE_F4;
inline constexpr int F5        = SDL_SCANCODE_F5;
inline constexpr int F6        = SDL_SCANCODE_F6;
inline constexpr int F7        = SDL_SCANCODE_F7;
inline constexpr int F8        = SDL_SCANCODE_F8;
inline constexpr int F9        = SDL_SCANCODE_F9;
inline constexpr int F10       = SDL_SCANCODE_F10;
inline constexpr int F11       = SDL_SCANCODE_F11;
inline constexpr int F12       = SDL_SCANCODE_F12;

inline constexpr int OemPlus   = SDL_SCANCODE_EQUALS;
inline constexpr int OemMinus  = SDL_SCANCODE_MINUS;
inline constexpr int NumpadPlus  = SDL_SCANCODE_KP_PLUS;
inline constexpr int NumpadMinus = SDL_SCANCODE_KP_MINUS;
inline constexpr int NumpadMul   = SDL_SCANCODE_KP_MULTIPLY;
inline constexpr int NumpadDiv   = SDL_SCANCODE_KP_DIVIDE;
inline constexpr int Oem1      = SDL_SCANCODE_SEMICOLON;
inline constexpr int Oem2      = SDL_SCANCODE_SLASH;
inline constexpr int Oem3      = SDL_SCANCODE_GRAVE;
inline constexpr int Oem4      = SDL_SCANCODE_LEFTBRACKET;
inline constexpr int Oem5      = SDL_SCANCODE_BACKSLASH;
inline constexpr int Oem6      = SDL_SCANCODE_RIGHTBRACKET;
inline constexpr int Oem7      = SDL_SCANCODE_APOSTROPHE;

inline bool isShiftDown()   { return (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0; }
inline bool isControlDown() { return (SDL_GetModState() & (KMOD_LCTRL  | KMOD_RCTRL))  != 0; }
inline bool isAltDown()     { return (SDL_GetModState() & (KMOD_LALT   | KMOD_RALT))   != 0; }

// ============================================================================
// Android (Android key codes)
// ============================================================================
#elif defined(__ANDROID__)

    inline constexpr int Left      = 21;
    inline constexpr int Right     = 22;
    inline constexpr int Up        = 19;
    inline constexpr int Down      = 20;
    inline constexpr int Home      = 122;
    inline constexpr int End       = 123;
    inline constexpr int PageUp    = 92;
    inline constexpr int PageDown  = 93;
    inline constexpr int Tab       = 61;

    inline constexpr int Backspace = 67;
    inline constexpr int Delete    = 112;
    inline constexpr int Insert    = 124;

    inline constexpr int Return    = 66;
    inline constexpr int Escape    = 111;
    inline constexpr int Space     = 62;

    inline constexpr int Shift     = 59;
    inline constexpr int Control   = 113;
    inline constexpr int Alt       = 57;

    inline constexpr int F1        = 131;
    inline constexpr int F2        = 132;
    inline constexpr int F3        = 133;
    inline constexpr int F4        = 134;
    inline constexpr int F5        = 135;
    inline constexpr int F6        = 136;
    inline constexpr int F7        = 137;
    inline constexpr int F8        = 138;
    inline constexpr int F9        = 139;
    inline constexpr int F10       = 140;
    inline constexpr int F11       = 141;
    inline constexpr int F12       = 142;

    inline constexpr int OemPlus   = 70;
    inline constexpr int OemMinus  = 69;
    inline constexpr int NumpadPlus  = 157;
    inline constexpr int NumpadMinus = 156;
    inline constexpr int NumpadMul   = 155;
    inline constexpr int NumpadDiv   = 154;
    inline constexpr int Oem1      = 74;  // semicolon
    inline constexpr int Oem2      = 76;  // slash
    inline constexpr int Oem3      = 68;  // grave
    inline constexpr int Oem4      = 71;  // left bracket
    inline constexpr int Oem5      = 73;  // backslash
    inline constexpr int Oem6      = 72;  // right bracket
    inline constexpr int Oem7      = 75;  // apostrophe

    inline bool isShiftDown()   { return false; } // modifier state via AInputEvent
    inline bool isControlDown() { return false; }
    inline bool isAltDown()     { return false; }

// ============================================================================
// macOS — placeholder
// ============================================================================
#elif defined(__APPLE__)

    inline constexpr int Left      = 0x7B;
inline constexpr int Right     = 0x7C;
inline constexpr int Up        = 0x7E;
inline constexpr int Down      = 0x7D;
inline constexpr int Home      = 0x73;
inline constexpr int End       = 0x77;
inline constexpr int PageUp    = 0x74;
inline constexpr int PageDown  = 0x79;
inline constexpr int Tab       = 0x30;
inline constexpr int Backspace = 0x33;
inline constexpr int Delete    = 0x75;
inline constexpr int Insert    = -1;
inline constexpr int Return    = 0x24;
inline constexpr int Escape    = 0x35;
inline constexpr int Space     = 0x31;
inline constexpr int Shift     = 0x38;
inline constexpr int Control   = 0x3B;
inline constexpr int Alt       = 0x3A;
inline constexpr int F1        = 0x7A;
inline constexpr int F2        = 0x78;
inline constexpr int F3        = 0x63;
inline constexpr int F4        = 0x76;
inline constexpr int F5        = 0x60;
inline constexpr int F6        = 0x61;
inline constexpr int F7        = 0x62;
inline constexpr int F8        = 0x64;
inline constexpr int F9        = 0x65;
inline constexpr int F10       = 0x6D;
inline constexpr int F11       = 0x67;
inline constexpr int F12       = 0x6F;

inline bool isShiftDown()   { return false; } // TODO: CGEventSourceKeyState
inline bool isControlDown() { return false; }
inline bool isAltDown()     { return false; }

#else
#error "Unsupported platform — add Key:: mappings to flux_keys.hpp"
#endif

} // namespace Key