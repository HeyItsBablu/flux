// flux/flux_color.hpp
#pragma once

#include <cstdint>
#include <cmath>

// ============================================================================
// CROSS-PLATFORM COLOR
// ============================================================================

struct Color
{
    uint8_t r, g, b, a;

    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}

    static constexpr Color fromRGB(uint8_t r, uint8_t g, uint8_t b)
    {
        return Color(r, g, b, 255);
    }
    static constexpr Color fromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return Color(r, g, b, a);
    }
    // 0xRRGGBBAA
    static constexpr Color fromHex(uint32_t hex)
    {
        return Color(
            (hex >> 24) & 0xFF,
            (hex >> 16) & 0xFF,
            (hex >> 8) & 0xFF,
            hex & 0xFF);
    }
    // 0xRRGGBB, alpha = 255
    static constexpr Color fromHex24(uint32_t hex)
    {
        return Color(
            (hex >> 16) & 0xFF,
            (hex >> 8) & 0xFF,
            hex & 0xFF,
            255);
    }

    static Color fromHSV(float h, float s, float v, float a = 1.f)
    {
        h = std::fmod(h, 360.f);
        if (h < 0.f)
            h += 360.f;
        float c = v * s;
        float x = c * (1.f - std::abs(std::fmod(h / 60.f, 2.f) - 1.f));
        float m = v - c;
        float r = 0, g = 0, b = 0;
        if (h < 60)
        {
            r = c;
            g = x;
            b = 0;
        }
        else if (h < 120)
        {
            r = x;
            g = c;
            b = 0;
        }
        else if (h < 180)
        {
            r = 0;
            g = c;
            b = x;
        }
        else if (h < 240)
        {
            r = 0;
            g = x;
            b = c;
        }
        else if (h < 300)
        {
            r = x;
            g = 0;
            b = c;
        }
        else
        {
            r = c;
            g = 0;
            b = x;
        }
        return Color(
            uint8_t((r + m) * 255.f),
            uint8_t((g + m) * 255.f),
            uint8_t((b + m) * 255.f),
            uint8_t(a * 255.f));
    }

    Color withAlpha(uint8_t newAlpha) const { return Color(r, g, b, newAlpha); }

    Color darken(int amount) const
    {
        auto clamp = [](int v) -> uint8_t
        {
            return v < 0 ? 0 : v > 255 ? 255
                                       : static_cast<uint8_t>(v);
        };
        return Color(clamp(r - amount), clamp(g - amount), clamp(b - amount), a);
    }

    Color lighten(int amount) const
    {
        auto clamp = [](int v) -> uint8_t
        {
            return v < 0 ? 0 : v > 255 ? 255
                                       : static_cast<uint8_t>(v);
        };
        return Color(clamp(r + amount), clamp(g + amount), clamp(b + amount), a);
    }

    Color interpolate(const Color &other, double t) const
    {
        if (t <= 0.0)
            return *this;
        if (t >= 1.0)
            return other;
        return Color(
            static_cast<uint8_t>(r + (other.r - r) * t),
            static_cast<uint8_t>(g + (other.g - g) * t),
            static_cast<uint8_t>(b + (other.b - b) * t),
            static_cast<uint8_t>(a + (other.a - a) * t));
    }

    bool operator==(const Color &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color &o) const { return !(*this == o); }
};

using NativeColor = Color;

// ============================================================================
// PREDEFINED COLOR PALETTE
// ============================================================================

namespace Colors
{
    // ── Basics ───────────────────────────────────────────────────────────
    inline constexpr Color transparent = Color::fromRGBA(0, 0, 0, 0);
    inline constexpr Color black = Color::fromRGB(0, 0, 0);
    inline constexpr Color white = Color::fromRGB(255, 255, 255);

