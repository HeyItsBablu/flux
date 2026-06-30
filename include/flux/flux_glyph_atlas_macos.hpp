// flux_glyph_atlas_macos.hpp
#pragma once
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Metal/Metal.h>
#import <CoreText/CoreText.h>
#include <unordered_map>
#include <vector>
#include <cstdint>

// ============================================================================
// GlyphKey — identifies one rasterized glyph at one size/font.
//
// CTFontRef pointer identity is stable for the cache's lifetime: FontCache
// owns and CFRetains every font it hands out (see flux_font_macos.mm), so we
// never rasterize against a dangling font. We do NOT retain the CTFontRef
// here ourselves — the atlas borrows it strictly as a map key.
// ============================================================================

struct GlyphKey
{
    CTFontRef font;   // borrowed, not owned
    CGGlyph   glyph;

    bool operator==(const GlyphKey &o) const
    {
        return font == o.font && glyph == o.glyph;
    }
};

struct GlyphKeyHash
{
    size_t operator()(const GlyphKey &k) const
    {
        size_t h1 = std::hash<const void *>()(k.font);
        size_t h2 = std::hash<uint16_t>()(k.glyph);
        return h1 ^ (h2 * 0x9E3779B97F4A7C15ull);
    }
};

// ============================================================================
// GlyphEntry — cached atlas slot + metrics needed to position a quad.
//
// uvMin/uvMax are normalized [0,1] atlas texture coordinates.
// offsetX/offsetY/width/height are in PIXELS, relative to the glyph's
// drawing origin (CoreText convention: origin at baseline, y-up before our
// flip). These get applied per-instance in the draw loop, not baked into
// the atlas geometry, so the same cached glyph can be positioned anywhere.
// ============================================================================

struct GlyphEntry
{
    float uvMinX = 0, uvMinY = 0, uvMaxX = 0, uvMaxY = 0;
    float offsetX = 0, offsetY = 0; // bearing, pixels, relative to draw origin
    float width = 0, height = 0;    // glyph bitmap size, pixels
};

// ============================================================================
// ShelfPacker — simple shelf (row-based) bin packer.
//
// Good fit for text: within one font/size, glyph heights cluster tightly,
// so shelves pack efficiently without needing a general rect-packer.
// No eviction — if the atlas fills, insert() returns false and the caller
// decides (grow + repack, or skip caching that glyph for this frame).
// ============================================================================

class ShelfPacker
{
public:
    ShelfPacker(int width, int height) : width_(width), height_(height) {}

    // Returns true and fills outX/outY (pixel origin, top-left) on success.
    bool insert(int w, int h, int &outX, int &outY)
    {
        if (w <= 0 || h <= 0 || w > width_) return false;

        // Try the current shelf first.
        if (cursorX_ + w <= width_ && currentShelfH_ >= h)
        {
            outX = cursorX_;
            outY = shelfY_;
            cursorX_ += w;
            return true;
        }

        // Start a new shelf below the tallest glyph seen on the current one.
        int newShelfY = shelfY_ + currentShelfH_;
        if (newShelfY + h > height_) return false; // atlas full

        shelfY_ = newShelfY;
        currentShelfH_ = h;
        cursorX_ = w;
        outX = 0;
        outY = shelfY_;
        return true;
    }

    void reset()
    {
        cursorX_ = 0;
        shelfY_ = 0;
        currentShelfH_ = 0;
    }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_, height_;
    int cursorX_ = 0;
    int shelfY_ = 0;
    int currentShelfH_ = 0;
};

// ============================================================================
// GlyphAtlas — owns the Metal texture, CPU staging, packer, and cache.
//
// One per MacState (per-window). Lifetime tied to the Metal device, same as
// uiMetalLayer/commandQueue.
// ============================================================================

class GlyphAtlas
{
public:
    static constexpr int kDefaultSize = 2048;

