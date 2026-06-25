// flux_font_win32.cpp
#ifdef _WIN32

#include "flux/flux_font.hpp"

// Full DWrite headers — only this file needs them for font creation.
#include <dwrite_3.h>
#include <windows.h>
#include <cstdio>

// ============================================================================
// Internal helpers
// ============================================================================

static void fontDbg(const char* msg)
{
    OutputDebugStringA(msg);
}

// Convert FontWeight enum → DWRITE_FONT_WEIGHT.
// Our enum values match DWRITE_FONT_WEIGHT numerically (100–900), so this
// is a direct cast. The explicit function keeps the intent clear.
static DWRITE_FONT_WEIGHT toDWriteWeight(FontWeight w)
{
    return static_cast<DWRITE_FONT_WEIGHT>(static_cast<int>(w));
}

// ============================================================================
// FontCache::createFont  (Win32 / DWrite)
//
// Returns an IDWriteTextFormat* cast to NativeFont (void*-equivalent alias).
// The format encodes: family, size, weight, style (italic/normal).
// Underline and strikethrough are NOT part of the format — they are applied
// per-layout via IDWriteTextLayout::SetUnderline / SetStrikethrough at
// draw time in Painter::drawRichText.
// ============================================================================

NativeFont FontCache::createFont(const FontKey& key)
{
    if (!dwriteFactory_)
    {
        fontDbg("[FontCache] createFont called before setDWriteFactory\n");
        return nullptr;
    }

    // Convert family name to wide string.
    std::wstring familyW = toWideString(key.family);

    DWRITE_FONT_STYLE style = key.italic
        ? DWRITE_FONT_STYLE_ITALIC
        : DWRITE_FONT_STYLE_NORMAL;

    IDWriteTextFormat* format = nullptr;
    HRESULT hr = dwriteFactory_->CreateTextFormat(
        familyW.c_str(),
        nullptr,                          // system font collection
        toDWriteWeight(key.weight),
        style,
        DWRITE_FONT_STRETCH_NORMAL,
        key.size,                         // size in DIPs (device-independent px)
        L"en-us",                         // locale
        &format);

    if (FAILED(hr) || !format)
    {
        // Fallback: try "Segoe UI" at the same size/weight
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "[FontCache] CreateTextFormat failed for '%s' size=%.1f hr=0x%08X — falling back to Segoe UI\n",
                    key.family.c_str(), key.size, (unsigned)hr);
        fontDbg(buf);

        hr = dwriteFactory_->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            toDWriteWeight(key.weight),
            style,
            DWRITE_FONT_STRETCH_NORMAL,
            key.size,
            L"en-us",
            &format);

        if (FAILED(hr) || !format)
        {
            fontDbg("[FontCache] Fallback CreateTextFormat also failed\n");
            return nullptr;
        }
    }

    // Default paragraph alignment — widgets override per draw call.
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Word-wrapping off by default — Painter::drawRichText handles its own
    // line breaking so we never want DWrite to wrap inside the format.
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    return static_cast<NativeFont>(format);
}

// ============================================================================
// FontCache::getFont  (primary — int size, no decoration flags)
// ============================================================================

NativeFont FontCache::getFont(const std::string& family, int size,
                              FontWeight weight)
{
    FontKey key;
    key.family = family;
    key.size   = static_cast<float>(size);
    key.weight = weight;
    key.italic = false;

    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    NativeFont font = createFont(key);
    cache[key] = font;
    return font;
}

// ============================================================================
// FontCache::getFont  (convenience — default family)
// ============================================================================

NativeFont FontCache::getFont(int size, FontWeight weight)
{
    return getFont("Segoe UI", size, weight);
}

// ============================================================================
// FontCache::getFont  (extended — with underline/strikeOut compat params)
//
// underline and strikeOut are accepted so existing call sites (TextWidget,
// IconWidget, etc.) compile without changes. They are intentionally not
// baked into the IDWriteTextFormat — DWrite applies them per text-layout
// run, which is handled in Painter::drawRichText.
// ============================================================================

NativeFont FontCache::getFont(const std::string& family, int size,
                              FontWeight weight,
                              bool /*underline*/, bool /*strikeOut*/)
{
    // Decoration flags intentionally ignored on Win32 D2D path.
    return getFont(family, size, weight);
}

// ============================================================================
// FontCache::getFontItalic
// ============================================================================

NativeFont FontCache::getFontItalic(const std::string& family, int size,
                                    FontWeight weight)
{
    FontKey key;
    key.family = family;
    key.size   = static_cast<float>(size);
    key.weight = weight;
    key.italic = true;

    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    NativeFont font = createFont(key);
    cache[key] = font;
    return font;
}

// ============================================================================
// FontCache::clear
//
// Release all IDWriteTextFormat objects.
// Called by FontCache destructor and on device loss.
// ============================================================================

void FontCache::clear()
{
    for (auto& pair : cache)
    {
        if (pair.second)
        {
            auto* fmt = static_cast<IDWriteTextFormat*>(pair.second);
            fmt->Release();
        }
    }
    cache.clear();
}

#endif // _WIN32