    // ── Material palette (500 shade) ────────────────────────────────────
    inline constexpr Color red = Color::fromRGB(244, 67, 54);
    inline constexpr Color pink = Color::fromRGB(233, 30, 99);
    inline constexpr Color purple = Color::fromRGB(156, 39, 176);
    inline constexpr Color deepPurple = Color::fromRGB(103, 58, 183);
    inline constexpr Color indigo = Color::fromRGB(63, 81, 181);
    inline constexpr Color blue = Color::fromRGB(33, 150, 243);
    inline constexpr Color lightBlue = Color::fromRGB(3, 169, 244);
    inline constexpr Color cyan = Color::fromRGB(0, 188, 212);
    inline constexpr Color teal = Color::fromRGB(0, 150, 136);
    inline constexpr Color green = Color::fromRGB(76, 175, 80);
    inline constexpr Color lightGreen = Color::fromRGB(139, 195, 74);
    inline constexpr Color lime = Color::fromRGB(205, 220, 57);
    inline constexpr Color yellow = Color::fromRGB(255, 235, 59);
    inline constexpr Color amber = Color::fromRGB(255, 193, 7);
    inline constexpr Color orange = Color::fromRGB(255, 152, 0);
    inline constexpr Color deepOrange = Color::fromRGB(255, 87, 34);
    inline constexpr Color brown = Color::fromRGB(121, 85, 72);

    // ── Accent variants (the brighter "A200"-equivalent used for CTAs) ──
    inline constexpr Color redAccent = Color::fromRGB(255, 82, 82);
    inline constexpr Color pinkAccent = Color::fromRGB(255, 64, 129);
    inline constexpr Color purpleAccent = Color::fromRGB(224, 64, 251);
    inline constexpr Color blueAccent = Color::fromRGB(68, 138, 255);
    inline constexpr Color tealAccent = Color::fromRGB(100, 255, 218);
    inline constexpr Color greenAccent = Color::fromRGB(105, 240, 174);
    inline constexpr Color amberAccent = Color::fromRGB(255, 215, 64);
    inline constexpr Color orangeAccent = Color::fromRGB(255, 145, 64);

    // ── Grey — full shade ramp ───────────────────────────────────────────
    namespace Grey
    {
        inline constexpr Color shade50 = Color::fromRGB(250, 250, 250);
        inline constexpr Color shade100 = Color::fromRGB(245, 245, 245);
        inline constexpr Color shade200 = Color::fromRGB(238, 238, 238);
        inline constexpr Color shade300 = Color::fromRGB(224, 224, 224);
        inline constexpr Color shade400 = Color::fromRGB(189, 189, 189);
        inline constexpr Color shade500 = Color::fromRGB(158, 158, 158);
        inline constexpr Color shade600 = Color::fromRGB(117, 117, 117);
        inline constexpr Color shade700 = Color::fromRGB(97, 97, 97);
        inline constexpr Color shade800 = Color::fromRGB(66, 66, 66);
        inline constexpr Color shade900 = Color::fromRGB(33, 33, 33);
    }
    inline constexpr Color grey = Grey::shade500;

    // ── Blue Grey — full shade ramp ──────────────────────────────────────
    namespace BlueGrey
    {
        inline constexpr Color shade50 = Color::fromRGB(236, 239, 241);
        inline constexpr Color shade100 = Color::fromRGB(207, 216, 220);
        inline constexpr Color shade200 = Color::fromRGB(176, 190, 197);
        inline constexpr Color shade300 = Color::fromRGB(144, 164, 174);
        inline constexpr Color shade400 = Color::fromRGB(120, 144, 156);
        inline constexpr Color shade500 = Color::fromRGB(96, 125, 139);
        inline constexpr Color shade600 = Color::fromRGB(84, 110, 122);
        inline constexpr Color shade700 = Color::fromRGB(69, 90, 100);
        inline constexpr Color shade800 = Color::fromRGB(55, 71, 79);
        inline constexpr Color shade900 = Color::fromRGB(38, 50, 56);
    }
    inline constexpr Color blueGrey = BlueGrey::shade500;

} // namespace Colors