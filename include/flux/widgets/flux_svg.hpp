// include/flux/flux_svg.hpp
#ifndef FLUX_SVG_HPP
#define FLUX_SVG_HPP

#include "flux/flux_widget.hpp"
#include "flux/flux_painter.hpp"

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
#include "flux/flux_dom_adapter.hpp"
#endif


#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <climits>

// ============================================================================
// SvgWidget — minimal inline-SVG shape renderer.
//
// Parses a practical subset of SVG 1.1 (see file header comment in the
// response) into a flat list of flattened, transformed shapes, then paints
// them with the SAME cross-platform Painter primitives every other widget
// uses (fillPolygonAlpha / drawPolyline / pushClipRect) — no new
// platform-specific Painter code required.
//
// Known limitation: Painter::fillPolygonAlpha is a documented no-op stub
// on the DOM web renderer (flux_painter_dom.cpp) — filled shapes won't
// appear there today; strokes (drawPolyline) still render fine everywhere
// it's implemented. Not something this widget can fix on its own.
// ============================================================================

// ── Minimal affine transform (SVG "transform" attribute) ───────────────────
struct SvgMat
{
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    static SvgMat identity() { return {}; }
    static SvgMat translate(float tx, float ty) { return {1, 0, 0, 1, tx, ty}; }
    static SvgMat scaleXY(float sx, float sy) { return {sx, 0, 0, sy, 0, 0}; }
    static SvgMat rotateDeg(float deg)
    {
        float r = deg * 3.14159265358979f / 180.f;
        float cs = std::cos(r), sn = std::sin(r);
        return {cs, sn, -sn, cs, 0, 0};
    }

    // this ∘ other — apply `other` first, then `this` (matches SVG's
    // left-to-right "transform" list composition order).
    SvgMat operator*(const SvgMat &o) const
    {
        return {
            a * o.a + c * o.b,
            b * o.a + d * o.b,
            a * o.c + c * o.d,
            b * o.c + d * o.d,
            a * o.e + c * o.f + e,
            b * o.e + d * o.f + f};
    }

    std::pair<float, float> apply(float x, float y) const
    {
        return {a * x + c * y + e, b * x + d * y + f};
    }
};

namespace flux_svg_detail
{

    inline int hexVal(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return 0;
    }

    inline bool parseSvgColor(std::string s, Color &out)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        if (b == std::string::npos)
            return false;
        s = s.substr(b, e - b + 1);

        if (s == "none" || s.empty())
            return false;
        if (s == "transparent")
        {
            out = Color::fromRGBA(0, 0, 0, 0);
            return true;
        }

        if (s[0] == '#')
        {
            std::string h = s.substr(1);
            if (h.size() == 3)
            {
                out = Color::fromRGB(hexVal(h[0]) * 17, hexVal(h[1]) * 17, hexVal(h[2]) * 17);
                return true;
            }
            if (h.size() == 6)
            {
                int r = hexVal(h[0]) * 16 + hexVal(h[1]);
                int g = hexVal(h[2]) * 16 + hexVal(h[3]);
                int bch = hexVal(h[4]) * 16 + hexVal(h[5]);
                out = Color::fromRGB(r, g, bch);
                return true;
            }
            if (h.size() == 8)
            {
                int r = hexVal(h[0]) * 16 + hexVal(h[1]);
                int g = hexVal(h[2]) * 16 + hexVal(h[3]);
                int bch = hexVal(h[4]) * 16 + hexVal(h[5]);
                int al = hexVal(h[6]) * 16 + hexVal(h[7]);
                out = Color::fromRGBA(r, g, bch, al);
                return true;
            }
            return false;
        }

        if (s.rfind("rgb(", 0) == 0 || s.rfind("rgba(", 0) == 0)
        {
            size_t op = s.find('('), cp = s.find(')');
            if (op == std::string::npos || cp == std::string::npos)
                return false;
            std::string inner = s.substr(op + 1, cp - op - 1);
            std::vector<float> vals;
            std::stringstream ss(inner);
            std::string tok;
            while (std::getline(ss, tok, ','))
                vals.push_back((float)std::atof(tok.c_str()));
            if (vals.size() < 3)
                return false;
            int a = (vals.size() >= 4) ? (int)std::round(vals[3] * 255.f) : 255;
            out = Color::fromRGBA((int)vals[0], (int)vals[1], (int)vals[2], a);
            return true;
        }