    bool init(id<MTLDevice> device, int size = kDefaultSize)
    {
        device_ = device;
        packer_ = ShelfPacker(size, size);

        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                          width:size height:size mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        texture_ = [device newTextureWithDescriptor:td];
        return texture_ != nil;
    }

    // Returns nullptr if not cached AND insertion failed (atlas full).
    // Caller (draw loop) should treat that as "skip this glyph" rather
    // than crash — a dropped glyph is a visible-but-recoverable bug,
    // not a fatal one.
    const GlyphEntry *getOrInsert(CTFontRef font, CGGlyph glyph)
    {
        GlyphKey key{font, glyph};
        auto it = cache_.find(key);
        if (it != cache_.end())
            return &it->second;

        GlyphEntry entry;
        if (!rasterizeAndPack(font, glyph, entry))
            return nullptr; // atlas full or empty glyph (e.g. space) — see note below

        auto [insertedIt, ok] = cache_.emplace(key, entry);
        return &insertedIt->second;
    }

    id<MTLTexture> texture() const { return texture_; }

    // Stats for the "atlas full" case — lets the caller log/decide on
    // growing in a future pass rather than silently dropping glyphs forever.
    bool isFull() const { return full_; }

private:
    bool rasterizeAndPack(CTFontRef font, CGGlyph glyph, GlyphEntry &outEntry)
    {
        CGRect bbox = CTFontGetBoundingRectsForGlyphs(
            font, kCTFontOrientationDefault, &glyph, nullptr, 1);

        // Whitespace / zero-area glyphs (e.g. space) are valid and common —
        // cache them as a zero-size entry so the draw loop still advances
        // the cursor correctly without sampling the atlas at all.
        int w = (int)ceil(bbox.size.width);
        int h = (int)ceil(bbox.size.height);
        if (w <= 0 || h <= 0)
        {
            outEntry = GlyphEntry{}; // all zero — draw loop skips the quad
            return true;
        }

        // 1px padding on each side avoids bilinear sampling bleed between
        // adjacent packed glyphs.
        int padW = w + 2, padH = h + 2;

        int px, py;
        if (!packer_.insert(padW, padH, px, py))
        {
            full_ = true;
            return false;
        }

        // Rasterize this one glyph via CG into a small temp buffer (alpha-only).
        std::vector<uint8_t> buf((size_t)padW * padH, 0);
        CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
        CGContextRef cg = CGBitmapContextCreate(
            buf.data(), padW, padH, 8, padW, gray, kCGImageAlphaOnly);
        CGColorSpaceRelease(gray);
        if (!cg) return false;

        // Flip + offset so the glyph's bbox-min lands at (1,1) inside the
        // padded cell, baseline-relative.
        CGContextTranslateCTM(cg, 1 - bbox.origin.x, 1 - bbox.origin.y);
        CGContextSetGrayFillColor(cg, 1.0, 1.0);
        CGPoint pt = CGPointZero;
        CTFontDrawGlyphs(font, &glyph, &pt, 1, cg);
        CGContextRelease(cg);

        [texture_ replaceRegion:MTLRegionMake2D(px, py, padW, padH)
                     mipmapLevel:0 withBytes:buf.data() bytesPerRow:padW];

        float atlasSize = (float)packer_.width();
        outEntry.uvMinX = (px + 1) / atlasSize;
        outEntry.uvMinY = (py + 1) / atlasSize;
        outEntry.uvMaxX = (px + 1 + w) / atlasSize;
        outEntry.uvMaxY = (py + 1 + h) / atlasSize;
        outEntry.offsetX = bbox.origin.x;
        outEntry.offsetY = bbox.origin.y;
        outEntry.width = (float)w;
        outEntry.height = (float)h;
        return true;
    }

    id<MTLDevice> device_ = nil;
    id<MTLTexture> texture_ = nil;
    ShelfPacker packer_{kDefaultSize, kDefaultSize};
    std::unordered_map<GlyphKey, GlyphEntry, GlyphKeyHash> cache_;
    bool full_ = false;
};

#endif
#endif