// flux_painter_android.cpp
#ifdef __ANDROID__

#include "flux/flux_painter.hpp"
#include "nanovg.h"
#include "nanovg_gl.h"

static std::string wstringToUtf8(const std::wstring &text)
{
    std::string utf8;
    utf8.reserve(text.size() * 4);
    for (wchar_t wc : text)
    {
        uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80)
        {
            utf8 += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            // 4-byte UTF-8 — needed for MDI v5+ codepoints (U+F0001 range)
            utf8 += static_cast<char>(0xF0 | (cp >> 18));
            utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return utf8;
}

extern float FluxAndroid_getDpiScale();
extern NVGcontext *FluxAndroid_getVG();
#define s_vg (FluxAndroid_getVG())

static NVGcolor toNVG(Color c)
{
    return nvgRGBA(c.r, c.g, c.b, c.a);
}

void Painter::fillRect(int x, int y, int w, int h, Color color)
{
    if (!s_vg)
        return;
    nvgBeginPath(s_vg);
    nvgRect(s_vg, x, y, w, h);
    nvgFillColor(s_vg, toNVG(color));
    nvgFill(s_vg);
}

void Painter::fillRoundedRect(int x, int y, int w, int h,
                              int radius, Color color)
{
    if (!s_vg)
        return;
    nvgBeginPath(s_vg);
    nvgRoundedRect(s_vg, x, y, w, h, radius);
    nvgFillColor(s_vg, toNVG(color));
    nvgFill(s_vg);
}

void Painter::drawBorder(int x, int y, int w, int h, int radius,
                         Color color, int borderWidth)
{
    if (!s_vg)
        return;
    nvgBeginPath(s_vg);
    nvgRoundedRect(s_vg, x + 0.5f, y + 0.5f, w - 1, h - 1, radius);
    nvgStrokeColor(s_vg, toNVG(color));
    nvgStrokeWidth(s_vg, static_cast<float>(borderWidth));
    nvgStroke(s_vg);
}

void Painter::fillRectAlpha(int x, int y, int w, int h, Color color)
{
    fillRect(x, y, w, h, color); // NanoVG handles alpha natively
}

void Painter::drawLine(int x1, int y1, int x2, int y2,
                       Color color, int width)
{
    if (!s_vg)
        return;
    nvgBeginPath(s_vg);
    nvgMoveTo(s_vg, x1, y1);
    nvgLineTo(s_vg, x2, y2);
    nvgStrokeColor(s_vg, toNVG(color));
    nvgStrokeWidth(s_vg, static_cast<float>(width));
    nvgStroke(s_vg);
}

void Painter::drawHLine(int x, int y, int len, Color color, int sw)
{
    drawLine(x, y, x + len, y, color, sw);
}
void Painter::drawVLine(int x, int y, int len, Color color, int sw)
{
    drawLine(x, y, x, y + len, color, sw);
}

void Painter::drawEllipse(int x, int y, int w, int h,
                          Color fill, Color stroke, int strokeWidth)
{
    if (!s_vg)
        return;
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float rx = w * 0.5f, ry = h * 0.5f;
    nvgBeginPath(s_vg);
    nvgEllipse(s_vg, cx, cy, rx, ry);
    nvgFillColor(s_vg, toNVG(fill));
    nvgFill(s_vg);
    if (strokeWidth > 0)
    {
        nvgStrokeColor(s_vg, toNVG(stroke));
        nvgStrokeWidth(s_vg, static_cast<float>(strokeWidth));
        nvgStroke(s_vg);
    }
}

void Painter::drawText(const std::wstring &text, int x, int y, int w, int h,
                       NativeFont fontHandle, Color color, UINT format)
{
    if (!s_vg || text.empty())
        return;

    // ── Apply font state ──────────────────────────────────────────────────────
    auto *f = reinterpret_cast<FluxAndroidFont *>(fontHandle);
    if (f && f->nvgHandle != -1)
    {
        nvgFontFaceId(s_vg, f->nvgHandle);
        nvgFontSize(s_vg, f->size);
    }
    else
    {
        // Fallback — use whatever NanoVG has active
        nvgFontSize(s_vg, 16.f);
    }

    // ── wstring → UTF-8 ───────────────────────────────────────────────────────
    std::string utf8 = wstringToUtf8(text);

    // ── Alignment ─────────────────────────────────────────────────────────────
    int hAlign = (format & DT_CENTER)  ? NVG_ALIGN_CENTER
                 : (format & DT_RIGHT) ? NVG_ALIGN_RIGHT
                                       : NVG_ALIGN_LEFT;
    int vAlign = (format & DT_VCENTER) ? NVG_ALIGN_MIDDLE : NVG_ALIGN_TOP;
    nvgTextAlign(s_vg, hAlign | vAlign);

    float tx = (format & DT_CENTER)  ? x + w * 0.5f
               : (format & DT_RIGHT) ? static_cast<float>(x + w)
                                     : static_cast<float>(x);
    float ty = (format & DT_VCENTER) ? y + h * 0.5f
                                     : static_cast<float>(y);

    nvgFillColor(s_vg, toNVG(color));
    nvgText(s_vg, tx, ty, utf8.c_str(), nullptr);
}

void Painter::drawTextA(const std::string &text, int x, int y, int w, int h,
                        NativeFont font, Color color, UINT format)
{
    drawText(toWideString(text), x, y, w, h, font, color, format);
}

void Painter::measureText(const std::wstring &text, NativeFont fontHandle,
                          int &outW, int &outH)
{
    if (!s_vg || text.empty())
    {
        outW = outH = 0;
        return;
    }

    auto *f = reinterpret_cast<FluxAndroidFont *>(fontHandle);
    if (f && f->nvgHandle != -1)
    {
        nvgFontFaceId(s_vg, f->nvgHandle);
        nvgFontSize(s_vg, f->size);
    }
    else
    {
        nvgFontSize(s_vg, 16.f);
    }

    std::string utf8 = wstringToUtf8(text); // ← was broken before

    float bounds[4] = {};
    nvgTextBounds(s_vg, 0, 0, utf8.c_str(), nullptr, bounds);
    outW = static_cast<int>(bounds[2] - bounds[0]);
    outH = static_cast<int>(bounds[3] - bounds[1]);
}

void Painter::pushClipRect(int x, int y, int w, int h)
{
    if (!s_vg)
        return;
    nvgSave(s_vg);
    nvgScissor(s_vg, x, y, w, h);
}
void Painter::popClipRect()
{
    if (s_vg)
        nvgRestore(s_vg);
}
void Painter::pushClipRoundedRect(int x, int y, int w, int h, int r)
{
    pushClipRect(x, y, w, h); // NanoVG scissor is rect-only; close enough
}

void Painter::fillGradientRect(int x, int y, int w, int h,
                               const std::vector<Color> &colors)
{
    if (colors.empty() || !s_vg)
        return;
    if (colors.size() == 1)
    {
        fillRect(x, y, w, h, colors[0]);
        return;
    }
    // NanoVG supports 2-stop gradients natively
    NVGpaint paint = nvgLinearGradient(s_vg,
                                       static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(x + w), static_cast<float>(y),
                                       toNVG(colors.front()), toNVG(colors.back()));
    nvgBeginPath(s_vg);
    nvgRect(s_vg, x, y, w, h);
    nvgFillPaint(s_vg, paint);
    nvgFill(s_vg);
}

void Painter::drawPolyline(const std::vector<std::pair<int, int>> &pts,
                           Color color, int strokeWidth)
{
    if (!s_vg || pts.size() < 2)
        return;
    nvgBeginPath(s_vg);
    nvgMoveTo(s_vg, pts[0].first, pts[0].second);
    for (size_t i = 1; i < pts.size(); ++i)
        nvgLineTo(s_vg, pts[i].first, pts[i].second);
    nvgStrokeColor(s_vg, toNVG(color));
    nvgStrokeWidth(s_vg, static_cast<float>(strokeWidth));
    nvgStroke(s_vg);
}

void Painter::fillPolygonAlpha(const std::vector<std::pair<int, int>> &pts,
                               Color color)
{
    if (!s_vg || pts.size() < 3)
        return;
    nvgBeginPath(s_vg);
    nvgMoveTo(s_vg, pts[0].first, pts[0].second);
    for (size_t i = 1; i < pts.size(); ++i)
        nvgLineTo(s_vg, pts[i].first, pts[i].second);
    nvgClosePath(s_vg);
    nvgFillColor(s_vg, toNVG(color));
    nvgFill(s_vg);
}

void Painter::fillRoundedRegion(int x, int y, int w, int h,
                                int r, Color color)
{
    fillRoundedRect(x, y, w, h, r, color);
}

void Painter::fillRectWithLeftAccent(int x, int y, int w, int h,
                                     Color bg, Color accent, int strip)
{
    fillRect(x, y, w, h, bg);
    fillRect(x, y, strip, h, accent);
}

void Painter::drawRectOutline(int x, int y, int w, int h,
                              Color color, int sw)
{
    drawBorder(x, y, w, h, 0, color, sw);
}
void Painter::drawRoundedRectOutline(int x, int y, int w, int h,
                                     int r, Color stroke, int sw)
{
    drawBorder(x, y, w, h, r, stroke, sw);
}

void Painter::fillRoundedRectGDI(int x, int y, int w, int h, int r,
                                 Color fill, Color stroke, int sw)
{
    fillRoundedRect(x, y, w, h, r, fill);
    if (sw > 0)
        drawBorder(x, y, w, h, r, stroke, sw);
}

void Painter::fillColumnBars(int x, int y, int w, int h,
                             const std::vector<int> &bars, Color color)
{
    if (!s_vg || bars.empty())
        return;
    int cols = std::min(w, (int)bars.size());
    for (int i = 0; i < cols; ++i)
    {
        int bh = std::max(0, std::min(h, bars[i]));
        fillRect(x + i, y + h - bh, 1, bh, color);
    }
}

// =============================================================================
// RICH TEXT IMPLEMENTATION  (Android / NanoVG)
// =============================================================================

void Painter::drawWavyLine(int x, int y, int len, Color color, int amplitude)
{
    if (len <= 0)
        return;
    const int step = amplitude * 2;
    std::vector<std::pair<int, int>> pts;
    pts.reserve(len / step + 2);
    int px = x;
    bool up = true;
    while (px < x + len)
    {
        pts.push_back({px, up ? y - amplitude : y + amplitude});
        px += step;
        up = !up;
    }
    pts.push_back({x + len, y});
    if (pts.size() >= 2)
        drawPolyline(pts, color, 1);
}

void Painter::drawFadeOverlay(int x, int y, int w, int h,
                              int fadeWidth, Color bg)
{
    if (fadeWidth <= 0 || w <= 0 || h <= 0)
        return;
    int startX = x + w - fadeWidth;
    if (startX < x)
        startX = x;
    std::vector<Color> stops = {bg.withAlpha(0), bg.withAlpha(255)};
    fillGradientRect(startX, y, fadeWidth, h, stops);
}

void Painter::drawTextDecorationLine(int lineX, int lineY, int lineW,
                                     const TextStyle &style,
                                     TextDecoration which)
{
    if (lineW <= 0)
        return;

    int fontSize = style.scaledFontSize();
    int ascent = static_cast<int>(fontSize * 0.75f);
    int thickness = style.decorationThickness;
    Color dc = style.decorationColor;

    int decorY = lineY;
    if (which == TextDecoration::Underline)
        decorY = lineY + ascent + 1;
    else if (which == TextDecoration::Overline)
        decorY = lineY;
    else if (which == TextDecoration::LineThrough)
        decorY = lineY + ascent - ascent / 3;

    switch (style.decorationStyle)
    {
    case TextDecorationStyle::Solid:
        drawHLine(lineX, decorY, lineW, dc, thickness);
        break;
    case TextDecorationStyle::Double:
        drawHLine(lineX, decorY, lineW, dc, thickness);
        drawHLine(lineX, decorY + 2, lineW, dc, thickness);
        break;
    case TextDecorationStyle::Dotted:
        for (int px = lineX; px < lineX + lineW; px += 4)
            drawHLine(px, decorY, std::min(2, lineX + lineW - px), dc, thickness);
        break;
    case TextDecorationStyle::Dashed:
        for (int px = lineX; px < lineX + lineW; px += 8)
            drawHLine(px, decorY, std::min(5, lineX + lineW - px), dc, thickness);
        break;
    case TextDecorationStyle::Wavy:
        drawWavyLine(lineX, decorY, lineW, dc, 2);
        break;
    }
}

// ── Internal: measure one NVG text line, returns pixel width ─────────────────
static int measureLineNVG(NVGcontext *vg, const std::string &utf8,
                          int start, int len, float letterSpacing,
                          int &outLineH)
{
    if (!vg || len <= 0)
    {
        outLineH = 16;
        return 0;
    }

    // NVG letter spacing is set as a context property before measuring
    nvgTextLetterSpacing(vg, letterSpacing);

    float bounds[4] = {};
    nvgTextBounds(vg, 0, 0, utf8.c_str() + start,
                  utf8.c_str() + start + len, bounds);

    // Height from NVG font metrics
    float ascender = 0, descender = 0, lineh = 0;
    nvgTextMetrics(vg, &ascender, &descender, &lineh);
    outLineH = static_cast<int>(lineh);

    return static_cast<int>(bounds[2] - bounds[0]);
}

// ── Internal line span for Android/NVG wrapping ───────────────────────────────
struct LineSpanNVG
{
    int start;
    int length;
};

// ── Internal: wrap UTF-8 string into line spans ───────────────────────────────
static std::vector<LineSpanNVG> wrapTextNVG(NVGcontext *vg,
                                            const std::string &utf8,
                                            int maxWidth, bool softWrap,
                                            float letterSpacing)
{
    std::vector<LineSpanNVG> lines;
    const int n = static_cast<int>(utf8.size());
    if (n == 0)
        return lines;

    nvgTextLetterSpacing(vg, letterSpacing);

    int pos = 0;
    while (pos < n)
    {
        int nlPos = pos;
        while (nlPos < n && utf8[nlPos] != '\n')
            ++nlPos;

        if (!softWrap || maxWidth <= 0)
        {
            lines.push_back({pos, nlPos - pos});
        }
        else
        {
            int lineStart = pos;
            while (lineStart < nlPos)
            {
                int lo = 0, hi = nlPos - lineStart, fit = 0;
                while (lo <= hi)
                {
                    int mid = (lo + hi) / 2;
                    int dummy;
                    int w = measureLineNVG(vg, utf8, lineStart, mid,
                                           letterSpacing, dummy);
                    if (w <= maxWidth)
                    {
                        fit = mid;
                        lo = mid + 1;
                    }
                    else
                    {
                        hi = mid - 1;
                    }
                }
                if (fit == 0)
                    fit = 1;

                int breakAt = fit;
                if (lineStart + fit < nlPos)
                {
                    int wb = fit;
                    while (wb > 1 && utf8[lineStart + wb - 1] != ' ')
                        --wb;
                    if (wb > 1)
                        breakAt = wb;
                }

                lines.push_back({lineStart, breakAt});
                lineStart += breakAt;
                while (lineStart < nlPos && utf8[lineStart] == ' ')
                    ++lineStart;
            }
        }
        pos = nlPos + 1;
        if (nlPos == n)
            break;
    }
    return lines;
}

void Painter::measureRichText(const std::wstring &text,
                              const TextStyle &style,
                              FontCache &fontCache,
                              int maxWidth, bool softWrap, int maxLines,
                              int &outWidth, int &outHeight)
{
    outWidth = outHeight = 0;
    if (!s_vg || text.empty())
        return;

    NativeFont font = fontCache.getFont(style.fontFamily,
                                        style.scaledFontSize(),
                                        style.fontWeight);
    auto *f = reinterpret_cast<FluxAndroidFont *>(font);
    if (f && f->nvgHandle != -1)
        nvgFontFaceId(s_vg, f->nvgHandle);
    nvgFontSize(s_vg, f ? f->size : style.scaledFontSize());

    std::string utf8 = wstringToUtf8(text);
    auto lines = wrapTextNVG(s_vg, utf8, maxWidth, softWrap, style.letterSpacing);

    int lineH = 0;
    {
        int lineHDummy = 0;
        measureLineNVG(s_vg, utf8, 0,
                       lines.empty() ? 0 : lines[0].length,
                       style.letterSpacing, lineHDummy);
        lineH = lineHDummy;
    }
    if (lineH == 0)
        lineH = style.scaledFontSize();
    int lineHeightPx = static_cast<int>(lineH * style.height);

    int totalLines = (maxLines > 0)
                         ? std::min((int)lines.size(), maxLines)
                         : (int)lines.size();

    for (int i = 0; i < totalLines; ++i)
    {
        int dummy;
        int w = measureLineNVG(s_vg, utf8,
                               lines[i].start, lines[i].length,
                               style.letterSpacing, dummy);
        outWidth = std::max(outWidth, w);
    }
    outHeight = totalLines * lineHeightPx;
}

void Painter::drawRichText(const std::wstring &text,
                           const RichTextParams &params,
                           FontCache &fontCache)
{
    if (!s_vg || text.empty() || params.w <= 0 || params.h <= 0)
        return;

    const TextStyle &style = params.style;
    NativeFont font = fontCache.getFont(style.fontFamily,
                                        style.scaledFontSize(),
                                        style.fontWeight);
    auto *f = reinterpret_cast<FluxAndroidFont *>(font);
    if (f && f->nvgHandle != -1)
        nvgFontFaceId(s_vg, f->nvgHandle);
    nvgFontSize(s_vg, f ? f->size : style.scaledFontSize());
    nvgTextLetterSpacing(s_vg, style.letterSpacing);

    std::string utf8 = wstringToUtf8(text);

    int wrapWidth = params.softWrap ? params.w : 0;
    auto lines = wrapTextNVG(s_vg, utf8, wrapWidth,
                             params.softWrap, style.letterSpacing);

    int lineH = 0;
    {
        int dummy;
        measureLineNVG(s_vg, utf8, 0,
                       lines.empty() ? 0 : lines[0].length,
                       style.letterSpacing, lineH);
        if (lineH == 0)
            lineH = style.scaledFontSize();
    }
    int lineHeightPx = static_cast<int>(lineH * style.height);

    int totalLines = (params.maxLines > 0)
                         ? std::min((int)lines.size(), params.maxLines)
                         : (int)lines.size();

    int blockH = totalLines * lineHeightPx;
    int startY = params.y;
    switch (params.textAlignVertical)
    {
    case TextAlignVertical::Center:
        startY = params.y + (params.h - blockH) / 2;
        break;
    case TextAlignVertical::Bottom:
        startY = params.y + params.h - blockH;
        break;
    default:
        break;
    }

    bool needClip = (params.overflow != TextOverflow::Visible);
    if (needClip)
        pushClipRect(params.x, params.y, params.w, params.h);

    for (int i = 0; i < totalLines; ++i)
    {
        const auto &span = lines[i];
        int lineY = startY + i * lineHeightPx;

        if (lineY + lineHeightPx < params.y)
            continue;
        if (lineY > params.y + params.h)
            break;

        int dummy;
        int lineW = measureLineNVG(s_vg, utf8,
                                   span.start, span.length,
                                   style.letterSpacing, dummy);

        int lineX = params.x;
        bool isRTL = (params.direction == TextDirection::RTL);
        switch (params.textAlign)
        {
        case TextAlign::Right:
        case TextAlign::End:
            lineX = isRTL ? params.x : (params.x + params.w - lineW);
            break;
        case TextAlign::Center:
            lineX = params.x + (params.w - lineW) / 2;
            break;
        case TextAlign::Left:
        case TextAlign::Start:
        default:
            lineX = isRTL ? (params.x + params.w - lineW) : params.x;
            break;
        }

        bool isLastVisible = (i == totalLines - 1);
        bool hasMoreLines = ((int)lines.size() > totalLines) ||
                            (totalLines == 1 && lineW > params.w);

        // Per-run background
        if (style.backgroundColor.has_value())
            fillRect(lineX, lineY, lineW, lineHeightPx, *style.backgroundColor);

        // Shadows
        for (const auto &sh : style.shadows)
        {
            nvgFillColor(s_vg, toNVG(sh.color));
            nvgTextAlign(s_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgText(s_vg,
                    static_cast<float>(lineX + sh.offsetX),
                    static_cast<float>(lineY + sh.offsetY),
                    utf8.c_str() + span.start,
                    utf8.c_str() + span.start + span.length);
        }

        // Ellipsis
        if (isLastVisible && hasMoreLines &&
            params.overflow == TextOverflow::Ellipsis)
        {
            // NanoVG has no built-in ellipsis; manually truncate + append "…"
            std::string lineText(utf8.c_str() + span.start, span.length);
            while (!lineText.empty())
            {
                float bounds[4] = {};
                std::string candidate = lineText + "\xe2\x80\xa6"; // UTF-8 ellipsis
                nvgTextBounds(s_vg, 0, 0,
                              candidate.c_str(), nullptr, bounds);
                if (bounds[2] - bounds[0] <= params.w || lineText.size() == 1)
                    break;
                lineText.pop_back();
                // Pop back until valid UTF-8 boundary
                while (!lineText.empty() &&
                       (static_cast<unsigned char>(lineText.back()) & 0xC0) == 0x80)
                    lineText.pop_back();
            }
            lineText += "\xe2\x80\xa6";

            nvgFillColor(s_vg, toNVG(style.color));
            nvgTextAlign(s_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgText(s_vg,
                    static_cast<float>(lineX),
                    static_cast<float>(lineY),
                    lineText.c_str(), nullptr);

            if (style.hasOverline())
                drawTextDecorationLine(lineX, lineY, lineW, style, TextDecoration::Overline);
            if (style.hasUnderline())
                drawTextDecorationLine(lineX, lineY, lineW, style, TextDecoration::Underline);
            if (style.hasLineThrough())
                drawTextDecorationLine(lineX, lineY, lineW, style, TextDecoration::LineThrough);
            continue;
        }

        // Normal draw
        nvgFillColor(s_vg, toNVG(style.color));
        nvgTextAlign(s_vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgText(s_vg,
                static_cast<float>(lineX),
                static_cast<float>(lineY),
                utf8.c_str() + span.start,
                utf8.c_str() + span.start + span.length);

        // Decorations
        if (style.hasOverline())
            drawTextDecorationLine(lineX, lineY, lineW, style, TextDecoration::Overline);
        if (style.hasUnderline())
            drawTextDecorationLine(lineX, lineY, lineW, style, TextDecoration::Underline);
        if (style.hasLineThrough())
            drawTextDecorationLine(lineX, lineY, lineW, style, TextDecoration::LineThrough);

        // Fade overlay
        if (isLastVisible && hasMoreLines &&
            params.overflow == TextOverflow::Fade)
        {
            Color fadeBg = Color::fromRGB(255, 255, 255);
            int fadeW = std::min(60, params.w / 3);
            drawFadeOverlay(params.x, lineY, params.w, lineHeightPx, fadeW, fadeBg);
        }
    }

    if (needClip)
        popClipRect();
}

void Painter::drawRichTextA(const std::string &text,
                            const RichTextParams &params,
                            FontCache &fontCache)
{
    if (text.empty())
        return;
    std::wstring wide;
    wide.reserve(text.size());
    for (unsigned char c : text)
        wide += static_cast<wchar_t>(c);
    drawRichText(wide, params, fontCache);
}

void Painter::drawArc(float cx, float cy, float radius,
                      int strokeWidth,
                      float startAngle, float sweepAngle,
                      Color color, bool roundedCaps)
{
    if (!s_vg)
        return;

    // NanoVG has no native arc-stroke primitive, so we build the path
    // manually and stroke it.
    nvgBeginPath(s_vg);
    nvgArc(s_vg, cx, cy, radius,
           startAngle, startAngle + sweepAngle,
           NVG_CW);

    nvgStrokeColor(s_vg, toNVG(color));
    nvgStrokeWidth(s_vg, static_cast<float>(strokeWidth));
    nvgLineCap(s_vg, roundedCaps ? NVG_ROUND : NVG_BUTT);
    nvgStroke(s_vg);
}

// ============================================================================
// Painter::drawImage
// ============================================================================

void Painter::drawImage(const ImageDrawParams &params)
{
    if (!s_vg || params.image == -1 || params.clipW <= 0 || params.clipH <= 0)
        return;

    nvgSave(s_vg);
    nvgScissor(s_vg, (float)params.clipX, (float)params.clipY,
               (float)params.clipW, (float)params.clipH);

    if (params.repeat != ImageRepeat::NoRepeat)
    {
        float tileW = params.destW, tileH = params.destH;
        float startX = (params.repeat == ImageRepeat::RepeatY) ? params.destX : (float)params.clipX;
        float startY = (params.repeat == ImageRepeat::RepeatX) ? params.destY : (float)params.clipY;
        float endX = (params.repeat == ImageRepeat::RepeatY) ? params.destX + tileW : (float)(params.clipX + params.clipW);
        float endY = (params.repeat == ImageRepeat::RepeatX) ? params.destY + tileH : (float)(params.clipY + params.clipH);

        for (float ty = startY; ty < endY; ty += tileH)
        {
            for (float tx = startX; tx < endX; tx += tileW)
            {
                NVGpaint p = nvgImagePattern(s_vg, tx, ty, tileW, tileH,
                                             0.0f, params.image, 1.0f);
                nvgBeginPath(s_vg);
                nvgRect(s_vg, tx, ty, tileW, tileH);
                nvgFillPaint(s_vg, p);
                nvgFill(s_vg);
            }
        }
    }
    else
    {
        NVGpaint paint = nvgImagePattern(s_vg, params.destX, params.destY,
                                         params.destW, params.destH,
                                         0.0f, params.image, 1.0f);
        nvgBeginPath(s_vg);
        if (params.borderRadius > 0)
            nvgRoundedRect(s_vg, (float)params.clipX, (float)params.clipY,
                           (float)params.clipW, (float)params.clipH,
                           (float)params.borderRadius);
        else
            nvgRect(s_vg, (float)params.clipX, (float)params.clipY,
                    (float)params.clipW, (float)params.clipH);
        nvgFillPaint(s_vg, paint);
        nvgFill(s_vg);
    }

    nvgRestore(s_vg);
}

void Painter::drawVideo(const VideoDrawParams& params)
{
    if (!s_vg || params.frame == -1 || params.dstW <= 0) return;
    NVGpaint paint = nvgImagePattern(s_vg,
        params.dstX, params.dstY,
        params.dstW, params.dstH,
        0.f, params.frame, 1.f);
    nvgBeginPath(s_vg);
    nvgRect(s_vg, params.dstX, params.dstY, params.dstW, params.dstH);
    nvgFillPaint(s_vg, paint);
    nvgFill(s_vg);
}

#endif // __ANDROID__