        static const std::unordered_map<std::string, Color> kNamed = {
            {"black", Color::fromRGB(0, 0, 0)},
            {"white", Color::fromRGB(255, 255, 255)},
            {"red", Color::fromRGB(255, 0, 0)},
            {"green", Color::fromRGB(0, 128, 0)},
            {"blue", Color::fromRGB(0, 0, 255)},
            {"yellow", Color::fromRGB(255, 255, 0)},
            {"gray", Color::fromRGB(128, 128, 128)},
            {"grey", Color::fromRGB(128, 128, 128)},
            {"orange", Color::fromRGB(255, 165, 0)},
            {"purple", Color::fromRGB(128, 0, 128)},
            {"pink", Color::fromRGB(255, 192, 203)},
            {"brown", Color::fromRGB(165, 42, 42)},
            {"cyan", Color::fromRGB(0, 255, 255)},
            {"magenta", Color::fromRGB(255, 0, 255)},
            {"lime", Color::fromRGB(0, 255, 0)},
            {"navy", Color::fromRGB(0, 0, 128)},
            {"teal", Color::fromRGB(0, 128, 128)},
            {"silver", Color::fromRGB(192, 192, 192)},
            {"maroon", Color::fromRGB(128, 0, 0)},
            {"olive", Color::fromRGB(128, 128, 0)},
            {"currentColor", Color::fromRGB(0, 0, 0)},
        };
        auto it = kNamed.find(s);
        if (it != kNamed.end())
        {
            out = it->second;
            return true;
        }
        return false;
    }

    inline bool scanNumber(const std::string &s, size_t &i, float &outVal)
    {
        size_t n = s.size();
        while (i < n && (s[i] == ' ' || s[i] == ',' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
        if (i >= n)
            return false;
        size_t start = i;
        if (s[i] == '+' || s[i] == '-')
            ++i;
        bool sawDigit = false;
        while (i < n && std::isdigit((unsigned char)s[i]))
        {
            ++i;
            sawDigit = true;
        }
        if (i < n && s[i] == '.')
        {
            ++i;
            while (i < n && std::isdigit((unsigned char)s[i]))
            {
                ++i;
                sawDigit = true;
            }
        }
        if (i < n && (s[i] == 'e' || s[i] == 'E'))
        {
            size_t save = i;
            ++i;
            if (i < n && (s[i] == '+' || s[i] == '-'))
                ++i;
            bool expDigit = false;
            while (i < n && std::isdigit((unsigned char)s[i]))
            {
                ++i;
                expDigit = true;
            }
            if (!expDigit)
                i = save;
        }
        if (!sawDigit)
        {
            i = start;
            return false;
        }
        outVal = (float)std::atof(s.substr(start, i - start).c_str());
        return true;
    }

    inline std::vector<float> parseNumberList(const std::string &s)
    {
        std::vector<float> out;
        size_t i = 0;
        float v;
        while (scanNumber(s, i, v))
            out.push_back(v);
        return out;
    }

    inline SvgMat parseTransform(const std::string &s)
    {
        SvgMat m = SvgMat::identity();
        size_t i = 0, n = s.size();
        while (i < n)
        {
            while (i < n && (std::isspace((unsigned char)s[i]) || s[i] == ','))
                ++i;
            if (i >= n)
                break;
            size_t nameStart = i;
            while (i < n && std::isalpha((unsigned char)s[i]))
                ++i;
            std::string name = s.substr(nameStart, i - nameStart);
            while (i < n && std::isspace((unsigned char)s[i]))
                ++i;
            if (i >= n || s[i] != '(')
                break;
            size_t close = s.find(')', i);
            if (close == std::string::npos)
                break;
            auto nums = parseNumberList(s.substr(i + 1, close - i - 1));
            i = close + 1;

            if (name == "translate")
            {
                m = m * SvgMat::translate(nums.size() > 0 ? nums[0] : 0.f,
                                          nums.size() > 1 ? nums[1] : 0.f);
            }
            else if (name == "scale")
            {
                float sx = nums.size() > 0 ? nums[0] : 1.f;
                m = m * SvgMat::scaleXY(sx, nums.size() > 1 ? nums[1] : sx);
            }
            else if (name == "rotate")
            {
                float deg = nums.size() > 0 ? nums[0] : 0.f;
                if (nums.size() >= 3)
                {
                    float cxp = nums[1], cyp = nums[2];
                    m = m * SvgMat::translate(cxp, cyp) * SvgMat::rotateDeg(deg) *
                        SvgMat::translate(-cxp, -cyp);
                }
                else
                {
                    m = m * SvgMat::rotateDeg(deg);
                }
            }
            else if (name == "matrix" && nums.size() >= 6)
            {
                m = m * SvgMat{nums[0], nums[1], nums[2], nums[3], nums[4], nums[5]};
            }
            // skewX/skewY intentionally unsupported — rare in icon-set SVGs
        }
        return m;
    }

    inline std::unordered_map<std::string, std::string> parseAttributes(const std::string &tag)
    {
        std::unordered_map<std::string, std::string> attrs;
        size_t i = 0, n = tag.size();
        while (i < n)
        {
            while (i < n && std::isspace((unsigned char)tag[i]))
                ++i;
            size_t nameStart = i;
            while (i < n && tag[i] != '=' && !std::isspace((unsigned char)tag[i]) && tag[i] != '/')
                ++i;
            if (i == nameStart)
            {
                ++i;
                continue;
            }
            std::string key = tag.substr(nameStart, i - nameStart);
            while (i < n && std::isspace((unsigned char)tag[i]))
                ++i;
            if (i >= n || tag[i] != '=')
                continue;
            ++i;
            while (i < n && std::isspace((unsigned char)tag[i]))
                ++i;
            if (i >= n)
                break;
            char quote = tag[i];
            std::string val;
            if (quote == '"' || quote == '\'')
            {
                ++i;
                size_t valStart = i;
                while (i < n && tag[i] != quote)
                    ++i;
                val = tag.substr(valStart, i - valStart);
                if (i < n)
                    ++i;
            }
            else
            {
                size_t valStart = i;
                while (i < n && !std::isspace((unsigned char)tag[i]))
                    ++i;
                val = tag.substr(valStart, i - valStart);
            }
            attrs[key] = val;
        }
        return attrs;
    }

    inline void flattenCubic(std::vector<std::pair<float, float>> &out,
                             float x0, float y0, float x1, float y1,
                             float x2, float y2, float x3, float y3, int segments = 16)
    {
        for (int i = 1; i <= segments; ++i)
        {
            float t = (float)i / segments, mt = 1 - t;
            out.push_back({mt * mt * mt * x0 + 3 * mt * mt * t * x1 + 3 * mt * t * t * x2 + t * t * t * x3,
                           mt * mt * mt * y0 + 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t * y3});
        }
    }

    inline void flattenQuad(std::vector<std::pair<float, float>> &out,
                            float x0, float y0, float x1, float y1,
                            float x2, float y2, int segments = 12)
    {
        for (int i = 1; i <= segments; ++i)
        {
            float t = (float)i / segments, mt = 1 - t;
            out.push_back({mt * mt * x0 + 2 * mt * t * x1 + t * t * x2, mt * mt * y0 + 2 * mt * t * y1 + t * t * y2});
        }
    }

    // Endpoint-to-center elliptical-arc flattening (SVG "A" command).
    inline void flattenArc(std::vector<std::pair<float, float>> &out,
                           float x0, float y0, float rx, float ry, float xRotDeg,
                           bool largeArc, bool sweep, float x1, float y1, int segments = 24)
    {
        if (rx == 0.f || ry == 0.f)
        {
            out.push_back({x1, y1});
            return;
        }
        rx = std::fabs(rx);
        ry = std::fabs(ry);
        float phi = xRotDeg * 3.14159265358979f / 180.f;
        float cosPhi = std::cos(phi), sinPhi = std::sin(phi);

        float dx2 = (x0 - x1) * 0.5f, dy2 = (y0 - y1) * 0.5f;
        float x1p = cosPhi * dx2 + sinPhi * dy2;
        float y1p = -sinPhi * dx2 + cosPhi * dy2;

        float rxSq = rx * rx, rySq = ry * ry, x1pSq = x1p * x1p, y1pSq = y1p * y1p;
        float radiiCheck = x1pSq / rxSq + y1pSq / rySq;
        if (radiiCheck > 1.f)
        {
            float s = std::sqrt(radiiCheck);
            rx *= s;
            ry *= s;
            rxSq = rx * rx;
            rySq = ry * ry;
        }

        float sign = (largeArc != sweep) ? 1.f : -1.f;
        float num = rxSq * rySq - rxSq * y1pSq - rySq * x1pSq;
        float den = rxSq * y1pSq + rySq * x1pSq;
        float coef = (den != 0.f) ? sign * std::sqrt(std::max(0.f, num / den)) : 0.f;
        float cxp = coef * (rx * y1p / ry);
        float cyp = coef * -(ry * x1p / rx);
        float cx = cosPhi * cxp - sinPhi * cyp + (x0 + x1) * 0.5f;
        float cy = sinPhi * cxp + cosPhi * cyp + (y0 + y1) * 0.5f;

        auto angle = [](float ux, float uy, float vx, float vy)
        {
            float dot = ux * vx + uy * vy;
            float len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
            float a = std::acos(std::max(-1.f, std::min(1.f, dot / len)));
            return (ux * vy - uy * vx < 0.f) ? -a : a;
        };

        float theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
        float dTheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
        const float twoPi = 2.f * 3.14159265358979f;
        if (!sweep && dTheta > 0)
            dTheta -= twoPi;
        if (sweep && dTheta < 0)
            dTheta += twoPi;

        for (int i = 1; i <= segments; ++i)
        {
            float t = theta1 + dTheta * ((float)i / segments);
            out.push_back({cx + rx * std::cos(t) * cosPhi - ry * std::sin(t) * sinPhi,
                           cy + rx * std::cos(t) * sinPhi + ry * std::sin(t) * cosPhi});
        }
    }

    // Merges the CSS `style="k:v;k:v"` attribute into the plain attribute map,
    // for the presentation properties SVG exporters commonly put there instead
    // of as separate XML attributes. style="" wins over the plain attribute,
    // matching normal CSS cascade rules — so this must run BEFORE
    // applySvgStyleAttrs() reads fill/stroke/etc.
    inline void mergeStyleAttribute(std::unordered_map<std::string, std::string> &attrs)
    {
        auto it = attrs.find("style");
        if (it == attrs.end())
            return;
        std::stringstream ss(it->second);
        std::string decl;
        auto trim = [](std::string &s)
        {
            size_t b = s.find_first_not_of(" \t\r\n");
            size_t e = s.find_last_not_of(" \t\r\n");
            s = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
        };
        while (std::getline(ss, decl, ';'))
        {
            size_t colon = decl.find(':');
            if (colon == std::string::npos)
                continue;
            std::string key = decl.substr(0, colon), val = decl.substr(colon + 1);
            trim(key);
            trim(val);
            if (key == "fill" || key == "stroke" || key == "stroke-width" ||
                key == "opacity" || key == "fill-opacity" || key == "stroke-opacity" ||
                key == "fill-rule" || key == "transform")
            {
                attrs[key] = val;
            }
        }
    }

    enum class SvgFillRule
    {
        NonZero,
        EvenOdd
    };

    // Fills a shape's ENTIRE set of subpaths together via anti-aliased
    // horizontal-scanline rasterization, honoring the fill-rule — the only
    // way to correctly render SVG "donut" shapes (eye cutouts, letter
    // counters) since Painter has no native multi-contour/hole-aware fill
    // primitive. Supersamples vertically (kSubSamples rows per pixel) and
    // computes exact horizontal coverage per pixel so curved/diagonal edges
    // are blended instead of hard-snapped to a pixel column — this is what
    // was previously producing visibly jagged/pixelated shapes. Interior
    // full-coverage runs are still merged into a single opaque fillRect for
    // performance; only edge pixels pay the per-pixel alpha-blend cost.
    // Takes FLOAT device coordinates (not rounded to int like the old
    // version) so sub-pixel precision survives into the coverage calc.
    inline void fillShapeScanline(Painter &painter,
                                  const std::vector<std::vector<std::pair<float, float>>> &devicePolys,
                                  SvgFillRule rule, Color color)
    {
        struct Edge
        {
            double x0, y0, x1, y1;
            int dir;
        };
        std::vector<Edge> edges;
        double minYf = 1e30, maxYf = -1e30, minXf = 1e30, maxXf = -1e30;

        for (auto &poly : devicePolys)
        {
            size_t n = poly.size();
            if (n < 3)
                continue;
            for (size_t i = 0; i < n; ++i)
            {
                auto [x0, y0] = poly[i];
                auto [x1, y1] = poly[(i + 1) % n];
                if (y0 == y1)
                    continue; // horizontal edges never cross a scanline
                edges.push_back({(double)x0, (double)y0, (double)x1, (double)y1, (y1 > y0) ? 1 : -1});
                minYf = std::min({minYf, (double)y0, (double)y1});
                maxYf = std::max({maxYf, (double)y0, (double)y1});
                minXf = std::min({minXf, (double)x0, (double)x1});
                maxXf = std::max({maxXf, (double)x0, (double)x1});
            }
        }
        if (edges.empty())
            return;

        int minY = (int)std::floor(minYf);
        int maxY = (int)std::ceil(maxYf) - 1;
        int minX = (int)std::floor(minXf);
        int maxX = (int)std::ceil(maxXf) - 1;
        if (maxY < minY || maxX < minX)
            return;

        const int rowW = maxX - minX + 1;
        constexpr int kSubSamples = 4; // vertical supersampling
        std::vector<float> coverage(rowW);

        for (int y = minY; y <= maxY; ++y)
        {
            std::fill(coverage.begin(), coverage.end(), 0.f);

            for (int s = 0; s < kSubSamples; ++s)
            {
                double scanY = y + (s + 0.5) / kSubSamples;
                struct Xing
                {
                    double x;
                    int dir;
                };
                std::vector<Xing> xs;
                for (auto &e : edges)
                {
                    double ylo = std::min(e.y0, e.y1), yhi = std::max(e.y0, e.y1);
                    if (scanY < ylo || scanY >= yhi)
                        continue;
                    double t = (scanY - e.y0) / (e.y1 - e.y0);
                    xs.push_back({e.x0 + t * (e.x1 - e.x0), e.dir});
                }
                if (xs.empty())
                    continue;
                std::sort(xs.begin(), xs.end(), [](const Xing &a, const Xing &b)
                          { return a.x < b.x; });

                int winding = 0;
                bool inside = false;
                double startX = 0;
                for (auto &xing : xs)
                {
                    bool wasInside = inside;
                    winding += xing.dir;
                    inside = (rule == SvgFillRule::EvenOdd) ? ((winding & 1) != 0) : (winding != 0);
                    if (!wasInside && inside)
                        startX = xing.x;
                    else if (wasInside && !inside)
                    {
                        double spanStart = std::max(startX, (double)minX);
                        double spanEnd = std::min(xing.x, (double)(maxX + 1));
                        if (spanEnd <= spanStart)
                            continue;

                        int px0 = (int)std::floor(spanStart);
                        int px1 = (int)std::floor(spanEnd - 1e-9);
                        if (px0 == px1)
                        {
                            coverage[px0 - minX] += (float)((spanEnd - spanStart) / kSubSamples);
                        }
                        else
                        {
                            coverage[px0 - minX] += (float)(((px0 + 1) - spanStart) / kSubSamples);
                            for (int px = px0 + 1; px < px1; ++px)
                                coverage[px - minX] += 1.f / kSubSamples;
                            coverage[px1 - minX] += (float)((spanEnd - px1) / kSubSamples);
                        }
                    }
                }
            }

            // Merge full-coverage runs into one opaque fillRect; draw
            // partial (edge) pixels individually with alpha scaled by
            // coverage.
            int x = 0;
            while (x < rowW)
            {
                float c = coverage[x];
                if (c <= 0.001f)
                {
                    ++x;
                    continue;
                }
                if (c >= 0.999f)
                {
                    int runStart = x;
                    while (x < rowW && coverage[x] >= 0.999f)
                        ++x;
                    painter.fillRect(minX + runStart, y, x - runStart, 1, color);
                }
                else
                {
                    Color partial = color.withAlpha((uint8_t)std::round(color.a * c));
                    painter.fillRect(minX + x, y, 1, 1, partial);
                    ++x;
                }
            }
        }
    }

} // namespace flux_svg_detail

struct SvgSubpath
{
    std::vector<std::pair<float, float>> points;
    bool closed = false;
};

inline std::vector<SvgSubpath> parseSvgPath(const std::string &d)
{
    using namespace flux_svg_detail;
    std::vector<SvgSubpath> subpaths;
    SvgSubpath current;

    size_t i = 0, n = d.size();
    char cmd = 0;
    float curX = 0, curY = 0, startX = 0, startY = 0;
    float lastCtrlX = 0, lastCtrlY = 0;
    bool haveLastCubicCtrl = false, haveLastQuadCtrl = false;

    auto flushSubpath = [&]()
    {
        if (current.points.size() >= 2)
            subpaths.push_back(current);
        current = SvgSubpath{};
    };

    while (i < n)
    {
        while (i < n && (std::isspace((unsigned char)d[i]) || d[i] == ','))
            ++i;
        if (i >= n)
            break;

        if (std::isalpha((unsigned char)d[i]))
        {
            cmd = d[i];
            ++i;
        }

        bool relative = std::islower((unsigned char)cmd) != 0;
        char C = (char)std::toupper((unsigned char)cmd);

        if (C == 'Z')
        {
            current.closed = true;
            curX = startX;
            curY = startY;
            flushSubpath();
            haveLastCubicCtrl = haveLastQuadCtrl = false;
            continue;
        }

        int need = 0;
        switch (C)
        {
        case 'M':
        case 'L':
        case 'T':
            need = 2;
            break;
        case 'H':
        case 'V':
            need = 1;
            break;
        case 'C':
            need = 6;
            break;
        case 'S':
        case 'Q':
            need = 4;
            break;
        case 'A':
            need = 7;
            break;
        default:
            need = 0;
            break;
        }
        float nums[8] = {};
        for (int k = 0; k < need; ++k)
            if (!scanNumber(d, i, nums[k]))
            {
                need = k;
                break;
            }
        if (need == 0)
            break;

        switch (C)
        {
        case 'M':
        {
            float x = relative ? curX + nums[0] : nums[0];
            float y = relative ? curY + nums[1] : nums[1];
            if (!current.points.empty())
                flushSubpath();
            curX = startX = x;
            curY = startY = y;
            current.points.push_back({x, y});
            cmd = relative ? 'l' : 'L';
            break;
        }
        case 'L':
        {
            curX = relative ? curX + nums[0] : nums[0];
            curY = relative ? curY + nums[1] : nums[1];
            current.points.push_back({curX, curY});
            break;
        }
        case 'H':
            curX = relative ? curX + nums[0] : nums[0];
            current.points.push_back({curX, curY});
            break;
        case 'V':
            curY = relative ? curY + nums[0] : nums[0];
            current.points.push_back({curX, curY});
            break;
        case 'C':
        {
            float x1 = relative ? curX + nums[0] : nums[0], y1 = relative ? curY + nums[1] : nums[1];
            float x2 = relative ? curX + nums[2] : nums[2], y2 = relative ? curY + nums[3] : nums[3];
            float x3 = relative ? curX + nums[4] : nums[4], y3 = relative ? curY + nums[5] : nums[5];
            flattenCubic(current.points, curX, curY, x1, y1, x2, y2, x3, y3);
            lastCtrlX = x2;
            lastCtrlY = y2;
            haveLastCubicCtrl = true;
            haveLastQuadCtrl = false;
            curX = x3;
            curY = y3;
            break;
        }
        case 'S':
        {
            float x2 = relative ? curX + nums[0] : nums[0], y2 = relative ? curY + nums[1] : nums[1];
            float x3 = relative ? curX + nums[2] : nums[2], y3 = relative ? curY + nums[3] : nums[3];
            float x1 = haveLastCubicCtrl ? (2 * curX - lastCtrlX) : curX;
            float y1 = haveLastCubicCtrl ? (2 * curY - lastCtrlY) : curY;
            flattenCubic(current.points, curX, curY, x1, y1, x2, y2, x3, y3);
            lastCtrlX = x2;
            lastCtrlY = y2;
            haveLastCubicCtrl = true;
            haveLastQuadCtrl = false;
            curX = x3;
            curY = y3;
            break;
        }
        case 'Q':
        {
            float x1 = relative ? curX + nums[0] : nums[0], y1 = relative ? curY + nums[1] : nums[1];
            float x2 = relative ? curX + nums[2] : nums[2], y2 = relative ? curY + nums[3] : nums[3];
            flattenQuad(current.points, curX, curY, x1, y1, x2, y2);
            lastCtrlX = x1;
            lastCtrlY = y1;
            haveLastQuadCtrl = true;
            haveLastCubicCtrl = false;
            curX = x2;
            curY = y2;
            break;
        }
        case 'T':
        {
            float x2 = relative ? curX + nums[0] : nums[0], y2 = relative ? curY + nums[1] : nums[1];
            float x1 = haveLastQuadCtrl ? (2 * curX - lastCtrlX) : curX;
            float y1 = haveLastQuadCtrl ? (2 * curY - lastCtrlY) : curY;
            flattenQuad(current.points, curX, curY, x1, y1, x2, y2);
            lastCtrlX = x1;
            lastCtrlY = y1;
            haveLastQuadCtrl = true;
            haveLastCubicCtrl = false;
            curX = x2;
            curY = y2;
            break;
        }
        case 'A':
        {
            float x = relative ? curX + nums[5] : nums[5], y = relative ? curY + nums[6] : nums[6];
            flattenArc(current.points, curX, curY, nums[0], nums[1], nums[2],
                       nums[3] != 0.f, nums[4] != 0.f, x, y);
            curX = x;
            curY = y;
            haveLastCubicCtrl = haveLastQuadCtrl = false;
            break;
        }
        default:
            break;
        }
    }
    if (!current.points.empty())
        flushSubpath();
    return subpaths;
}

struct SvgShape
{
    std::vector<SvgSubpath> subpaths; // already flattened + transformed
    bool hasFill = false;
    Color fillColor = Color::fromRGB(0, 0, 0);
    bool hasStroke = false;
    Color strokeColor = Color::fromRGB(0, 0, 0);
    float strokeWidth = 1.f;
    flux_svg_detail::SvgFillRule fillRule = flux_svg_detail::SvgFillRule::NonZero;
};

struct SvgDocument
{
    float viewBoxX = 0, viewBoxY = 0, viewBoxW = 0, viewBoxH = 0;
    bool hasViewBox = false;
    float intrinsicW = 0, intrinsicH = 0;
    std::vector<SvgShape> shapes;
    bool valid = false;
};

struct SvgStyle
{
    bool hasFill = true;
    Color fill = Color::fromRGB(0, 0, 0);
    bool hasStroke = false;
    Color stroke = Color::fromRGB(0, 0, 0);
    float strokeWidth = 1.f;
    float opacity = 1.f;
    SvgMat transform = SvgMat::identity();
    flux_svg_detail::SvgFillRule fillRule = flux_svg_detail::SvgFillRule::NonZero;
};

inline void applySvgStyleAttrs(const std::unordered_map<std::string, std::string> &attrs, SvgStyle &style)
{
    using namespace flux_svg_detail;
    Color c;
    auto it = attrs.find("fill");
    if (it != attrs.end())
    {
        if (it->second == "none")
            style.hasFill = false;
        else if (parseSvgColor(it->second, c))
        {
            style.hasFill = true;
            style.fill = c;
        }
    }
    it = attrs.find("stroke");
    if (it != attrs.end())
    {
        if (it->second == "none")
            style.hasStroke = false;
        else if (parseSvgColor(it->second, c))
        {
            style.hasStroke = true;
            style.stroke = c;
        }
    }
    it = attrs.find("stroke-width");
    if (it != attrs.end())
        style.strokeWidth = (float)std::atof(it->second.c_str());
    it = attrs.find("opacity");
    if (it != attrs.end())
        style.opacity *= (float)std::atof(it->second.c_str());
    it = attrs.find("fill-opacity");
    if (it != attrs.end())
    {
        float fo = (float)std::atof(it->second.c_str());
        style.fill = style.fill.withAlpha((uint8_t)std::round(style.fill.a * fo));
    }
    it = attrs.find("stroke-opacity");
    if (it != attrs.end())
    {
        float so = (float)std::atof(it->second.c_str());
        style.stroke = style.stroke.withAlpha((uint8_t)std::round(style.stroke.a * so));
    }
    it = attrs.find("transform");
    if (it != attrs.end())
        style.transform = style.transform * parseTransform(it->second);
    it = attrs.find("fill-rule");
    if (it != attrs.end())
        style.fillRule = (it->second == "evenodd")
                             ? flux_svg_detail::SvgFillRule::EvenOdd
                             : flux_svg_detail::SvgFillRule::NonZero;
}

inline SvgDocument parseSvgDocument(const std::string &src)
{
    using namespace flux_svg_detail;
    SvgDocument doc;
    std::vector<SvgStyle> styleStack;
    styleStack.push_back(SvgStyle{});

    size_t i = 0, n = src.size();
    bool sawSvgTag = false;

    while (i < n)
    {
        size_t lt = src.find('<', i);
        if (lt == std::string::npos)
            break;
        size_t gt = src.find('>', lt);
        if (gt == std::string::npos)
            break;

        std::string tagContent = src.substr(lt + 1, gt - lt - 1);
        i = gt + 1;
        if (tagContent.empty())
            continue;
        if (tagContent[0] == '?' || tagContent[0] == '!')
            continue;

        bool closing = tagContent[0] == '/';
        bool selfClosing = !tagContent.empty() && tagContent.back() == '/';
        if (selfClosing)
            tagContent.pop_back();
        if (closing)
            tagContent = tagContent.substr(1);
        while (!tagContent.empty() && std::isspace((unsigned char)tagContent.back()))
            tagContent.pop_back();

        size_t nameEnd = 0;
        while (nameEnd < tagContent.size() && !std::isspace((unsigned char)tagContent[nameEnd]))
            ++nameEnd;
        std::string tagName = tagContent.substr(0, nameEnd);
        std::string attrStr = (nameEnd < tagContent.size()) ? tagContent.substr(nameEnd + 1) : "";

        if (closing)
        {
            if (tagName == "g" && styleStack.size() > 1)
                styleStack.pop_back();
            continue;
        }

        auto attrs = parseAttributes(attrStr);
        mergeStyleAttribute(attrs);

        if (tagName == "svg")
        {
            sawSvgTag = true;
            auto vb = attrs.find("viewBox");
            if (vb != attrs.end())
            {
                auto nums = parseNumberList(vb->second);
                if (nums.size() == 4)
                {
                    doc.hasViewBox = true;
                    doc.viewBoxX = nums[0];
                    doc.viewBoxY = nums[1];
                    doc.viewBoxW = nums[2];
                    doc.viewBoxH = nums[3];
                }
            }
            if (attrs.count("width"))
                doc.intrinsicW = (float)std::atof(attrs["width"].c_str());
            if (attrs.count("height"))
                doc.intrinsicH = (float)std::atof(attrs["height"].c_str());
            continue;
        }

        SvgStyle style = styleStack.back();
        applySvgStyleAttrs(attrs, style);

        if (tagName == "g")
        {
            if (!selfClosing)
                styleStack.push_back(style);
            continue;
        }

        if (tagName == "defs" || tagName == "clipPath" || tagName == "style" ||
            tagName == "title" || tagName == "desc" || tagName == "metadata" ||
            tagName == "text" || tagName == "use" || tagName == "image" ||
            tagName == "linearGradient" || tagName == "radialGradient")
        {
            if (!selfClosing)
            {
                size_t closePos = src.find("</" + tagName, i);
                if (closePos != std::string::npos)
                {
                    size_t closeEnd = src.find('>', closePos);
                    if (closeEnd != std::string::npos)
                        i = closeEnd + 1;
                }
            }
            continue;
        }

        SvgShape shape;
        bool haveShape = false;

        if (tagName == "path")
        {
            auto d = attrs.find("d");
            if (d != attrs.end())
            {
                shape.subpaths = parseSvgPath(d->second);
                haveShape = !shape.subpaths.empty();
            }
        }
        else if (tagName == "rect")
        {
            float rx0 = attrs.count("x") ? (float)std::atof(attrs["x"].c_str()) : 0.f;
            float ry0 = attrs.count("y") ? (float)std::atof(attrs["y"].c_str()) : 0.f;
            float rw = attrs.count("width") ? (float)std::atof(attrs["width"].c_str()) : 0.f;
            float rh = attrs.count("height") ? (float)std::atof(attrs["height"].c_str()) : 0.f;
            if (rw > 0 && rh > 0)
            {
                SvgSubpath sp;
                sp.closed = true;
                sp.points = {{rx0, ry0}, {rx0 + rw, ry0}, {rx0 + rw, ry0 + rh}, {rx0, ry0 + rh}};
                shape.subpaths.push_back(sp);
                haveShape = true;
            }
        }
        else if (tagName == "circle" || tagName == "ellipse")
        {
            float cxp = attrs.count("cx") ? (float)std::atof(attrs["cx"].c_str()) : 0.f;
            float cyp = attrs.count("cy") ? (float)std::atof(attrs["cy"].c_str()) : 0.f;
            float rxp, ryp;
            if (tagName == "circle")
                rxp = ryp = attrs.count("r") ? (float)std::atof(attrs["r"].c_str()) : 0.f;
            else
            {
                rxp = attrs.count("rx") ? (float)std::atof(attrs["rx"].c_str()) : 0.f;
                ryp = attrs.count("ry") ? (float)std::atof(attrs["ry"].c_str()) : 0.f;
            }
            if (rxp > 0 && ryp > 0)
            {
                SvgSubpath sp;
                sp.closed = true;
                const int segs = 32;
                for (int k = 0; k < segs; ++k)
                {
                    float t = (float)k / segs * 2.f * 3.14159265358979f;
                    sp.points.push_back({cxp + rxp * std::cos(t), cyp + ryp * std::sin(t)});
                }
                shape.subpaths.push_back(sp);
                haveShape = true;
            }
        }
        else if (tagName == "line")
        {
            SvgSubpath sp;
            sp.closed = false;
            sp.points = {
                {attrs.count("x1") ? (float)std::atof(attrs["x1"].c_str()) : 0.f,
                 attrs.count("y1") ? (float)std::atof(attrs["y1"].c_str()) : 0.f},
                {attrs.count("x2") ? (float)std::atof(attrs["x2"].c_str()) : 0.f,
                 attrs.count("y2") ? (float)std::atof(attrs["y2"].c_str()) : 0.f}};
            shape.subpaths.push_back(sp);
            style.hasFill = false;
            haveShape = true;
        }
        else if (tagName == "polyline" || tagName == "polygon")
        {
            auto p = attrs.find("points");
            if (p != attrs.end())
            {
                auto nums = parseNumberList(p->second);
                SvgSubpath sp;
                sp.closed = (tagName == "polygon");
                for (size_t k = 0; k + 1 < nums.size(); k += 2)
                    sp.points.push_back({nums[k], nums[k + 1]});
                if (sp.points.size() >= 2)
                {
                    shape.subpaths.push_back(sp);
                    haveShape = true;
                    if (tagName == "polyline")
                        style.hasFill = false;
                }
            }
        }

        if (haveShape)
        {
            for (auto &sp : shape.subpaths)
                for (auto &pt : sp.points)
                    pt = style.transform.apply(pt.first, pt.second);

            shape.hasFill = style.hasFill;
            shape.fillColor = style.fill.withAlpha((uint8_t)std::round(style.fill.a * style.opacity));
            shape.hasStroke = style.hasStroke;
            shape.strokeColor = style.stroke.withAlpha((uint8_t)std::round(style.stroke.a * style.opacity));
            shape.strokeWidth = std::max(0.f, style.strokeWidth);
            shape.fillRule = style.fillRule;
            doc.shapes.push_back(std::move(shape));
        }
    }

    doc.valid = sawSvgTag && !doc.shapes.empty();
    return doc;
}

// ============================================================================
// SvgWidget
// ============================================================================

enum class SvgFit
{
    Fill,
    Contain,
    Cover,
    None,
    ScaleDown
};

class SvgWidget : public Widget
{
public:
    SvgFit fit = SvgFit::Contain;
    Alignment svgAlignment = Alignment::Center;
    // alpha 0 = "use each shape's own fill/stroke colors" (default);
    // alpha > 0 recolors every fill/stroke to a single flat tint, the
    // common "icon tinting" use case.
    Color tintColor = Color::fromRGBA(0, 0, 0, 0);

    SvgWidget()
    {
        autoWidth = true;
        autoHeight = true;
    }

    static std::shared_ptr<SvgWidget> fromString(const std::string &svgText)
    {
        auto w = std::make_shared<SvgWidget>();
        w->setSource(svgText);
        return w;
    }

    static std::shared_ptr<SvgWidget> asset(const std::string &path)
    {
        auto w = std::make_shared<SvgWidget>();
        w->loadAsset(path);
        return w;
    }

    std::shared_ptr<SvgWidget> setSource(const std::string &svgText)
    {
        doc_ = parseSvgDocument(svgText);
        // Kept verbatim (not reconstructed from doc_) so the DOM/SSR
        // backend can embed the browser's own SVG renderer instead of
        // re-deriving markup from our parsed shape list — see _renderDom().
        rawSvgText_ = svgText;
        markNeedsLayout();
        return self();
    }

    std::shared_ptr<SvgWidget> loadAsset(const std::string &path)
    {
#if defined(_WIN32) && !defined(FLUX_SSR)
        FILE *f = nullptr;
        fopen_s(&f, path.c_str(), "rb");
#else
        FILE *f = fopen(path.c_str(), "rb");
#endif
        if (!f)
        {
            doc_ = SvgDocument{};
            markNeedsLayout();
            return self();
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string buf;
        if (sz > 0)
        {
            buf.resize((size_t)sz);
            size_t nread = fread(buf.data(), 1, (size_t)sz, f);
            if ((long)nread != sz)
                buf.clear();
        }
        fclose(f);
        return setSource(buf);
    }

    std::shared_ptr<SvgWidget> setFit(SvgFit f)
    {
        fit = f;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<SvgWidget> setAlignment(Alignment a)
    {
        svgAlignment = a;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<SvgWidget> setTintColor(Color c)
    {
        tintColor = c;
        markNeedsPaint();
        return self();
    }
    std::shared_ptr<SvgWidget> setWidth(int w)
    {
        width = w;
        autoWidth = false;
        markNeedsLayout();
        return self();
    }
    std::shared_ptr<SvgWidget> setHeight(int h)
    {
        height = h;
        autoHeight = false;
        markNeedsLayout();
        return self();
    }

    void computeLayout(GraphicsContext & /*ctx*/, const BoxConstraints &constraints,
                       FontCache & /*fontCache*/) override
    {
        int padW = paddingLeft + paddingRight;
        int padH = paddingTop + paddingBottom;

        if (autoWidth)
            width = (constraints.maxWidth < kUnbounded)
                        ? constraints.clampWidth(constraints.maxWidth)
                        : constraints.clampWidth((int)std::ceil(_naturalWidth()) + padW);
        else
            width = constraints.clampWidth(width);

        if (autoHeight)
            height = (constraints.maxHeight < kUnbounded)
                         ? constraints.clampHeight(constraints.maxHeight)
                         : constraints.clampHeight((int)std::ceil(_naturalHeight()) + padH);
        else
            height = constraints.clampHeight(height);

        applyConstraints();
        needsLayout = false;
    }

    void render(GraphicsContext &ctx, FontCache & /*fontCache*/) override
    {
        Painter painter(ctx, this);
        if (hasBackground)
            drawRoundedRectangle(ctx);

        int cx = x + paddingLeft, cy = y + paddingTop;
        int cw = width - paddingLeft - paddingRight;
        int ch = height - paddingTop - paddingBottom;

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
        // DOM backend: Painter::fillRect etc. each map to ONE persistent,
        // reused DOM node per (owner, slot) — see flux_painter_dom.cpp's
        // ensureNode(). _renderShapes() below calls fillRect/drawPolyline
        // many times per shape (once per AA scanline row, once per stroke
        // segment) with no slot, so on this backend every call would
        // silently overwrite the SAME node instead of painting distinct
        // rects — the browser only ever shows whatever the LAST call drew.
        // Skip the shape-by-shape path entirely here and let the browser's
        // own SVG renderer do the work via a background-image data URI.
        if (getActiveDomAdapter() && cw > 0 && ch > 0)
        {
            _renderDom(cx, cy, cw, ch);
            needsPaint = false;
            return;
        }
#endif


        if (doc_.valid && cw > 0 && ch > 0)
            _renderShapes(painter, cx, cy, cw, ch);

        needsPaint = false;
    }

private:
    SvgDocument doc_;
    std::string rawSvgText_;

    std::shared_ptr<SvgWidget> self() { return std::static_pointer_cast<SvgWidget>(shared_from_this()); }

    float _naturalWidth() const
    {
        if (doc_.intrinsicW > 0)
            return doc_.intrinsicW;
        if (doc_.hasViewBox)
            return doc_.viewBoxW;
        return 24.f;
    }
    float _naturalHeight() const
    {
        if (doc_.intrinsicH > 0)
            return doc_.intrinsicH;
        if (doc_.hasViewBox)
            return doc_.viewBoxH;
        return 24.f;
    }

#if defined(__EMSCRIPTEN__) || defined(FLUX_SSR)
    // Percent-encodes the subset of characters that are unsafe (or that
    // would prematurely terminate the surrounding CSS url("...") string)
    // inside a `data:image/svg+xml,<...>` URI. Leaving most SVG markup
    // (letters, digits, path-data punctuation like '.', '-', ',') un-
    // encoded keeps the resulting string short; browsers accept a mix of
    // encoded and literal UTF-8 bytes in this position.
    static std::string _urlEncodeSvg(const std::string &s)
    {
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() + s.size() / 4);
        for (unsigned char c : s)
        {
            bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '.' || c == '-' || c == '_' || c == ':' ||
                        c == '/' || c == ',' || c == ' ' || c == '=' ||
                        c == ';';
            if (safe)
            {
                out += (char)c;
            }
            else
            {
                out += '%';
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
            }
        }
        return out;
    }

    // DOM backend paint path — see the comment at the render() call site.
    // Embeds the ORIGINAL SVG markup as a background-image data URI on one
    // reused node, letting the browser's native SVG renderer do the actual
    // rasterization (correctly antialiased, for free) instead of our own
    // scanline fill, which only works against a real pixel buffer.
    //
    // Known limitation: tintColor recoloring (used for flat icon tinting
    // on every other backend) has no equivalent here yet — a CSS filter
    // approximation or server-side text recolor of rawSvgText_ before
    // encoding would be needed; left as a follow-up since none of the
    // icons in this pass use it.
    void _renderDom(int cx, int cy, int cw, int ch)
    {
        IDomAdapter *adapter = getActiveDomAdapter();
        if (!adapter || rawSvgText_.empty())
            return;

        DomNodeHandle node = fluxDomEnsureNode(this, "div");
        fluxDomApplyRect(this, cx, cy, cw, ch);

        std::string dataUri = "data:image/svg+xml,";
        dataUri += _urlEncodeSvg(rawSvgText_);
        adapter->setStyle(node, "background-image", "url(\"" + dataUri + "\")");
        adapter->setStyle(node, "background-repeat", "no-repeat");
        adapter->setStyle(node, "background-position", "center");

        const char *sizeMode = "contain";
        switch (fit)
        {
        case SvgFit::Fill:
            sizeMode = "100% 100%";
            break;
        case SvgFit::Cover:
            sizeMode = "cover";
            break;
        case SvgFit::None:
            sizeMode = "auto";
            break;
        case SvgFit::Contain:
        case SvgFit::ScaleDown:
        default:
            sizeMode = "contain";
            break;
        }
        adapter->setStyle(node, "background-size", sizeMode);
    }
#endif



    void _renderShapes(Painter &painter, int cx, int cy, int cw, int ch)
    {
        float vbX = doc_.hasViewBox ? doc_.viewBoxX : 0.f;
        float vbY = doc_.hasViewBox ? doc_.viewBoxY : 0.f;
        float vbW = doc_.hasViewBox ? doc_.viewBoxW : _naturalWidth();
        float vbH = doc_.hasViewBox ? doc_.viewBoxH : _naturalHeight();
        if (vbW <= 0.f)
            vbW = 1.f;
        if (vbH <= 0.f)
            vbH = 1.f;

        float scaleX = cw / vbW, scaleY = ch / vbH;
        float scale = scaleX;
        bool uniform = (fit != SvgFit::Fill);

        switch (fit)
        {
        case SvgFit::Fill:
            break;
        case SvgFit::Contain:
            scale = std::min(scaleX, scaleY);
            break;
        case SvgFit::Cover:
            scale = std::max(scaleX, scaleY);
            break;
        case SvgFit::None:
            scale = 1.f;
            break;
        case SvgFit::ScaleDown:
            scale = std::min(1.f, std::min(scaleX, scaleY));
            break;
        }

        float sx = uniform ? scale : scaleX;
        float sy = uniform ? scale : scaleY;
        float destW = vbW * sx, destH = vbH * sy;
        float freeX = cw - destW, freeY = ch - destH;
        float offX = freeX * 0.5f, offY = freeY * 0.5f;

        switch (svgAlignment)
        {
        case Alignment::TopCenter:
            offY = 0.f;
            break;
        case Alignment::BottomCenter:
            offY = freeY;
            break;
        case Alignment::CenterLeft:
            offX = 0.f;
            break;
        case Alignment::CenterRight:
            offX = freeX;
            break;
        case Alignment::Start:
            offX = 0.f;
            offY = 0.f;
            break;
        case Alignment::End:
            offX = freeX;
            offY = freeY;
            break;
        case Alignment::TopRight:
            offX = freeX;
            offY = 0.f;
            break;
        case Alignment::BottomLeft:
            offX = 0.f;
            offY = freeY;
            break;
        case Alignment::BottomRight:
            offX = freeX;
            offY = freeY;
            break;
        default:
            break; // Center — already set
        }

        auto toDest = [&](float px, float py) -> std::pair<float, float>
        {
            return {cx + offX + (px - vbX) * sx,
                    cy + offY + (py - vbY) * sy};
        };

        bool tinting = tintColor.a > 0;

        painter.pushClipRect(cx, cy, cw, ch);
        for (auto &shape : doc_.shapes)
        {
            std::vector<std::vector<std::pair<float, float>>> devicePolys(shape.subpaths.size());
            for (size_t k = 0; k < shape.subpaths.size(); ++k)
            {
                auto &sp = shape.subpaths[k];
                if (sp.points.size() < 2)
                    continue;
                auto &dst = devicePolys[k];
                dst.reserve(sp.points.size());
                for (auto &pt : sp.points)
                    dst.push_back(toDest(pt.first, pt.second));
            }

            // Fill ALL of this shape's subpaths together — required so holes
            // (opposite-winding or evenodd-alternating subpaths) actually cancel
            // out instead of each subpath painting over the "hole" solid.
            if (shape.hasFill)
                flux_svg_detail::fillShapeScanline(painter, devicePolys, shape.fillRule,
                                                   tinting ? tintColor : shape.fillColor);

            if (shape.hasStroke && shape.strokeWidth > 0.f)
            {
                int sw = std::max(1, (int)std::round(shape.strokeWidth * sx));
                for (size_t k = 0; k < shape.subpaths.size(); ++k)
                {
                    if (devicePolys[k].size() < 2)
                        continue;
                    std::vector<std::pair<int, int>> pts;
                    pts.reserve(devicePolys[k].size() + 1);
                    for (auto &p : devicePolys[k])
                        pts.push_back({(int)std::round(p.first), (int)std::round(p.second)});
                    if (shape.subpaths[k].closed)
                        pts.push_back(pts.front());
                    painter.drawPolyline(pts, tinting ? tintColor : shape.strokeColor, sw);
                }
            }
        }
        painter.popClipRect();
    }
};

using SvgWidgetPtr = std::shared_ptr<SvgWidget>;

// Content starting with '<' is treated as inline markup; anything else is
// treated as an asset path (mirrors Image()'s http-prefix heuristic).
inline SvgWidgetPtr Svg(const std::string &svgTextOrPath)
{
    size_t nz = svgTextOrPath.find_first_not_of(" \t\r\n");
    if (nz != std::string::npos && svgTextOrPath[nz] == '<')
        return SvgWidget::fromString(svgTextOrPath);
    return SvgWidget::asset(svgTextOrPath);
}

#endif // FLUX_SVG_HPP