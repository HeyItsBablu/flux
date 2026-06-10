// flux_font_macos.mm
// macOS CoreText FontCache implementation.
//
// NativeFont is void* on non-Win32 platforms (see flux_font.hpp).
// On macOS we store a CTFontRef inside that void*.
//
// Underline and strikethrough are NOT baked into the CTFont itself —
// CoreText applies them at draw time via NSAttributedString / CTLine
// attributes, so the font object is created without them.
// The FontKey still differentiates on underline/strikeOut so that
// callers which request decorated fonts get distinct cache entries
// (consistent with the Win32 and Linux behaviour), even though the
// returned CTFontRef is identical for plain vs decorated at this level.
// The Painter layer is responsible for setting the attribute at draw time.
//
// Caller must NOT CFRelease the returned CTFontRef — the cache owns it.
// Call FontCache::clear() (e.g. in ~FontCache) to release everything.
//
// Link against: CoreText  CoreFoundation  (already in the Apple CMake block)
//
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include "flux/flux_font.hpp"

// ============================================================================
// Internal helpers
// ============================================================================

// Convert FontWeight → CoreText symbolic weight (-1.0 … +1.0 scale).
// kCTFontWeightLight ≈ -0.4, Regular = 0.0, Bold ≈ +0.4
static CTFontSymbolicTraits weightToSymbolicTraits(FontWeight w) {
    // Symbolic traits only has a bold bit; light is handled via the
    // variation axis below.
    if (w == FontWeight::Bold) return kCTFontTraitBold;
    return 0;
}

// Map our three weights to the CoreText weight value used in the
// traits dictionary.  Range is -1.0 (ultra-light) to +1.0 (ultra-black).
static CGFloat weightToCTWeight(FontWeight w) {
    switch (w) {
    case FontWeight::Light:  return -0.4f;  // kCTFontWeightLight
    case FontWeight::Bold:   return  0.4f;  // kCTFontWeightBold
    case FontWeight::Normal:
    default:                 return  0.0f;  // kCTFontWeightRegular
    }
}

// ============================================================================
// FontCache::createFont
// ============================================================================

NativeFont FontCache::createFont(const FontKey& key) {
    // ── Build a traits dictionary that carries the weight ─────────────────────
    CGFloat ctWeight = weightToCTWeight(key.weight);
    CTFontSymbolicTraits symbolic = weightToSymbolicTraits(key.weight);

    NSDictionary* traits = @{
        (NSString*)kCTFontWeightTrait : @(ctWeight),
        (NSString*)kCTFontSymbolicTrait : @(symbolic),
    };

    // ── Build the font descriptor ─────────────────────────────────────────────
    NSString* family = [NSString stringWithUTF8String:key.family.c_str()];

    NSDictionary* attrs = @{
        (NSString*)kCTFontFamilyNameAttribute : family,
        (NSString*)kCTFontTraitsAttribute     : traits,
    };

    CTFontDescriptorRef descriptor =
        CTFontDescriptorCreateWithAttributes((__bridge CFDictionaryRef)attrs);

    // Size: CoreText uses points directly (same as CSS pt / UIKit pt).
    // flux_font stores the size in logical pixels set by the caller — treat
    // them as points, matching the Win32 convention where lfHeight == -size
    // gives a cell height in pixels at 96 DPI (1 pt ≈ 1 px on non-Retina).
    CTFontRef font = CTFontCreateWithFontDescriptor(
            descriptor, (CGFloat)key.size, nullptr);

    CFRelease(descriptor);

    if (!font) {
        // Fallback: system UI font at the requested size
        font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem,
                                             (CGFloat)key.size, nullptr);
    }

    // We store the CTFontRef as void*. The cache holds the +1 retain count
    // returned by CTFontCreate*.
    return reinterpret_cast<NativeFont>(font);
}

// ============================================================================
// FontCache public API
// ============================================================================

NativeFont FontCache::getFont(const std::string& family, int size,
                               FontWeight weight) {
    return getFont(family, size, weight, false, false);
}

NativeFont FontCache::getFont(int size, FontWeight weight) {
    // macOS system UI font — ".AppleSystemUIFont" resolves to SF Pro.
    return getFont(".AppleSystemUIFont", size, weight, false, false);
}

NativeFont FontCache::getFont(const std::string& family, int size,
                               FontWeight weight,
                               bool underline, bool strikeOut) {
    FontKey key{ family, size, weight, underline, strikeOut };

    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    NativeFont font = createFont(key);
    cache[key] = font;
    return font;
}

void FontCache::clear() {
    for (auto& pair : cache) {
        CTFontRef font = reinterpret_cast<CTFontRef>(pair.second);
        if (font) CFRelease(font);
    }
    cache.clear();
}

#endif // TARGET_OS_OSX
#endif // __APPLE__