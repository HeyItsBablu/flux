#ifndef FLUX_VECTOR_HPP
#define FLUX_VECTOR_HPP

// ============================================================================
// flux_vector.hpp  —  VectorSurface
// ============================================================================

#include "clipper2/clipper.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// §V0  MATH HELPERS
// ============================================================================

namespace vmath {

struct Vec2 {
  float x, y;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(float s, Vec2 a) { return {a.x * s, a.y * s}; }
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float len(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline Vec2 norm(Vec2 v) {
  float l = len(v);
  return l > 1e-9f ? Vec2{v.x / l, v.y / l} : Vec2{0, 0};
}
inline Vec2 perp(Vec2 v) { return {-v.y, v.x}; }

// Cubic Bézier point
inline Vec2 cubicBez(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
  float u = 1.f - t;
  return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) +
         p3 * (t * t * t);
}
// Quadratic Bézier point
inline Vec2 quadBez(Vec2 p0, Vec2 p1, Vec2 p2, float t) {
  float u = 1.f - t;
  return p0 * (u * u) + p1 * (2 * u * t) + p2 * (t * t);
}

// 3×3 column-major affine transform stored as [a,b,c, d,e,f, 0,0,1]
struct Mat3 {
  float m[9]; // column-major: m[col*3+row]
  Mat3() { identity(); }
  void identity() {
    m[0] = 1;
    m[1] = 0;
    m[2] = 0;
    m[3] = 0;
    m[4] = 1;
    m[5] = 0;
    m[6] = 0;
    m[7] = 0;
    m[8] = 1;
  }
  Vec2 apply(Vec2 v) const {
    return {m[0] * v.x + m[3] * v.y + m[6], m[1] * v.x + m[4] * v.y + m[7]};
  }
  static Mat3 translate(float tx, float ty) {
    Mat3 r;
    r.m[6] = tx;
    r.m[7] = ty;
    return r;
  }
  static Mat3 scale(float sx, float sy) {
    Mat3 r;
    r.m[0] = sx;
    r.m[4] = sy;
    return r;
  }
  static Mat3 rotate(float rad) {
    Mat3 r;
    float c = cosf(rad), s = sinf(rad);
    r.m[0] = c;
    r.m[1] = s;
    r.m[3] = -s;
    r.m[4] = c;
    return r;
  }
  Mat3 operator*(const Mat3 &o) const {
    Mat3 r;
    for (int c = 0; c < 3; c++)
      for (int row = 0; row < 3; row++) {
        r.m[c * 3 + row] = 0;
        for (int k = 0; k < 3; k++)
          r.m[c * 3 + row] += m[k * 3 + row] * o.m[c * 3 + k];
      }
    return r;
  }
  float tx() const { return m[6]; }
  float ty() const { return m[7]; }
};

// Axis-aligned bounding box
struct AABB {
  float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
  bool valid() const { return x0 <= x1 && y0 <= y1; }
  void expand(Vec2 p) {
    x0 = min(x0, p.x);
    y0 = min(y0, p.y);
    x1 = max(x1, p.x);
    y1 = max(y1, p.y);
  }
  bool contains(Vec2 p) const {
    return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
  }
  bool intersects(const AABB &o) const {
    return x0 <= o.x1 && x1 >= o.x0 && y0 <= o.y1 && y1 >= o.y0;
  }
  Vec2 center() const { return {(x0 + x1) * .5f, (y0 + y1) * .5f}; }
  float width() const { return x1 - x0; }
  float height() const { return y1 - y0; }
};

} // namespace vmath

// ============================================================================
// §V1  SHAPE DATA MODEL
// ============================================================================

enum class PathCmdKind : uint8_t { MoveTo, LineTo, CubicTo, QuadTo, Close };

struct PathCmd {
  PathCmdKind kind;
  vmath::Vec2 pt[3];
};

enum class VFillRule { NonZero, EvenOdd };

struct GradientStop {
  RGBA color = {0, 0, 0, 1};
  float position = 0.f; // 0.0 → 1.0 along the gradient axis
};

enum class VFillMode { Solid, LinearGradient };

struct VFill {
  RGBA color = {0, 0, 0, 1};
  VFillRule rule = VFillRule::NonZero;
  bool none = false;

  // Gradient fields (only used when mode == LinearGradient)
  VFillMode mode = VFillMode::Solid;
  std::vector<GradientStop> stops; // 2–4 stops, sorted by position
  float gx0 = 0.f, gy0 = 0.f;      // start point (object space 0→1)
  float gx1 = 1.f, gy1 = 0.f;      // end point   (object space 0→1)
};

enum class LineCapV { Butt, Round, Square };
enum class LineJoinV { Miter, Round, Bevel, Butt };

struct VStroke {
  RGBA color = {0, 0, 0, 1};
  float width = 1.f;
  float dashOffset = 0.f;
  LineCapV cap = LineCapV::Round;
  LineJoinV join = LineJoinV::Round;
  std::vector<float> dash;
  bool none = false;
};

enum class VShapeKind : uint8_t { Rect, Ellipse, Poly, Path, Text };

struct VRect {
  float x, y, w, h, rx, ry;
};
struct VEllipse {
  float cx, cy, rx, ry;
};
struct VPoly {
  std::vector<vmath::Vec2> pts;
  bool closed = false;
};
struct VPath {
  std::vector<PathCmd> cmds;
};
struct VText {
  std::wstring text;
  float x, y;
  TextStyle style;
};

using VShapeId = uint32_t;
static constexpr VShapeId kNoShape = 0;

struct VShape {
  VShapeId id = kNoShape;
  VShapeKind kind = VShapeKind::Path;
  vmath::Mat3 xform;
  VFill fill;
  VStroke stroke;
  VRect rect{};
  VEllipse ellipse{};
  VPoly poly{};
  VPath path{};
  VText text{};
};

using VScene = std::vector<VShape>;

// ============================================================================
// §V2  TESSELLATOR
// ============================================================================

namespace vtess {

using namespace vmath;

static constexpr int kEllipseSegs = 256;
static constexpr int kRoundCapSegs = 16;
static constexpr int kCurveSteps = 16;

using Contour = std::vector<Vec2>;
using Contours = std::vector<Contour>;

inline void flattenPath(const VPath &p, Contours &out,
                        int steps = kCurveSteps) {
  Contour cur;
  Vec2 pen{0, 0};
  auto flush = [&]() {
    if (!cur.empty()) {
      out.push_back(cur);
      cur.clear();
    }
  };
  for (auto &cmd : p.cmds) {
    switch (cmd.kind) {
    case PathCmdKind::MoveTo:
      flush();
      pen = cmd.pt[0];
      cur.push_back(pen);
      break;
    case PathCmdKind::LineTo:
      pen = cmd.pt[0];
      cur.push_back(pen);
      break;
    case PathCmdKind::CubicTo:
      for (int i = 1; i <= steps; i++) {
        float t = float(i) / steps;
        cur.push_back(cubicBez(pen, cmd.pt[0], cmd.pt[1], cmd.pt[2], t));
      }
      pen = cmd.pt[2];
      break;
    case PathCmdKind::QuadTo:
      for (int i = 1; i <= steps; i++) {
        float t = float(i) / steps;
        cur.push_back(quadBez(pen, cmd.pt[0], cmd.pt[1], t));
      }
      pen = cmd.pt[1];
      break;
    case PathCmdKind::Close:
      if (!cur.empty())
        cur.push_back(cur.front());
      flush();
      break;
    }
  }
  flush();
}

inline Contour ellipseContour(float cx, float cy, float rx, float ry,
                              int segs = kEllipseSegs) {
  Contour c;
  c.reserve(segs);
  for (int i = 0; i < segs; i++) {
    float a = float(i) / segs * 6.2831853f;
    c.push_back({cx + cosf(a) * rx, cy + sinf(a) * ry});
  }
  return c;
}

inline Contour rectContour(float x, float y, float w, float h, float rx,
                           float ry) {
  if (rx <= 0 && ry <= 0)
    return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
  rx = min(rx, w * .5f);
  ry = min(ry, h * .5f);
  const int segs = 8;
  Contour c;
  auto arc = [&](float acx, float acy, float startA, float endA) {
    for (int i = 0; i <= segs; i++) {
      float a = startA + (endA - startA) * float(i) / segs;
      c.push_back({acx + cosf(a) * rx, acy + sinf(a) * ry});
    }
  };
  static constexpr float PI = 3.14159265f;
  arc(x + w - rx, y + ry, -PI / 2, 0);
  arc(x + w - rx, y + h - ry, 0, PI / 2);
  arc(x + rx, y + h - ry, PI / 2, PI);
  arc(x + rx, y + ry, PI, 3 * PI / 2);
  return c;
}

static float polyArea(const std::vector<Vec2> &p) {
  float a = 0;
  int n = (int)p.size();
  for (int i = 0, j = n - 1; i < n; j = i++)
    a += cross(p[j], p[i]);
  return a * .5f;
}

inline void earClip(const std::vector<Vec2> &pts, std::vector<float> &out) {
  int n = (int)pts.size();
  if (n < 3)
    return;
  if (n == 3) {
    for (auto &p : pts) {
      out.push_back(p.x);
      out.push_back(p.y);
    }
    return;
  }
  std::vector<Vec2> poly = pts;
  if (polyArea(poly) < 0)
    std::reverse(poly.begin(), poly.end());
  if (poly.size() > 1 && len(poly.back() - poly.front()) < 1e-4f)
    poly.pop_back();
  n = (int)poly.size();
  if (n < 3)
    return;
  std::vector<int> idx(n);
  std::iota(idx.begin(), idx.end(), 0);
  auto isEar = [&](int i) -> bool {
    int a = idx[(i - 1 + idx.size()) % idx.size()], b = idx[i],
        c = idx[(i + 1) % idx.size()];
    Vec2 A = poly[a], B = poly[b], C = poly[c];
    if (cross(B - A, C - A) <= 0)
      return false;
    for (int j = 0; j < (int)idx.size(); j++) {
      if (j == (int)((i - 1 + idx.size()) % idx.size()) || j == i ||
          j == (int)((i + 1) % idx.size()))
        continue;
      Vec2 P = poly[idx[j]];
      if (cross(B - A, P - A) >= 0 && cross(C - B, P - B) >= 0 &&
          cross(A - C, P - C) >= 0)
        return false;
    }
    return true;
  };
  int limit = n * n + 10;
  while (idx.size() >= 3 && limit-- > 0) {
    bool clipped = false;
    for (int i = 0; i < (int)idx.size(); i++) {
      if (isEar(i)) {
        int a = idx[(i - 1 + idx.size()) % idx.size()], b = idx[i],
            c = idx[(i + 1) % idx.size()];
        out.push_back(poly[a].x);
        out.push_back(poly[a].y);
        out.push_back(poly[b].x);
        out.push_back(poly[b].y);
        out.push_back(poly[c].x);
        out.push_back(poly[c].y);
        idx.erase(idx.begin() + i);
        clipped = true;
        break;
      }
    }
    if (!clipped)
      break;
  }
}

// ── Dash pattern subdivider ────────────────────────────────────────────────
// Takes a polyline and a dash pattern (alternating dash/gap lengths in canvas
// units) and returns a list of sub-polylines — one per dash segment.
// Pattern is cycled: [dash0, gap0, dash1, gap1, ...]
// If pattern is empty the original polyline is returned as-is (solid).
inline std::vector<Contour> applyDash(const std::vector<Vec2> &pts, bool closed,
                                      const std::vector<float> &pattern,
                                      float offset = 0.f) {
  if (pattern.empty())
    return {pts};
  if (pts.size() < 2)
    return {};

  // Build a flat list of (length, patternIndex) segments walking the polyline
  // We walk arc-length along the path, switching dash/gap according to pattern
  std::vector<Contour> result;
  Contour current;

  // Total pattern length for cycling
  float patLen = 0.f;
  for (float v : pattern)
    patLen += v;
  if (patLen < 1e-6f)
    return {pts};

  // Normalize offset into [0, patLen)
  float pos = std::fmod(offset, patLen);
  if (pos < 0.f)
    pos += patLen;

  // Determine starting pattern index and remaining length in first segment
  int patIdx = 0;
  float patRemain = 0.f;
  {
    float acc = 0.f;
    for (int i = 0; i < (int)pattern.size(); i++) {
      if (acc + pattern[i] > pos) {
        patIdx = i;
        patRemain = (acc + pattern[i]) - pos;
        break;
      }
      acc += pattern[i];
    }
  }

  bool drawing = (patIdx % 2 == 0); // even indices = dash, odd = gap

  // Walk each polyline segment
  Vec2 prev = pts[0];
  if (drawing)
    current.push_back(prev);

  int n = (int)pts.size();
  int numSegs = closed ? n : n - 1;

  for (int si = 0; si < numSegs; si++) {
    Vec2 a = pts[si];
    Vec2 b = pts[(si + 1) % n];
    float segLen = len(b - a);
    if (segLen < 1e-9f)
      continue;
    Vec2 dir = norm(b - a);

    float walked = 0.f;
    while (walked < segLen) {
      float step = min(patRemain, segLen - walked);
      Vec2 pt = a + dir * (walked + step);

      if (drawing) {
        if (current.empty())
          current.push_back(a + dir * walked);
        current.push_back(pt);
      } else {
        // End of a gap — if we were drawing, the contour was already saved
        if (!current.empty()) {
          result.push_back(current);
          current.clear();
        }
      }

      walked += step;
      patRemain -= step;

      if (patRemain < 1e-6f) {
        // Advance to next pattern slot
        patIdx = (patIdx + 1) % (int)pattern.size();
        patRemain = pattern[patIdx];
        drawing = (patIdx % 2 == 0);

        if (drawing && walked < segLen)
          current.push_back(a + dir * walked);
        else if (!drawing && !current.empty()) {
          result.push_back(current);
          current.clear();
        }
      }
    }
  }

  if (!current.empty())
    result.push_back(current);
  return result;
}

inline void expandStroke(const std::vector<Vec2> &pts, bool closed, float hw,
                         LineCapV cap, LineJoinV join,
                         std::vector<float> &out) {
  int n = (int)pts.size();
  if (n < 2)
    return;
  if (hw < 0.0001f)
    hw = 0.0001f;

  auto pushQuad = [&](Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    out.push_back(a.x);
    out.push_back(a.y);
    out.push_back(b.x);
    out.push_back(b.y);
    out.push_back(c.x);
    out.push_back(c.y);
    out.push_back(a.x);
    out.push_back(a.y);
    out.push_back(c.x);
    out.push_back(c.y);
    out.push_back(d.x);
    out.push_back(d.y);
  };
  auto segNormal = [&](Vec2 a, Vec2 b) -> Vec2 { return perp(norm(b - a)); };
  static constexpr float kMiterLimit = 4.f;
  auto miterOffset = [&](Vec2 nIn, Vec2 nOut, float &outScale) -> Vec2 {
    Vec2 bis = norm(Vec2{nIn.x + nOut.x, nIn.y + nOut.y});
    float denom = dot(bis, nIn);
    if (denom < 1e-4f) {
      outScale = 1.f;
      return nIn;
    }
    float scale = 1.f / denom;
    outScale = scale;
    if (scale > kMiterLimit) {
      outScale = kMiterLimit;
      return bis;
    }
    return bis * scale;
  };

  int numSegs = closed ? n : n - 1;
  std::vector<Vec2> segN(numSegs);
  for (int i = 0; i < numSegs; i++) {
    int j = (i + 1) % n;
    segN[i] = segNormal(pts[i], pts[j]);
  }
  std::vector<Vec2> normals(n);
  std::vector<float> miterScales(n, 1.f);
  for (int i = 0; i < n; i++) {
    if (!closed) {
      if (i == 0) {
        normals[0] = segN[0];
        miterScales[0] = 1.f;
        continue;
      }
      if (i == n - 1) {
        normals[n - 1] = segN[n - 2];
        miterScales[n - 1] = 1.f;
        continue;
      }
    }
    int prevSeg = (i - 1 + numSegs) % numSegs, nextSeg = i % numSegs;
    float sc = 1.f;
    normals[i] = miterOffset(segN[prevSeg], segN[nextSeg], sc);
    miterScales[i] = sc;
  }

  auto roundFan = [&](Vec2 centre, Vec2 fromPt, Vec2 toPt) {
    float a0 = std::atan2(fromPt.y - centre.y, fromPt.x - centre.x);
    float a1 = std::atan2(toPt.y - centre.y, toPt.x - centre.x);
    float da = a1 - a0;
    while (da > 3.14159265f)
      da -= 6.2831853f;
    while (da < -3.14159265f)
      da += 6.2831853f;
    const int segs = kRoundCapSegs;
    for (int k = 0; k < segs; k++) {
      float t0 = a0 + da * float(k) / segs, t1 = a0 + da * float(k + 1) / segs;
      out.push_back(centre.x);
      out.push_back(centre.y);
      out.push_back(centre.x + cosf(t0) * hw);
      out.push_back(centre.y + sinf(t0) * hw);
      out.push_back(centre.x + cosf(t1) * hw);
      out.push_back(centre.y + sinf(t1) * hw);
    }
  };
  auto emitSegQuad = [&](int i, int j) {
    Vec2 L0 = pts[i] + normals[i] * hw, R0 = pts[i] - normals[i] * hw;
    Vec2 L1 = pts[j] + normals[j] * hw, R1 = pts[j] - normals[j] * hw;
    pushQuad(L0, R0, R1, L1);
  };
  auto emitJoinFan = [&](int i) {
    if (join == LineJoinV::Butt)
      return;
    if (miterScales[i] <= kMiterLimit && join == LineJoinV::Miter)
      return;
    int prevSeg = (i - 1 + numSegs) % numSegs, nextSeg = i % numSegs;
    float cr = cross(segN[prevSeg], segN[nextSeg]);
    if (std::abs(cr) < 1e-6f)
      return;
    Vec2 centre = pts[i];
    if (cr > 0.f)
      roundFan(centre, centre + segN[prevSeg] * hw,
               centre + segN[nextSeg] * hw);
    else
      roundFan(centre, centre - segN[prevSeg] * hw,
               centre - segN[nextSeg] * hw);
  };

  for (int i = 0; i < n - 1; i++)
    emitSegQuad(i, i + 1);
  if (closed)
    emitSegQuad(n - 1, 0);
  if (closed) {
    for (int i = 0; i < n; i++)
      emitJoinFan(i);
  } else {
    for (int i = 1; i < n - 1; i++)
      emitJoinFan(i);
  }

  if (!closed) {
    auto roundCap = [&](Vec2 center, Vec2 dir, int segs = kRoundCapSegs) {
      float startA = std::atan2(dir.y, dir.x) - 3.14159265f / 2;
      for (int i = 0; i < segs; i++) {
        float a0 = startA + 3.14159265f * float(i) / segs;
        float a1 = startA + 3.14159265f * float(i + 1) / segs;
        out.push_back(center.x);
        out.push_back(center.y);
        out.push_back(center.x + cosf(a0) * hw);
        out.push_back(center.y + sinf(a0) * hw);
        out.push_back(center.x + cosf(a1) * hw);
        out.push_back(center.y + sinf(a1) * hw);
      }
    };
    auto squareCap = [&](Vec2 center, Vec2 dir) {
      Vec2 ext = dir * hw, nor = perp(dir) * hw;
      Vec2 a = center + nor, b = center - nor, c = b + ext, d = a + ext;
      pushQuad(a, b, c, d);
    };
    Vec2 d0 = norm(pts[1] - pts[0]);
    Vec2 d1 = norm(pts[n - 1] - pts[n - 2]);
    if (cap == LineCapV::Round) {
      roundCap(pts[0], {-d0.x, -d0.y});
      roundCap(pts[n - 1], d1);
    } else if (cap == LineCapV::Square) {
      squareCap(pts[0], {-d0.x, -d0.y});
      squareCap(pts[n - 1], d1);
    }
  }
}

struct ShapeTris {
  std::vector<float> fill, stroke;
};

inline ShapeTris tessShape(const VShape &s) {
  ShapeTris result;
  using namespace vmath;
  Contours contours;
  switch (s.kind) {
  case VShapeKind::Rect:
    contours.push_back(rectContour(s.rect.x, s.rect.y, s.rect.w, s.rect.h,
                                   s.rect.rx, s.rect.ry));
    break;
  case VShapeKind::Ellipse:
    contours.push_back(
        ellipseContour(s.ellipse.cx, s.ellipse.cy, s.ellipse.rx, s.ellipse.ry));
    break;
  case VShapeKind::Poly:
    contours.push_back(s.poly.pts);
    break;
  case VShapeKind::Path:
    flattenPath(s.path, contours);
    break;
  case VShapeKind::Text:
    return result;
  }
  float hw = s.stroke.width * .5f;
  if (hw < 0.001f)
    hw = 0.001f;
  if (!s.fill.none) {
    for (auto &c : contours) {
      std::vector<float> lf;
      earClip(c, lf);
      for (int i = 0; i + 1 < (int)lf.size(); i += 2) {
        Vec2 p = s.xform.apply({lf[i], lf[i + 1]});
        result.fill.push_back(p.x);
        result.fill.push_back(p.y);
      }
    }
  }
  if (!s.stroke.none && s.stroke.width > 0) {
    for (auto &c : contours) {
      bool closed =
          (s.kind == VShapeKind::Rect || s.kind == VShapeKind::Ellipse ||
           (s.kind == VShapeKind::Poly && s.poly.closed));
      LineJoinV ej = s.stroke.join;
      if (s.kind == VShapeKind::Rect)
        ej = LineJoinV::Miter;
      std::vector<float> ls;
      expandStroke(c, closed, hw, s.stroke.cap, ej, ls);
      for (int i = 0; i + 1 < (int)ls.size(); i += 2) {
        Vec2 p = s.xform.apply({ls[i], ls[i + 1]});
        result.stroke.push_back(p.x);
        result.stroke.push_back(p.y);
      }
    }
  }
  return result;
}

inline vmath::AABB shapeAABB(const VShape &s) {
  using namespace vmath;
  Contours cs;
  switch (s.kind) {
  case VShapeKind::Rect:
    cs.push_back(rectContour(s.rect.x, s.rect.y, s.rect.w, s.rect.h, s.rect.rx,
                             s.rect.ry));
    break;
  case VShapeKind::Ellipse:
    cs.push_back(
        ellipseContour(s.ellipse.cx, s.ellipse.cy, s.ellipse.rx, s.ellipse.ry));
    break;
  case VShapeKind::Poly:
    cs.push_back(s.poly.pts);
    break;
  case VShapeKind::Path:
    flattenPath(s.path, cs);
    break;
  case VShapeKind::Text: {
    // Estimate text bounds using fontSize as height and a rough width
    // heuristic. The real pixel size is in the texture cache, but shapeAABB is
    // a free function so we use a conservative estimate: width ≈ nChars *
    // fontSize * 0.6
    float estW = float(s.text.text.size()) * s.text.style.fontSize * 0.6f;
    float estH = float(s.text.style.fontSize) * 1.2f;
    float ax = s.text.x, ay = s.text.y;
    cs.push_back(
        {{ax, ay}, {ax + estW, ay}, {ax + estW, ay + estH}, {ax, ay + estH}});
    break;
  }
  }
  AABB bb;
  for (auto &c : cs)
    for (auto &p : c)
      bb.expand(s.xform.apply(p));
  return bb;
}

// ============================================================================
// §V2b  CLIPPER2 BRIDGE
// ============================================================================

inline Clipper2Lib::PathsD contoursToPaths(const Contours &cs) {
  Clipper2Lib::PathsD out;
  for (auto &c : cs) {
    Clipper2Lib::PathD p;
    for (auto &v : c)
      p.push_back({v.x, v.y});
    out.push_back(p);
  }
  return out;
}

inline Contours pathsToContours(const Clipper2Lib::PathsD &ps) {
  Contours out;
  for (auto &p : ps) {
    Contour c;
    for (auto &pt : p)
      c.push_back({(float)pt.x, (float)pt.y});
    out.push_back(c);
  }
  return out;
}

// Collect world-space contours from any VShape kind
inline Contours shapeToWorldContours(const VShape &s) {
  Contours local;
  switch (s.kind) {
  case VShapeKind::Rect:
    local.push_back(rectContour(s.rect.x, s.rect.y, s.rect.w, s.rect.h,
                                s.rect.rx, s.rect.ry));
    break;
  case VShapeKind::Ellipse:
    local.push_back(
        ellipseContour(s.ellipse.cx, s.ellipse.cy, s.ellipse.rx, s.ellipse.ry));
    break;
  case VShapeKind::Poly:
    local.push_back(s.poly.pts);
    break;
  case VShapeKind::Path:
    flattenPath(s.path, local);
    break;
  default:
    break;
  }
  // Apply xform to get world-space coords
  Contours world;
  for (auto &c : local) {
    Contour wc;
    for (auto &v : c)
      wc.push_back(s.xform.apply(v));
    world.push_back(wc);
  }
  return world;
}

} // namespace vtess

// ============================================================================
// §V3  TOOL ENUM
// ============================================================================

enum class VTool { Select, Pen, Node, Rect, Ellipse, Line, Text };

// ============================================================================
// §V4  UNDO SNAPSHOT
// ============================================================================

struct VSnapshot {
  VScene scene;
  size_t estBytes;
};

// ============================================================================
// §V4c  GUIDES
// ============================================================================

struct Guide {
  enum class Axis { H, V };
  Axis axis;
  float position = 0.f; // canvas-space coordinate
  bool locked = false;
};

// ============================================================================
// §V4b  TEXT SESSION
// ============================================================================

struct VTextSession {
  bool active = false;
  float x = 0.f, y = 0.f; // canvas-space anchor
  std::wstring text;
  TextStyle style;
};

// ============================================================================
// §V5  TRANSFORM HANDLE SYSTEM
// ============================================================================

enum class HandleKind { None, Scale, Rotate, Origin };

struct TransformHandle {
  HandleKind kind = HandleKind::None;
  int index = -1;
};

struct ScaleConstraint {
  bool lockX, lockY;
};
static constexpr ScaleConstraint kScaleConstraints[8] = {
    {false, false}, {true, false},  {false, false}, {false, true},
    {false, true},  {false, false}, {true, false},  {false, false},
};

// ============================================================================
// §V6  VECTOR SURFACE
// ============================================================================

class VectorSurface : public RenderSurface {
public:
  static constexpr size_t kDefaultUndoBudget = 64ULL * 1024 * 1024;
  static constexpr size_t kBytesPerPoint = 32;

  explicit VectorSurface(size_t budget = kDefaultUndoBudget)
      : undoBudget_(budget) {}
  ~VectorSurface() { destroy(); }

  // ── Scene access ──────────────────────────────────────────────────────────
  VScene &scene() { return scene_; }
  const VScene &scene() const { return scene_; }

  VShapeId addShape(VShape s) {
    s.id = nextId_++;
    pushUndo();
    scene_.push_back(std::move(s));
    dirty_ = true;
    return scene_.back().id;
  }
  void removeShape(VShapeId id) {
    auto it = std::find_if(scene_.begin(), scene_.end(),
                           [id](auto &s) { return s.id == id; });
    if (it == scene_.end())
      return;
    pushUndo();
    scene_.erase(it);
    selection_.erase(std::remove(selection_.begin(), selection_.end(), id),
                     selection_.end());
    dirty_ = true;
  }
  VShape *beginEdit(VShapeId id) {
    auto it = std::find_if(scene_.begin(), scene_.end(),
                           [id](auto &s) { return s.id == id; });
    return it == scene_.end() ? nullptr : &*it;
  }
  void endEdit() { dirty_ = true; }

  // ── Selection ─────────────────────────────────────────────────────────────
  const std::vector<VShapeId> &selection() const { return selection_; }
  void select(VShapeId id, bool additive = false) {
    if (!additive)
      selection_.clear();
    if (id != kNoShape &&
        std::find(selection_.begin(), selection_.end(), id) == selection_.end())
      selection_.push_back(id);
    dirty_ = true;
  }
  void selectAll() {
    selection_.clear();
    for (auto &s : scene_)
      selection_.push_back(s.id);
    dirty_ = true;
  }
  void deselectAll() {
    selection_.clear();
    transformMode_ = TransformMode::Scale;
    dirty_ = true;
  }

  // ── Tool ──────────────────────────────────────────────────────────────────
  void setTool(VTool t) {
    // Commit any open text session when switching tools
    if (tool_ == VTool::Text && textSession_.active)
      commitTextSession();
    tool_ = t;
    cancelInProgress();
    dirty_ = true;
  }
  void setTransformMode(const std::string &m) {
    if (m == "rotate")
      transformMode_ = TransformMode::Rotate;
    else if (m == "move")
      transformMode_ = TransformMode::Move;
    else
      transformMode_ = TransformMode::Scale;
    originSet_ = false;
    if (!selection_.empty())
      transformOrigin_ = selectionBB().center();
    dirty_ = true;
  }

  VTool getTool() const { return tool_; }

  // ── Active style ──────────────────────────────────────────────────────────
  VFill &activeFill() { return activeFill_; }
  VStroke &activeStroke() { return activeStroke_; }
  void setActiveFill(const VFill &f) { activeFill_ = f; }
  void setActiveStroke(const VStroke &s) { activeStroke_ = s; }

  // ── Text style ────────────────────────────────────────────────────────────
  void setTextStyle(const TextStyle &ts) {
    activeTextStyle_ = ts;
    // Live-update current session if active
    if (textSession_.active) {
      textSession_.style = ts;
      dirty_ = true;
    }
  }
  const TextStyle &getTextStyle() const { return activeTextStyle_; }

  // ── Undo / Redo ───────────────────────────────────────────────────────────
  void undo() {
    if (undoStack_.empty())
      return;
    VSnapshot fwd;
    fwd.scene = scene_;
    fwd.estBytes = estimateSceneBytes();
    redoStack_.push_back(std::move(fwd));
    VSnapshot &s = undoStack_.back();
    scene_ = std::move(s.scene);
    undoBytes_ -= s.estBytes;
    undoStack_.pop_back();
    selection_.clear();
    dirty_ = true;
  }
  void redo() {
    if (redoStack_.empty())
      return;
    pushUndo();
    VSnapshot &s = redoStack_.back();
    scene_ = std::move(s.scene);
    redoStack_.pop_back();
    selection_.clear();
    dirty_ = true;
  }
  bool canUndo() const { return !undoStack_.empty(); }
  bool canRedo() const { return !redoStack_.empty(); }

  void setZoom(float z) { currentZoom_ = z > 0.f ? z : 1.f; }
  float currentZoom() const { return currentZoom_; }

  // ── Z-order ───────────────────────────────────────────────────────────────
  void bringToFront(VShapeId id) {
    auto it = std::find_if(scene_.begin(), scene_.end(),
                           [id](auto &s) { return s.id == id; });
    if (it == scene_.end() || it == scene_.end() - 1)
      return;
    pushUndo();
    std::rotate(it, it + 1, scene_.end());
    dirty_ = true;
  }
  void sendToBack(VShapeId id) {
    auto it = std::find_if(scene_.begin(), scene_.end(),
                           [id](auto &s) { return s.id == id; });
    if (it == scene_.end() || it == scene_.begin())
      return;
    pushUndo();
    std::rotate(scene_.begin(), it, it + 1);
    dirty_ = true;
  }

  // ── needsContinuousRedraw — cursor blink ───────────────────────────────
  bool needsContinuousRedraw() const override { return textSession_.active; }

  // ── RenderSurface interface ───────────────────────────────────────────────
  void initialize(int w, int h) override {
    w_ = w;
    h_ = h;
    buildShaders();
    buildBuffers();
    if (!gdiplusToken_) {
      Gdiplus::GdiplusStartupInput gsi;
      Gdiplus::GdiplusStartup(&gdiplusToken_, &gsi, nullptr);
    }
    dirty_ = true;
  }
  void resize(int w, int h) override {
    w_ = w;
    h_ = h;
    dirty_ = true;
    textTexCache_.clear();
  }
  void update(double dt) override {
    // Advance text cursor blink
    if (textSession_.active) {
      blinkAccum_ += dt;
      if (blinkAccum_ >= 0.5) {
        blinkAccum_ = 0.0;
        blinkVisible_ = !blinkVisible_;
        dirty_ = true;
      }
    }
  }

  void render(const float mvp[16]) override {
    glDisable(GL_SCISSOR_TEST);
    if (mvp[0] > 1e-9f)
      currentOffsetX_ = (-1.f - mvp[12]) / mvp[0];
    if (mvp[5] > 1e-9f)
      currentOffsetY_ = (-1.f - mvp[13]) / mvp[5];

    glClearColor(pasteboard_.r, pasteboard_.g, pasteboard_.b, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (showArtboard_) {
      const float kShadowOff = 6.f;
      std::vector<float> shadowQuad = {
          0.f + kShadowOff,       0.f - kShadowOff,
          float(w_) + kShadowOff, 0.f - kShadowOff,
          float(w_) + kShadowOff, float(h_) - kShadowOff,
          float(w_) + kShadowOff, float(h_) - kShadowOff,
          0.f + kShadowOff,       float(h_) - kShadowOff,
          0.f + kShadowOff,       0.f - kShadowOff,
      };
      drawTris(shadowQuad, shadowColor_, mvp);
      std::vector<float> aq = {
          0.f,       0.f,       float(w_), 0.f,       float(w_), float(h_),
          float(w_), float(h_), 0.f,       float(h_), 0.f,       0.f,
      };
      drawTris(aq, artboardColor_, mvp);
    }

    if (snapToGrid_ && showGuides_)
      renderGrid(mvp);

    for (auto &shape : scene_)
      renderShape(shape, mvp);
    renderGhost(mvp);

    if (showGuides_)
      renderGuides(mvp);
    if (showRulers_)
      renderRulers(mvp);

    renderSnapFeedback(mvp);

    // ── Text session live preview ─────────────────────────────────────────
    if (textSession_.active)
      renderTextSession(mvp);

    if (showArtboard_) {
      RGBA borderCol = {0.18f, 0.18f, 0.18f, 1.f};
      drawBorderRect(0.f, 0.f, float(w_), float(h_), 1.f, borderCol, mvp);
    }

    renderSelection(mvp);
    glDisable(GL_BLEND);
    glUseProgram(0);
    dirty_ = false;
  }

  void onMouseDown(float x, float y) override {
    mx_ = x;
    my_ = y;
    mdownX_ = x;
    mdownY_ = y;

    // ── Ruler drag → create new guide ────────────────────────────────────
    if (showRulers_) {
      bool inHRuler = (y >= 0.f && y < kRulerSize && x >= kRulerSize);
      bool inVRuler = (x >= 0.f && x < kRulerSize && y >= kRulerSize);
      if (inHRuler) {
        draggingNewGuide_ = true;
        newGuideAxis_ = Guide::Axis::H;
        newGuidePos_ = y;
        dirty_ = true;
        return;
      }
      if (inVRuler) {
        draggingNewGuide_ = true;
        newGuideAxis_ = Guide::Axis::V;
        newGuidePos_ = x;
        dirty_ = true;
        return;
      }
    }

    // ── Click existing guide → start moving it ────────────────────────────
    if (showGuides_) {
      int gi = hitTestGuide(x, y);
      if (gi >= 0) {
        draggingExistGuide_ = true;
        draggingGuideIdx_ = gi;
        dirty_ = true;
        return;
      }
    }

    switch (tool_) {
    case VTool::Select:
      onSelectDown(x, y);
      break;
    case VTool::Pen:
      onPenDown(x, y);
      break;
    case VTool::Node:
      onNodeDown(x, y);
      break;
    case VTool::Text:
      onTextDown(x, y);
      break;
    case VTool::Rect:
    case VTool::Ellipse:
    case VTool::Line:
      onShapeDown(x, y);
      break;
    }
    dirty_ = true;
  }

  void onMouseMove(float x, float y) override {
    float dx = x - mx_, dy = y - my_;
    mx_ = x;
    my_ = y;

    // New guide being dragged out of ruler
    if (draggingNewGuide_) {
      newGuidePos_ = (newGuideAxis_ == Guide::Axis::H) ? y : x;
      dirty_ = true;
      return;
    }
    // Existing guide being repositioned
    if (draggingExistGuide_ && draggingGuideIdx_ >= 0) {
      auto &g = guides_[draggingGuideIdx_];
      g.position = (g.axis == Guide::Axis::H) ? y : x;
      dirty_ = true;
      return;
    }
    // Update hovered guide for cursor feedback
    hoveredGuide_ = showGuides_ ? hitTestGuide(x, y) : -1;

    switch (tool_) {
    case VTool::Select:
      onSelectMove(x, y, dx, dy);
      break;
    case VTool::Pen:
      onPenMove(x, y);
      break;
    case VTool::Node:
      onNodeMove(x, y, dx, dy);
      break;
    case VTool::Text:
      break;
    case VTool::Rect:
    case VTool::Ellipse:
    case VTool::Line:
      onShapeMove(x, y);
      break;
    }
    dirty_ = true;
  }

  void onMouseUp(float x, float y) override {
    // Commit new guide dragged from ruler
    if (draggingNewGuide_) {
      float pos = (newGuideAxis_ == Guide::Axis::H) ? y : x;
      // Only add if dragged onto the canvas
      bool onCanvas = (newGuideAxis_ == Guide::Axis::H)
                          ? (pos > kRulerSize && pos < float(h_))
                          : (pos > kRulerSize && pos < float(w_));
      if (onCanvas)
        guides_.push_back({newGuideAxis_, pos});
      draggingNewGuide_ = false;
      dirty_ = true;
      return;
    }
    // Commit moved guide — delete if dragged back into ruler
    if (draggingExistGuide_ && draggingGuideIdx_ >= 0) {
      auto &g = guides_[draggingGuideIdx_];
      bool offCanvas = (g.axis == Guide::Axis::H) ? (g.position < kRulerSize)
                                                  : (g.position < kRulerSize);
      if (offCanvas)
        guides_.erase(guides_.begin() + draggingGuideIdx_);
      draggingExistGuide_ = false;
      draggingGuideIdx_ = -1;
      dirty_ = true;
      return;
    }

    switch (tool_) {
    case VTool::Select:
      onSelectUp(x, y);
      break;
    case VTool::Pen:
      break;
    case VTool::Node:
      onNodeUp();
      break;
    case VTool::Text:
      break;
    case VTool::Rect:
    case VTool::Ellipse:
    case VTool::Line:
      onShapeUp(x, y);
      break;
    }
    draggingShape_ = false;
    draggingBox_ = false;
    draggingNode_ = false;
    draggingHandle_ = false;
    dirty_ = true;
  }

  void onKeyDown(int key) override {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Text tool key handling takes priority
    if (tool_ == VTool::Text && textSession_.active) {
      handleTextKey(key);
      return;
    }

    if (ctrl && key == 'Z') {
      if (shift)
        redo();
      else
        undo();
    }
    if (ctrl && key == 'Y')
      redo();
    if (ctrl && key == 'A')
      selectAll();
    if (ctrl && key == VK_OEM_1) // Ctrl+;
      setShowGuides(!showGuides_);
    if (key == 'S' && !ctrl)
      setSnapEnabled(!snapEnabled_);
    if (key == VK_ESCAPE) {
      if (tool_ == VTool::Pen && penInProgress_)
        commitPen();
      else if (tool_ == VTool::Text && textSession_.active)
        cancelTextSession();
      else
        deselectAll();
    }
    if (key == VK_DELETE || key == VK_BACK) {
      for (auto id : selection_)
        removeShapeNoUndo(id);
      pushUndo();
      selection_.clear();
    }
    float nudge = shift ? 10.f : 1.f;
    if (!selection_.empty()) {
      float dx = 0, dy = 0;
      if (key == VK_LEFT)
        dx = -nudge;
      if (key == VK_RIGHT)
        dx = nudge;
      if (key == VK_UP)
        dy = nudge;
      if (key == VK_DOWN)
        dy = -nudge;
      if (dx || dy)
        translateSelected(dx, dy);
    }
    dirty_ = true;
  }
  void onKeyUp(int) override {}

  void onRightMouseDown(float x, float y) override {
    if (tool_ == VTool::Pen && penInProgress_)
      commitPen();
    else if (tool_ == VTool::Text && textSession_.active)
      cancelTextSession();
    else
      deselectAll();
    dirty_ = true;
  }

  void destroy() override {
    if (prog_) {
      glDeleteProgram(prog_);
      prog_ = 0;
    }
    if (vao_) {
      glDeleteVertexArrays(1, &vao_);
      vao_ = 0;
    }
    if (vbo_) {
      glDeleteBuffers(1, &vbo_);
      vbo_ = 0;
    }
    for (auto &[k, v] : textTexCache_)
      glDeleteTextures(1, &v.tex);
    textTexCache_.clear();
    if (gdiplusToken_) {
      Gdiplus::GdiplusShutdown(gdiplusToken_);
      gdiplusToken_ = 0;
    }
  }

  void setShowArtboard(bool v) {
    showArtboard_ = v;
    dirty_ = true;
  }

  // ── Guides public API ─────────────────────────────────────────────────────
  void addGuide(Guide::Axis axis, float pos) {
    guides_.push_back({axis, pos});
    dirty_ = true;
  }
  void removeGuide(int idx) {
    if (idx >= 0 && idx < (int)guides_.size()) {
      guides_.erase(guides_.begin() + idx);
      dirty_ = true;
    }
  }
  void clearGuides() {
    guides_.clear();
    dirty_ = true;
  }
  const std::vector<Guide> &guides() const { return guides_; }

  void setSnapEnabled(bool v) {
    snapEnabled_ = v;
    dirty_ = true;
  }
  void setSnapToGuides(bool v) {
    snapToGuides_ = v;
    dirty_ = true;
  }
  void setSnapToObjects(bool v) {
    snapToObjects_ = v;
    dirty_ = true;
  }
  void setSnapToGrid(bool v) {
    snapToGrid_ = v;
    dirty_ = true;
  }
  void setGridSize(float v) {
    gridSize_ = max(4.f, v);
    dirty_ = true;
  }
  void setShowGuides(bool v) {
    showGuides_ = v;
    dirty_ = true;
  }
  void setShowRulers(bool v) {
    showRulers_ = v;
    dirty_ = true;
  }

  bool snapEnabled() const { return snapEnabled_; }
  bool showGuides() const { return showGuides_; }
  bool showRulers() const { return showRulers_; }
  bool snapToGrid() const { return snapToGrid_; }
  float gridSize() const { return gridSize_; }

  bool showArtboard() const { return showArtboard_; }
  // ── Boolean operations (Clipper2) ─────────────────────────────────────────
  enum class BooleanOp { Unite, Subtract, Intersect, Xor };

  void booleanOp(BooleanOp op) {
    if (selection_.size() < 2)
      return;

    // Build subject (first selected) and clip (rest), sorted by scene order
    std::vector<VShapeId> ordered;
    for (auto &s : scene_)
      if (std::find(selection_.begin(), selection_.end(), s.id) !=
          selection_.end())
        ordered.push_back(s.id);

    Clipper2Lib::PathsD subject, clip;
    bool first = true;
    for (auto id : ordered) {
      auto *s = beginEdit(id);
      if (!s)
        continue;
      auto world = vtess::shapeToWorldContours(*s);
      auto paths = vtess::contoursToPaths(world);
      if (first) {
        for (auto &p : paths)
          subject.push_back(p);
        first = false;
      } else {
        for (auto &p : paths)
          clip.push_back(p);
      }
    }
    if (subject.empty())
      return;

    Clipper2Lib::FillRule fr = Clipper2Lib::FillRule::NonZero;
    Clipper2Lib::PathsD result;
    switch (op) {
    case BooleanOp::Unite:
      result = Clipper2Lib::Union(subject, clip, fr);
      break;
    case BooleanOp::Subtract:
      result = Clipper2Lib::Difference(subject, clip, fr);
      break;
    case BooleanOp::Intersect:
      result = Clipper2Lib::Intersect(subject, clip, fr);
      break;
    case BooleanOp::Xor:
      result = Clipper2Lib::Xor(subject, clip, fr);
      break;
    }
    if (result.empty())
      return;

    pushUndo();
    for (auto id : selection_)
      removeShapeNoUndo(id);

    VShape out;
    out.id = nextId_++;
    out.kind = VShapeKind::Path;
    out.fill = activeFill_;
    out.stroke = activeStroke_;
    for (auto &path : result) {
      if (path.empty())
        continue;
      out.path.cmds.push_back(
          {PathCmdKind::MoveTo, {{(float)path[0].x, (float)path[0].y}}});
      for (size_t i = 1; i < path.size(); i++)
        out.path.cmds.push_back(
            {PathCmdKind::LineTo, {{(float)path[i].x, (float)path[i].y}}});
      out.path.cmds.push_back({PathCmdKind::Close});
    }
    scene_.push_back(out);
    selection_ = {scene_.back().id};
    dirty_ = true;
  }

  void outlineStroke(VShapeId id) {
    auto *s = beginEdit(id);
    if (!s || s->stroke.none || s->stroke.width <= 0)
      return;

    auto world = vtess::shapeToWorldContours(*s);
    if (world.empty())
      return;
    auto paths = vtess::contoursToPaths(world);

    Clipper2Lib::JoinType jt = Clipper2Lib::JoinType::Round;
    if (s->stroke.join == LineJoinV::Miter)
      jt = Clipper2Lib::JoinType::Miter;
    if (s->stroke.join == LineJoinV::Bevel)
      jt = Clipper2Lib::JoinType::Bevel;
    Clipper2Lib::EndType et = Clipper2Lib::EndType::Round;
    if (s->stroke.cap == LineCapV::Butt)
      et = Clipper2Lib::EndType::Butt;
    if (s->stroke.cap == LineCapV::Square)
      et = Clipper2Lib::EndType::Square;
    bool isClosed =
        (s->kind == VShapeKind::Rect || s->kind == VShapeKind::Ellipse ||
         (s->kind == VShapeKind::Poly && s->poly.closed));
    if (isClosed)
      et = Clipper2Lib::EndType::Polygon;
    double hw = s->stroke.width * 0.5;
    Clipper2Lib::PathsD outer = Clipper2Lib::InflatePaths(paths, hw, jt, et);
    Clipper2Lib::PathsD inner = Clipper2Lib::InflatePaths(paths, -hw, jt, et);
    Clipper2Lib::PathsD stroked =
        Clipper2Lib::Difference(outer, inner, Clipper2Lib::FillRule::NonZero);
    if (stroked.empty())
      return;

    pushUndo();
    VFill newFill = s->fill;
    newFill.color = s->stroke.color;
    newFill.none = false;
    VStroke newStroke = s->stroke;
    newStroke.none = true;
    removeShapeNoUndo(id);

    VShape out;
    out.id = nextId_++;
    out.kind = VShapeKind::Path;
    out.fill = newFill;
    out.stroke = newStroke;
    for (auto &path : stroked) {
      if (path.empty())
        continue;
      out.path.cmds.push_back(
          {PathCmdKind::MoveTo, {{(float)path[0].x, (float)path[0].y}}});
      for (size_t i = 1; i < path.size(); i++)
        out.path.cmds.push_back(
            {PathCmdKind::LineTo, {{(float)path[i].x, (float)path[i].y}}});
      out.path.cmds.push_back({PathCmdKind::Close});
    }
    scene_.push_back(out);
    selection_ = {scene_.back().id};
    dirty_ = true;
  }

  void clipperCleanFill(const vtess::Contours &contours, VFillRule fr,
                        std::vector<float> &outTris) {
    auto paths = vtess::contoursToPaths(contours);
    Clipper2Lib::FillRule cfr = (fr == VFillRule::EvenOdd)
                                    ? Clipper2Lib::FillRule::EvenOdd
                                    : Clipper2Lib::FillRule::NonZero;
    Clipper2Lib::PathsD cleaned = Clipper2Lib::Union(paths, cfr);
    auto cleanedC = vtess::pathsToContours(cleaned);
    for (auto &c : cleanedC)
      vtess::earClip(c, outTris);
  }

private:
  int w_ = 512, h_ = 512;
  bool dirty_ = false;
  bool showArtboard_ = true;
  RGBA pasteboard_ = {0.314f, 0.314f, 0.314f, 1.f};
  RGBA artboardColor_ = {1.f, 1.f, 1.f, 1.f};
  RGBA shadowColor_ = {0.f, 0.f, 0.f, 0.32f};

  VScene scene_;
  VShapeId nextId_ = 1;
  std::vector<VShapeId> selection_;
  VTool tool_ = VTool::Select;
  VFill activeFill_ = {{0.2f, 0.5f, 1.f, 1.f}, VFillRule ::NonZero, false};
  VStroke activeStroke_ = {{0.f, 0.f, 0.f, 1.f}, 2.f, 0.f,  LineCapV::Round,
                           LineJoinV::Round,     {},  false};
  TextStyle activeTextStyle_; // text style for the text tool

  size_t undoBudget_, undoBytes_ = 0;
  std::vector<VSnapshot> undoStack_, redoStack_;

  float mx_ = 0, my_ = 0, mdownX_ = 0, mdownY_ = 0;

  // ── Select state ─────────────────────────────────────────────────────────
  bool draggingShape_ = false, draggingBox_ = false;
  float boxX0_ = 0, boxY0_ = 0, boxX1_ = 0, boxY1_ = 0;
  std::vector<std::pair<VShapeId, vmath::Mat3>> dragOrigins_;

  // ── Transform handle state ───────────────────────────────────────────────
  enum class TransformMode { Scale, Rotate, Move };
  TransformMode transformMode_ = TransformMode::Scale;
  bool draggingHandle_ = false;
  HandleKind dragHandleKind_ = HandleKind::None;
  int dragHandleIndex_ = -1;
  vmath::AABB dragStartBB_;
  std::vector<std::pair<VShapeId, vmath::Mat3>> handleDragOrigins_;
  float rotateStartAngle_ = 0.f;
  vmath::Vec2 transformOrigin_ = {0, 0};
  bool originSet_ = false;
  bool draggingOrigin_ = false;

  // ── Pen state ─────────────────────────────────────────────────────────────
  bool penInProgress_ = false;
  std::vector<vmath::Vec2> penAnchors_, penCPs_;
  vmath::Vec2 penCursor_{};

  // ── Node state ────────────────────────────────────────────────────────────
  bool draggingNode_ = false;
  int dragNodeIdx_ = -1, dragNodeCPSide_ = 0;
  bool dragNodeIsCP_ = false;

  // ── Shape drag-create state ───────────────────────────────────────────────
  bool shapeDragging_ = false;
  float shapeX0_ = 0, shapeY0_ = 0;
  VShapeId ghostId_ = kNoShape;

  // ── Text session state ────────────────────────────────────────────────────
  VTextSession textSession_;
  bool blinkVisible_ = true;
  double blinkAccum_ = 0.0;

  // ── Guides & Snap ─────────────────────────────────────────────────────────
  std::vector<Guide> guides_;
  bool snapEnabled_ = true;
  bool snapToGuides_ = true;
  bool snapToObjects_ = true;
  bool snapToGrid_ = false;
  float gridSize_ = 20.f;
  bool showGuides_ = true;
  bool showRulers_ = true;
  static constexpr float kRulerSize = 18.f;

  // Guide drag-create state
  bool draggingNewGuide_ = false;
  Guide::Axis newGuideAxis_ = Guide::Axis::V;
  float newGuidePos_ = 0.f;

  // Guide drag-move state
  int hoveredGuide_ = -1; // index into guides_, -1 = none
  int draggingGuideIdx_ = -1;
  bool draggingExistGuide_ = false;

  // Snap feedback (for rendering the snap highlight this frame)
  bool snapFiredX_ = false, snapFiredY_ = false;
  float snapLineX_ = 0.f, snapLineY_ = 0.f;

  // ── GL ────────────────────────────────────────────────────────────────────
  GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
  float currentZoom_ = 1.f;
  float currentOffsetX_ = 0.f;
  float currentOffsetY_ = 0.f;
  struct ULocs {
    GLint mvp = -1, mode = -1, color = -1, tex = -1, alpha = -1;
    // Gradient uniforms
    GLint gStart = -1, gEnd = -1;
    GLint gColors = -1, gPositions = -1, gStopCount = -1;
    // Shape AABB for object-space gradient mapping
    GLint shapeBBMin = -1, shapeBBMax = -1;
  } u_;

  struct TextTex {
    GLuint tex = 0;
    int w = 0, h = 0;
    float ox = 0, oy = 0;
    int pxW = 0, pxH = 0;
  };
  std::unordered_map<uint64_t, TextTex> textTexCache_;
  ULONG_PTR gdiplusToken_ = 0;

  // =========================================================================
  // ── renderGrid ────────────────────────────────────────────────────────────
  void renderGrid(const float mvp[16]) {
    RGBA col = {0.5f, 0.5f, 0.5f, 0.15f};
    float lw = 1.f / currentZoom_;
    std::vector<float> tris;
    for (float gx = 0.f; gx <= float(w_); gx += gridSize_) {
      vtess::expandStroke({{gx, 0.f}, {gx, float(h_)}}, false, lw * .5f,
                          LineCapV::Butt, LineJoinV::Miter, tris);
    }
    for (float gy = 0.f; gy <= float(h_); gy += gridSize_) {
      vtess::expandStroke({{0.f, gy}, {float(w_), gy}}, false, lw * .5f,
                          LineCapV::Butt, LineJoinV::Miter, tris);
    }
    drawTris(tris, col, mvp);
  }

  // =========================================================================
  // ── renderGuides ──────────────────────────────────────────────────────────
  void renderGuides(const float mvp[16]) {
    float lw = 1.f / currentZoom_;
    for (int i = 0; i < (int)guides_.size(); i++) {
      auto &g = guides_[i];
      bool hovered = (i == hoveredGuide_ || i == draggingGuideIdx_);
      RGBA col = hovered ? RGBA{0.2f, 0.9f, 1.f, 0.95f}
                         : RGBA{0.2f, 0.55f, 1.f, 0.55f};
      std::vector<float> tris;
      if (g.axis == Guide::Axis::H) {
        vtess::expandStroke({{0.f, g.position}, {float(w_), g.position}}, false,
                            lw * .5f, LineCapV::Butt, LineJoinV::Miter, tris);
      } else {
        vtess::expandStroke({{g.position, 0.f}, {g.position, float(h_)}}, false,
                            lw * .5f, LineCapV::Butt, LineJoinV::Miter, tris);
      }
      drawTris(tris, col, mvp);
    }

    // Draw the new guide preview while dragging from ruler
    if (draggingNewGuide_) {
      RGBA preCol = {0.4f, 1.f, 0.6f, 0.75f};
      std::vector<float> tris;
      if (newGuideAxis_ == Guide::Axis::H) {
        vtess::expandStroke({{0.f, newGuidePos_}, {float(w_), newGuidePos_}},
                            false, lw * .5f, LineCapV::Butt, LineJoinV::Miter,
                            tris);
      } else {
        vtess::expandStroke({{newGuidePos_, 0.f}, {newGuidePos_, float(h_)}},
                            false, lw * .5f, LineCapV::Butt, LineJoinV::Miter,
                            tris);
      }
      drawTris(tris, preCol, mvp);
    }
  }

  // =========================================================================
  // ── renderRulers ──────────────────────────────────────────────────────────
  void renderRulers(const float mvp[16]) {
    float rs = kRulerSize / currentZoom_;
    RGBA rulerBg = {0.18f, 0.18f, 0.18f, 1.f};
    RGBA rulerTick = {0.55f, 0.55f, 0.55f, 1.f};
    RGBA corner = {0.14f, 0.14f, 0.14f, 1.f};

    // Horizontal ruler (top)
    std::vector<float> hRuler = {0.f,       -rs, float(w_), -rs,
                                 float(w_), 0.f, float(w_), 0.f,
                                 0.f,       0.f, 0.f,       -rs};
    drawTris(hRuler, rulerBg, mvp);

    // Vertical ruler (left)
    std::vector<float> vRuler = {-rs, 0.f,       0.f, 0.f,       0.f, float(h_),
                                 0.f, float(h_), -rs, float(h_), -rs, 0.f};
    drawTris(vRuler, rulerBg, mvp);

    // Corner square
    std::vector<float> cornerQ = {-rs, -rs, 0.f, -rs, 0.f, 0.f,
                                  0.f, 0.f, -rs, 0.f, -rs, -rs};
    drawTris(cornerQ, corner, mvp);

    // Tick marks — every gridSize_ canvas units
    float tickStep = gridSize_;
    float lw = 0.5f / currentZoom_;
    std::vector<float> ticks;

    // Horizontal ruler ticks
    for (float gx = 0.f; gx <= float(w_); gx += tickStep) {
      bool major = (std::fmod(gx, tickStep * 5) < 0.5f);
      float th = major ? rs * 0.55f : rs * 0.3f;
      vtess::expandStroke({{gx, -rs}, {gx, -rs + th}}, false, lw,
                          LineCapV::Butt, LineJoinV::Miter, ticks);
    }
    // Vertical ruler ticks
    for (float gy = 0.f; gy <= float(h_); gy += tickStep) {
      bool major = (std::fmod(gy, tickStep * 5) < 0.5f);
      float th = major ? rs * 0.55f : rs * 0.3f;
      vtess::expandStroke({{-rs, gy}, {-rs + th, gy}}, false, lw,
                          LineCapV::Butt, LineJoinV::Miter, ticks);
    }
    drawTris(ticks, rulerTick, mvp);

    // Mouse position cursor line on rulers
    std::vector<float> cursorTris;
    float lw2 = 1.f / currentZoom_;
    vtess::expandStroke({{mx_, -rs}, {mx_, 0.f}}, false, lw2, LineCapV::Butt,
                        LineJoinV::Miter, cursorTris);
    vtess::expandStroke({{-rs, my_}, {0.f, my_}}, false, lw2, LineCapV::Butt,
                        LineJoinV::Miter, cursorTris);
    drawTris(cursorTris, {1.f, 0.4f, 0.1f, 0.85f}, mvp);
  }

  // =========================================================================
  // ── renderSnapFeedback ────────────────────────────────────────────────────
  void renderSnapFeedback(const float mvp[16]) {
    if (!snapEnabled_)
      return;
    float lw = 1.f / currentZoom_;
    if (snapFiredX_) {
      std::vector<float> tris;
      vtess::expandStroke({{snapLineX_, 0.f}, {snapLineX_, float(h_)}}, false,
                          lw, LineCapV::Butt, LineJoinV::Miter, tris);
      drawTris(tris, {0.f, 1.f, 0.9f, 0.75f}, mvp);
    }
    if (snapFiredY_) {
      std::vector<float> tris;
      vtess::expandStroke({{0.f, snapLineY_}, {float(w_), snapLineY_}}, false,
                          lw, LineCapV::Butt, LineJoinV::Miter, tris);
      drawTris(tris, {0.f, 1.f, 0.9f, 0.75f}, mvp);
    }
  }

  // Returns guide index under screen point, -1 if none
  int hitTestGuide(float x, float y) const {
    float thresh = 5.f / currentZoom_;
    for (int i = (int)guides_.size() - 1; i >= 0; i--) {
      auto &g = guides_[i];
      if (g.locked)
        continue;
      float d = (g.axis == Guide::Axis::H) ? std::abs(g.position - y)
                                           : std::abs(g.position - x);
      if (d < thresh)
        return i;
    }
    return -1;
  }

  // =========================================================================
  // ── Handle geometry helpers ───────────────────────────────────────────────
  static void getHandlePositions(const vmath::AABB &bb, float pad,
                                 vmath::Vec2 out[8]) {
    float x0 = bb.x0 - pad, y0 = bb.y0 - pad, x1 = bb.x1 + pad,
          y1 = bb.y1 + pad;
    float mx = (x0 + x1) * .5f, my = (y0 + y1) * .5f;
    out[0] = {x0, y0};
    out[1] = {mx, y0};
    out[2] = {x1, y0};
    out[3] = {x0, my};
    out[4] = {x1, my};
    out[5] = {x0, y1};
    out[6] = {mx, y1};
    out[7] = {x1, y1};
  }

  vmath::AABB selectionBB() const {
    vmath::AABB bb;
    for (auto id : selection_) {
      auto it = std::find_if(scene_.begin(), scene_.end(),
                             [id](auto &s) { return s.id == id; });
      if (it != scene_.end()) {
        auto sb = vtess::shapeAABB(*it);
        if (sb.valid()) {
          bb.expand({sb.x0, sb.y0});
          bb.expand({sb.x1, sb.y1});
        }
      }
    }
    return bb;
  }

  int hitTestHandles(float x, float y, float pad) const {
    if (selection_.empty())
      return -1;
    auto bb = selectionBB();
    if (!bb.valid())
      return -1;
    vmath::Vec2 pts[8];
    getHandlePositions(bb, pad, pts);
    float r = 8.f / currentZoom_;
    for (int i = 0; i < 8; i++) {
      float dx = pts[i].x - x, dy = pts[i].y - y;
      if (dx * dx + dy * dy < r * r)
        return i;
    }
    return -1;
  }
  bool hitTestOrigin(float x, float y) const {
    if (!originSet_ || selection_.empty())
      return false;
    float r = 7.f / currentZoom_;
    float dx = transformOrigin_.x - x, dy = transformOrigin_.y - y;
    return dx * dx + dy * dy < r * r;
  }

  // =========================================================================
  // ── Undo helpers ──────────────────────────────────────────────────────────
  size_t estimateSceneBytes() const {
    size_t n = 0;
    for (auto &s : scene_) {
      n += s.poly.pts.size() * kBytesPerPoint;
      n += s.path.cmds.size() * kBytesPerPoint * 3;
      n += s.text.text.size() * 2;
    }
    return n + scene_.size() * 256;
  }
  void pushUndo() {
    redoStack_.clear();
    size_t est = estimateSceneBytes();
    while (undoBudget_ > 0 && !undoStack_.empty() &&
           undoBytes_ + est > undoBudget_) {
      undoBytes_ -= undoStack_.front().estBytes;
      undoStack_.erase(undoStack_.begin());
    }
    undoStack_.push_back({scene_, est});
    undoBytes_ += est;
  }
  void removeShapeNoUndo(VShapeId id) {
    auto it = std::find_if(scene_.begin(), scene_.end(),
                           [id](auto &s) { return s.id == id; });
    if (it != scene_.end())
      scene_.erase(it);
  }
  void translateSelected(float dx, float dy) {
    pushUndo();
    for (auto id : selection_) {
      auto *s = beginEdit(id);
      if (!s)
        continue;
      s->xform.m[6] += dx;
      s->xform.m[7] += dy;
    }
    endEdit();
  }

  vmath::Vec2 clampToArtboard(float x, float y) const {
    return {std::clamp(x, 0.f, float(w_)), std::clamp(y, 0.f, float(h_))};
  }

  // =========================================================================
  // ── Snap resolver ─────────────────────────────────────────────────────────

  // Returns nearest snapped X, sets snapFiredX_ if a snap occurred
  float snapX(float cx, VShapeId excludeId = kNoShape) {
    if (!snapEnabled_)
      return cx;
    float thresh = 6.f / currentZoom_;
    float best = cx, bestD = thresh;

    auto tryX = [&](float candidate) {
      float d = std::abs(candidate - cx);
      if (d < bestD) {
        bestD = d;
        best = candidate;
      }
    };

    // Grid
    if (snapToGrid_) {
      float g = std::round(cx / gridSize_) * gridSize_;
      tryX(g);
    }
    // Guides
    if (snapToGuides_) {
      for (auto &guide : guides_)
        if (guide.axis == Guide::Axis::V)
          tryX(guide.position);
    }
    // Object edges
    if (snapToObjects_) {
      for (auto &s : scene_) {
        if (s.id == excludeId)
          continue;
        auto bb = vtess::shapeAABB(s);
        if (!bb.valid())
          continue;
        tryX(bb.x0);
        tryX(bb.x1);
        tryX(bb.center().x);
      }
    }

    snapFiredX_ = (best != cx);
    snapLineX_ = best;
    return best;
  }

  float snapY(float cy, VShapeId excludeId = kNoShape) {
    if (!snapEnabled_)
      return cy;
    float thresh = 6.f / currentZoom_;
    float best = cy, bestD = thresh;

    auto tryY = [&](float candidate) {
      float d = std::abs(candidate - cy);
      if (d < bestD) {
        bestD = d;
        best = candidate;
      }
    };

    if (snapToGrid_) {
      float g = std::round(cy / gridSize_) * gridSize_;
      tryY(g);
    }
    if (snapToGuides_) {
      for (auto &guide : guides_)
        if (guide.axis == Guide::Axis::H)
          tryY(guide.position);
    }
    if (snapToObjects_) {
      for (auto &s : scene_) {
        if (s.id == excludeId)
          continue;
        auto bb = vtess::shapeAABB(s);
        if (!bb.valid())
          continue;
        tryY(bb.y0);
        tryY(bb.y1);
        tryY(bb.center().y);
      }
    }

    snapFiredY_ = (best != cy);
    snapLineY_ = best;
    return best;
  }

  // Snap a full AABB translation — snaps all 5 candidate points
  // (left, right, top, bottom, center) and picks the closest snap per axis
  vmath::Vec2 snapTranslation(const vmath::AABB &bb, float dx, float dy,
                              VShapeId excludeId = kNoShape) {
    if (!snapEnabled_)
      return {dx, dy};

    float newX0 = bb.x0 + dx, newX1 = bb.x1 + dx, newCX = bb.center().x + dx;
    float newY0 = bb.y0 + dy, newY1 = bb.y1 + dy, newCY = bb.center().y + dy;

    float thresh = 6.f / currentZoom_;

    // Try snapping each edge/center, pick whichever fires closest
    struct Cand {
      float raw, snapped;
    };
    auto bestCandX = [&]() -> float {
      float bestD = thresh + 1.f, bestShift = 0.f;
      for (float raw : {newX0, newX1, newCX}) {
        float sn = snapX(raw, excludeId);
        float d = std::abs(sn - raw);
        if (snapFiredX_ && d < bestD) {
          bestD = d;
          bestShift = sn - raw;
        }
      }
      return dx + bestShift;
    };
    auto bestCandY = [&]() -> float {
      float bestD = thresh + 1.f, bestShift = 0.f;
      for (float raw : {newY0, newY1, newCY}) {
        float sn = snapY(raw, excludeId);
        float d = std::abs(sn - raw);
        if (snapFiredY_ && d < bestD) {
          bestD = d;
          bestShift = sn - raw;
        }
      }
      return dy + bestShift;
    };

    return {bestCandX(), bestCandY()};
  }

  vmath::Vec2 clampTranslation(VShapeId id, float dx, float dy) const {
    auto *s = const_cast<VectorSurface *>(this)->beginEdit(id);
    if (!s)
      return {dx, dy};
    auto bb = vtess::shapeAABB(*s);
    if (!bb.valid())
      return {dx, dy};
    float fw = float(w_), fh = float(h_);
    if (bb.x0 + dx < 0.f)
      dx = -bb.x0;
    if (bb.y0 + dy < 0.f)
      dy = -bb.y0;
    if (bb.x1 + dx > fw)
      dx = fw - bb.x1;
    if (bb.y1 + dy > fh)
      dy = fh - bb.y1;
    return {dx, dy};
  }

  // =========================================================================
  // ── Hit-test shapes ───────────────────────────────────────────────────────
  VShapeId hitTest(float x, float y) const {
    vmath::Vec2 pt{x, y};
    for (int i = (int)scene_.size() - 1; i >= 0; i--) {
      auto &s = scene_[i];
      auto bb = vtess::shapeAABB(s);
      float sw = s.stroke.none ? 0.f
                               : (s.stroke.width / currentZoom_) * .5f +
                                     2.f / currentZoom_;
      bb.x0 -= sw;
      bb.y0 -= sw;
      bb.x1 += sw;
      bb.y1 += sw;
      if (bb.contains(pt))
        return s.id;
    }
    return kNoShape;
  }
  std::vector<VShapeId> hitTestBox(float x0, float y0, float x1,
                                   float y1) const {
    vmath::AABB sel;
    sel.x0 = min(x0, x1);
    sel.y0 = min(y0, y1);
    sel.x1 = max(x0, x1);
    sel.y1 = max(y0, y1);
    std::vector<VShapeId> result;
    for (auto &s : scene_)
      if (vtess::shapeAABB(s).intersects(sel))
        result.push_back(s.id);
    return result;
  }

  // =========================================================================
  // ── Tool: Select ──────────────────────────────────────────────────────────
  void beginHandleDrag(float x, float y) {
    dragStartBB_ = selectionBB();
    if (!originSet_)
      transformOrigin_ = dragStartBB_.center();
    handleDragOrigins_.clear();
    for (auto id : selection_) {
      auto *s = beginEdit(id);
      if (s)
        handleDragOrigins_.push_back({id, s->xform});
    }
    if (dragHandleKind_ == HandleKind::Rotate)
      rotateStartAngle_ =
          std::atan2(y - transformOrigin_.y, x - transformOrigin_.x);
  }

  void applyScaleHandle(float x, float y, bool shiftConstrain) {
    if (!dragStartBB_.valid())
      return;
    int hi = dragHandleIndex_;
    static const int kOpposite[8] = {7, 6, 5, 4, 3, 2, 1, 0};
    vmath::Vec2 pts[8];
    float pad = 4.f / currentZoom_;
    getHandlePositions(dragStartBB_, pad, pts);
    vmath::Vec2 anchor = pts[kOpposite[hi]];
    vmath::Vec2 origHandle = pts[hi];
    float newDX = x - anchor.x, newDY = y - anchor.y;
    float oldDX = origHandle.x - anchor.x, oldDY = origHandle.y - anchor.y;
    auto &c = kScaleConstraints[hi];
    float sx = c.lockX ? 1.f : (std::abs(oldDX) > 1e-4f ? newDX / oldDX : 1.f);
    float sy = c.lockY ? 1.f : (std::abs(oldDY) > 1e-4f ? newDY / oldDY : 1.f);
    if (shiftConstrain && !c.lockX && !c.lockY) {
      float s = std::abs(sx) > std::abs(sy) ? sx : sy;
      sx = sy = s;
    }
    vmath::Mat3 T = vmath::Mat3::translate(anchor.x, anchor.y);
    vmath::Mat3 Ti = vmath::Mat3::translate(-anchor.x, -anchor.y);
    vmath::Mat3 S = vmath::Mat3::scale(sx, sy);
    vmath::Mat3 xf = T * S * Ti;
    for (auto &[id, orig] : handleDragOrigins_) {
      auto *s = beginEdit(id);
      if (!s)
        continue;
      s->xform = xf * orig;
    }
    endEdit();
  }

  void applyRotateHandle(float x, float y, bool shiftConstrain) {
    float angle = std::atan2(y - transformOrigin_.y, x - transformOrigin_.x);
    float dAngle = angle - rotateStartAngle_;
    if (shiftConstrain) {
      const float snap = 3.14159265f / 4.f;
      dAngle = std::round(dAngle / snap) * snap;
    }
    vmath::Mat3 T =
        vmath::Mat3::translate(transformOrigin_.x, transformOrigin_.y);
    vmath::Mat3 Ti =
        vmath::Mat3::translate(-transformOrigin_.x, -transformOrigin_.y);
    vmath::Mat3 R = vmath::Mat3::rotate(dAngle);
    vmath::Mat3 xf = T * R * Ti;
    for (auto &[id, orig] : handleDragOrigins_) {
      auto *s = beginEdit(id);
      if (!s)
        continue;
      s->xform = xf * orig;
    }
    endEdit();
  }

  void onSelectDown(float x, float y) {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    float pad = 4.f / currentZoom_;
    if (!selection_.empty() && hitTestOrigin(x, y)) {
      draggingOrigin_ = true;
      draggingHandle_ = false;
      draggingShape_ = false;
      draggingBox_ = false;
      return;
    }
    if (!selection_.empty()) {
      int hi = hitTestHandles(x, y, pad);
      if (hi >= 0) {
        if (transformMode_ == TransformMode::Move) {
          // Move mode: ignore handles, fall through to shape drag below
        } else {
          draggingHandle_ = true;
          draggingShape_ = false;
          draggingBox_ = false;
          draggingOrigin_ = false;
          dragHandleIndex_ = hi;
          dragHandleKind_ = (transformMode_ == TransformMode::Scale)
                                ? HandleKind::Scale
                                : HandleKind::Rotate;
          beginHandleDrag(x, y);
          return;
        }
      }
    }
    VShapeId hit = hitTest(x, y);
    if (hit != kNoShape) {
      bool alreadySelected = std::find(selection_.begin(), selection_.end(),
                                       hit) != selection_.end();
      if (!shift) {
        if (!alreadySelected)
          selection_.clear();
      }
      select(hit, shift);
      if (!alreadySelected || shift) {
        transformMode_ = TransformMode::Scale;
        originSet_ = false;
        transformOrigin_ = selectionBB().center();
      }
      dragOrigins_.clear();
      for (auto id : selection_) {
        auto *s = beginEdit(id);
        if (s)
          dragOrigins_.push_back({id, s->xform});
      }
      draggingShape_ = true;
      draggingBox_ = false;
      draggingHandle_ = false;
      draggingOrigin_ = false;
    } else {
      if (!shift) {
        selection_.clear();
        transformMode_ = TransformMode::Scale;
        originSet_ = false;
      }
      draggingBox_ = true;
      draggingShape_ = false;
      draggingHandle_ = false;
      draggingOrigin_ = false;
      boxX0_ = boxX1_ = x;
      boxY0_ = boxY1_ = y;
    }
  }

  void onSelectMove(float x, float y, float, float) {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (draggingOrigin_) {
      transformOrigin_ = {x, y};
      originSet_ = true;
      dirty_ = true;
      return;
    }
    if (draggingHandle_) {
      if (dragHandleKind_ == HandleKind::Scale)
        applyScaleHandle(x, y, shift);
      else
        applyRotateHandle(x, y, shift);
      return;
    }
    if (draggingShape_) {
      snapFiredX_ = snapFiredY_ = false;
      float totalDX = x - mdownX_, totalDY = y - mdownY_;

      // Build combined AABB of all dragged shapes at their drag-origin position
      vmath::AABB combinedBB;
      for (auto &[id, orig] : dragOrigins_) {
        auto *s = beginEdit(id);
        if (!s)
          continue;
        vmath::Mat3 savedXform = s->xform;
        s->xform.m[6] = orig.m[6];
        s->xform.m[7] = orig.m[7];
        auto bb = vtess::shapeAABB(*s);
        s->xform = savedXform;
        if (bb.valid()) {
          combinedBB.expand({bb.x0, bb.y0});
          combinedBB.expand({bb.x1, bb.y1});
        }
      }

      // Snap the combined translation
      VShapeId firstId =
          dragOrigins_.empty() ? kNoShape : dragOrigins_[0].first;
      auto snapped =
          snapTranslation({combinedBB.x0 + totalDX, combinedBB.y0 + totalDY,
                           combinedBB.x1 + totalDX, combinedBB.y1 + totalDY},
                          totalDX, totalDY, firstId);
      float sdx = snapped.x, sdy = snapped.y;

      for (auto &[id, orig] : dragOrigins_) {
        auto *s = beginEdit(id);
        if (!s)
          continue;
        s->xform.m[6] = orig.m[6] + sdx;
        s->xform.m[7] = orig.m[7] + sdy;
        auto bb = vtess::shapeAABB(*s);
        float fw = float(w_), fh = float(h_);
        float cx = 0.f, cy = 0.f;
        if (bb.valid()) {
          if (bb.x0 < 0.f)
            cx = -bb.x0;
          if (bb.y0 < 0.f)
            cy = -bb.y0;
          if (bb.x1 > fw)
            cx = fw - bb.x1;
          if (bb.y1 > fh)
            cy = fh - bb.y1;
        }
        s->xform.m[6] += cx;
        s->xform.m[7] += cy;
      }
      endEdit();
    }
    if (draggingBox_) {
      boxX1_ = x;
      boxY1_ = y;
    }
  }

  void onSelectUp(float x, float y) {
    if (draggingOrigin_) {
      draggingOrigin_ = false;
      return;
    }
    if (draggingHandle_) {
      bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      if (dragHandleKind_ == HandleKind::Scale)
        applyScaleHandle(x, y, shift);
      else
        applyRotateHandle(x, y, shift);
      pushUndo();
      draggingHandle_ = false;
      dragHandleIndex_ = -1;
      if (!originSet_)
        transformOrigin_ = selectionBB().center();
      return;
    }
    if (draggingShape_ &&
        (std::abs(x - mdownX_) > 1.f || std::abs(y - mdownY_) > 1.f)) {
      pushUndo();
      float totalDX = x - mdownX_, totalDY = y - mdownY_;
      for (auto &[id, orig] : dragOrigins_) {
        auto *s = beginEdit(id);
        if (!s)
          continue;
        s->xform.m[6] = orig.m[6] + totalDX;
        s->xform.m[7] = orig.m[7] + totalDY;
      }
    }
    if (draggingBox_) {
      bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      auto hits = hitTestBox(boxX0_, boxY0_, boxX1_, boxY1_);
      if (!shift)
        selection_.clear();
      for (auto id : hits)
        select(id, true);
      draggingBox_ = false;
      if (!selection_.empty()) {
        transformMode_ = TransformMode::Scale;
        originSet_ = false;
        transformOrigin_ = selectionBB().center();
      }
    }
    draggingShape_ = false;
    dragOrigins_.clear();
  }

  // =========================================================================
  // ── Tool: Pen ─────────────────────────────────────────────────────────────
  void onPenDown(float x, float y) {
    if (penInProgress_ && !penAnchors_.empty()) {
      float dx = x - penAnchors_.front().x, dy = y - penAnchors_.front().y;
      if (std::sqrt(dx * dx + dy * dy) < 8.f) {
        commitPen(true);
        return;
      }
    }
    if (!penInProgress_) {
      penInProgress_ = true;
      penAnchors_.clear();
      penCPs_.clear();
    }
    auto cp = clampToArtboard(x, y);
    penAnchors_.push_back(cp);
    penCPs_.push_back(cp);
    penCursor_ = cp;
  }
  void onPenMove(float x, float y) {
    penCursor_ = {x, y};
    if ((GetKeyState(VK_SHIFT) & 0x8000) && !penAnchors_.empty())
      penCPs_.back() = {x, y};
  }
  void commitPen(bool closed = false) {
    if (penAnchors_.size() < 2) {
      penInProgress_ = false;
      penAnchors_.clear();
      penCPs_.clear();
      return;
    }
    VShape s;
    s.fill = activeFill_;
    s.stroke = activeStroke_;
    s.kind = VShapeKind::Path;
    s.id = nextId_++;
    VPath p;
    p.cmds.push_back({PathCmdKind::MoveTo, {penAnchors_[0]}});
    for (int i = 1; i < (int)penAnchors_.size(); i++) {
      vmath::Vec2 cp1 = penCPs_[i - 1], cp2 = penAnchors_[i];
      bool hasCurve = (vmath::len(cp1 - penAnchors_[i - 1]) > 1e-3f);
      if (hasCurve) {
        PathCmd cmd;
        cmd.kind = PathCmdKind::CubicTo;
        cmd.pt[0] = cp1;
        cmd.pt[1] = cp2;
        cmd.pt[2] = penAnchors_[i];
        p.cmds.push_back(cmd);
      } else {
        p.cmds.push_back({PathCmdKind::LineTo, {penAnchors_[i]}});
      }
    }
    if (closed)
      p.cmds.push_back({PathCmdKind::Close});
    s.path = std::move(p);
    pushUndo();
    scene_.push_back(std::move(s));
    selection_ = {scene_.back().id};
    penInProgress_ = false;
    penAnchors_.clear();
    penCPs_.clear();
    dirty_ = true;
  }
  void cancelInProgress() {
    penInProgress_ = false;
    penAnchors_.clear();
    penCPs_.clear();
    shapeDragging_ = false;
    ghostId_ = kNoShape;
    draggingNode_ = false;
    draggingHandle_ = false;
    // Do NOT cancel text session here — use cancelTextSession() explicitly
  }

  // =========================================================================
  // ── Tool: Text ────────────────────────────────────────────────────────────

  void onTextDown(float x, float y) {
    // If a session is already active, clicking elsewhere commits it
    // and starts a new one at the new location
    if (textSession_.active)
      commitTextSession();

    // Start a new text session at the clicked point
    textSession_.active = true;
    textSession_.x = x;
    textSession_.y = y;
    textSession_.text.clear();
    textSession_.style = activeTextStyle_;
    blinkVisible_ = true;
    blinkAccum_ = 0.0;
    dirty_ = true;
  }

  void handleTextKey(int vk) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (vk == VK_RETURN) {
      // Enter commits the text as a VShape
      commitTextSession();
      dirty_ = true;
      return;
    }
    if (vk == VK_ESCAPE) {
      cancelTextSession();
      dirty_ = true;
      return;
    }
    if (vk == VK_BACK) {
      if (!textSession_.text.empty())
        textSession_.text.pop_back();
      dirty_ = true;
      return;
    }
    if (ctrl && vk == 'A') {
      textSession_.text.clear();
      dirty_ = true;
      return;
    }
    // Convert virtual key to Unicode character
    BYTE ks[256] = {};
    GetKeyboardState(ks);
    wchar_t buf[4] = {};
    int n = ToUnicode(vk, MapVirtualKey(vk, MAPVK_VK_TO_VSC), ks, buf, 4, 0);
    if (n == 1 && buf[0] >= 0x20) {
      textSession_.text += buf[0];
      dirty_ = true;
    }
  }

  void commitTextSession() {
    if (!textSession_.active)
      return;
    if (!textSession_.text.empty()) {
      VShape s;
      s.id = nextId_++;
      s.kind = VShapeKind::Text;
      s.fill = activeFill_; // carry fill color (ignored for text render,
      s.stroke.none = true; //   but stored for completeness)
      s.text.text = textSession_.text;
      s.text.x = textSession_.x;
      s.text.y = textSession_.y;
      s.text.style = textSession_.style;
      // Sync the fill color into the text style so it renders in the right
      // color
      s.text.style.r = activeFill_.color.r;
      s.text.style.g = activeFill_.color.g;
      s.text.style.b = activeFill_.color.b;
      pushUndo();
      scene_.push_back(std::move(s));
      selection_ = {scene_.back().id};
    }
    textSession_.active = false;
    textSession_.text.clear();
    // Invalidate any cached texture for the in-progress text
    textTexCache_.clear();
    dirty_ = true;
  }

  void cancelTextSession() {
    textSession_.active = false;
    textSession_.text.clear();
    dirty_ = true;
  }

  // ── Live text preview renderer ────────────────────────────────────────────
  // Renders the current typing session directly via GDI+ to an offscreen
  // bitmap, uploads to a GL texture, and blits it.  A blinking cursor bar
  // is appended when blinkVisible_ is true.
  void renderTextSession(const float mvp[16]) {
    if (!textSession_.active)
      return;

    const TextStyle &ts = textSession_.style;
    std::wstring display = textSession_.text;
    if (blinkVisible_)
      display += L'|'; // blinking I-beam cursor

    // Build a temporary VShape so we can reuse getTextTex infrastructure
    VShape tmp;
    tmp.id = 0;
    tmp.kind = VShapeKind::Text;
    tmp.text.text = display;
    tmp.text.x = textSession_.x;
    tmp.text.y = textSession_.y;
    tmp.text.style = ts;
    // Color from active fill
    tmp.text.style.r = activeFill_.color.r;
    tmp.text.style.g = activeFill_.color.g;
    tmp.text.style.b = activeFill_.color.b;

    // Render and blit
    auto &tt = getTextTex(tmp);
    if (tt.tex)
      blitTextTex(tt, tmp, mvp);

    // Draw a subtle anchor indicator at the text origin
    float cr = 3.f / currentZoom_;
    drawCircle(textSession_.x, textSession_.y, cr, {0.3f, 0.7f, 1.f, 0.8f},
               mvp);
  }

  // =========================================================================
  // ── Tool: Node ────────────────────────────────────────────────────────────
  void onNodeDown(float x, float y) {
    dragNodeIdx_ = -1;
    draggingNode_ = false;
    if (selection_.empty())
      return;
    VShapeId id = selection_.front();
    auto *s = beginEdit(id);
    if (!s)
      return;
    auto checkPt = [&](vmath::Vec2 p, int cmdIdx, bool isCP,
                       int cpSide) -> bool {
      vmath::Vec2 wp = s->xform.apply(p);
      float dx = wp.x - x, dy = wp.y - y;
      if (dx * dx + dy * dy < 64.f) {
        dragNodeIdx_ = cmdIdx;
        dragNodeIsCP_ = isCP;
        dragNodeCPSide_ = cpSide;
        draggingNode_ = true;
        return true;
      }
      return false;
    };
    if (s->kind == VShapeKind::Poly) {
      for (int i = 0; i < (int)s->poly.pts.size(); i++)
        if (checkPt(s->poly.pts[i], i, false, 0))
          return;
    } else if (s->kind == VShapeKind::Path) {
      for (int ci = 0; ci < (int)s->path.cmds.size(); ci++) {
        auto &cmd = s->path.cmds[ci];
        if (cmd.kind == PathCmdKind::MoveTo ||
            cmd.kind == PathCmdKind::LineTo) {
          if (checkPt(cmd.pt[0], ci, false, 0))
            return;
        } else if (cmd.kind == PathCmdKind::CubicTo) {
          if (checkPt(cmd.pt[0], ci, true, 0))
            return;
          if (checkPt(cmd.pt[1], ci, true, 1))
            return;
          if (checkPt(cmd.pt[2], ci, false, 0))
            return;
        }
      }
    }
    VShapeId hit = hitTest(x, y);
    if (hit != kNoShape)
      select(hit);
  }

  void onNodeMove(float x, float y, float, float) {
    if (!draggingNode_ || dragNodeIdx_ < 0)
      return;
    if (selection_.empty())
      return;
    VShapeId id = selection_.front();
    auto *s = beginEdit(id);
    if (!s)
      return;
    vmath::Vec2 local = clampToArtboard(x, y);
    if (s->kind == VShapeKind::Poly) {
      if (dragNodeIdx_ < (int)s->poly.pts.size())
        s->poly.pts[dragNodeIdx_] = local;
    } else if (s->kind == VShapeKind::Path) {
      if (dragNodeIdx_ < (int)s->path.cmds.size()) {
        auto &cmd = s->path.cmds[dragNodeIdx_];
        if (cmd.kind == PathCmdKind::MoveTo ||
            cmd.kind == PathCmdKind::LineTo) {
          if (!dragNodeIsCP_)
            cmd.pt[0] = local;
        } else if (cmd.kind == PathCmdKind::CubicTo) {
          if (dragNodeIsCP_)
            cmd.pt[dragNodeCPSide_] = local;
          else
            cmd.pt[2] = local;
        }
      }
    }
    endEdit();
  }
  void onNodeUp() {
    if (draggingNode_)
      pushUndo();
    draggingNode_ = false;
    dragNodeIdx_ = -1;
  }

  // =========================================================================
  // ── Tool: Rect / Ellipse / Line ───────────────────────────────────────────
  void onShapeDown(float x, float y) {
    shapeDragging_ = true;
    auto c = clampToArtboard(x, y);
    shapeX0_ = c.x;
    shapeY0_ = c.y;
  }
  void onShapeMove(float, float) {}
  void onShapeUp(float x, float y) {
    if (!shapeDragging_)
      return;
    shapeDragging_ = false;
    auto cr = clampToArtboard(x, y);
    x = cr.x;
    y = cr.y;
    float x0 = min(shapeX0_, x), y0 = min(shapeY0_, y);
    float x1 = max(shapeX0_, x), y1 = max(shapeY0_, y);
    float fw = float(w_), fh = float(h_);
    x0 = std::clamp(x0, 0.f, fw);
    y0 = std::clamp(y0, 0.f, fh);
    x1 = std::clamp(x1, 0.f, fw);
    y1 = std::clamp(y1, 0.f, fh);
    if (x1 - x0 < 2.f && y1 - y0 < 2.f)
      return;
    x0 = snapX(x0);
    y0 = snapY(y0);
    x1 = snapX(x1);
    y1 = snapY(y1);
    VShape s;
    s.fill = activeFill_;
    s.stroke = activeStroke_;
    s.id = nextId_++;
    switch (tool_) {
    case VTool::Rect:
      s.kind = VShapeKind::Rect;
      s.rect = {x0, y0, x1 - x0, y1 - y0, 0, 0};
      break;
    case VTool::Ellipse:
      s.kind = VShapeKind::Ellipse;
      s.ellipse = {(x0 + x1) * .5f, (y0 + y1) * .5f, (x1 - x0) * .5f,
                   (y1 - y0) * .5f};
      break;
    case VTool::Line:
      s.kind = VShapeKind::Poly;
      s.poly.pts = {{shapeX0_, shapeY0_}, {x, y}};
      s.poly.closed = false;
      s.fill.none = true;
      break;
    default:
      return;
    }
    pushUndo();
    scene_.push_back(std::move(s));
    selection_ = {scene_.back().id};
    transformMode_ = TransformMode::Scale;
    originSet_ = false;
    transformOrigin_ = selectionBB().center();
    dirty_ = true;
  }

  // =========================================================================
  // ── GL resource helpers ───────────────────────────────────────────────────
  void buildShaders() {
    const char *vert = R"GLSL(
#version 460 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
out vec2 vWorld;
void main(){
  vUV = aUV;
  vWorld = aPos;
  gl_Position = uMVP * vec4(aPos, 0, 1);
}
)GLSL";

    const char *frag = R"GLSL(
#version 460 core
in vec2 vUV;
in vec2 vWorld;
uniform sampler2D uTex;
uniform vec4      uColor;
uniform float     uAlpha;
uniform int       uMode;

// Gradient uniforms
uniform vec2  uGStart;       // gradient start in world space
uniform vec2  uGEnd;         // gradient end in world space
uniform vec4  uGColors[4];   // stop colors
uniform float uGPos[4];      // stop positions 0→1
uniform int   uGStopCount;   // 2–4

out vec4 fragColor;

vec4 evalGradient(float t) {
  t = clamp(t, 0.0, 1.0);
  vec4 c = uGColors[0];
  for (int i = 1; i < uGStopCount; i++) {
    if (t >= uGPos[i-1] && t <= uGPos[i]) {
      float f = (t - uGPos[i-1]) / max(uGPos[i] - uGPos[i-1], 0.0001);
      c = mix(uGColors[i-1], uGColors[i], f);
    }
  }
  if (t > uGPos[uGStopCount-1]) c = uGColors[uGStopCount-1];
  return c;
}

void main(){
  if (uMode == 0) {
    vec4 c = texture(uTex, vUV);
    fragColor = vec4(c.rgb, c.a * uAlpha);
  } else if (uMode == 1) {
    fragColor = uColor;
  } else {
    // Linear gradient — project world pos onto gradient axis
    vec2 axis = uGEnd - uGStart;
    float len2 = dot(axis, axis);
    float t = len2 > 0.0001 ? dot(vWorld - uGStart, axis) / len2 : 0.0;
    fragColor = evalGradient(t);
  }
}
)GLSL";

    prog_ = glutil::linkProgram(vert, frag);
    assert(prog_);
    u_.mvp = glGetUniformLocation(prog_, "uMVP");
    u_.mode = glGetUniformLocation(prog_, "uMode");
    u_.color = glGetUniformLocation(prog_, "uColor");
    u_.tex = glGetUniformLocation(prog_, "uTex");
    u_.alpha = glGetUniformLocation(prog_, "uAlpha");
    u_.gStart = glGetUniformLocation(prog_, "uGStart");
    u_.gEnd = glGetUniformLocation(prog_, "uGEnd");
    u_.gColors = glGetUniformLocation(prog_, "uGColors");
    u_.gPositions = glGetUniformLocation(prog_, "uGPos");
    u_.gStopCount = glGetUniformLocation(prog_, "uGStopCount");
  }

  void buildBuffers() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 4096, nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glDisableVertexAttribArray(1);
    glBindVertexArray(0);
  }

  void drawTris(const std::vector<float> &verts, const RGBA &col,
                const float mvp[16]) {
    if (verts.empty())
      return;
    glUseProgram(prog_);
    glUniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    glUniform1i(u_.mode, 1);
    glUniform4f(u_.color, col.r, col.g, col.b, col.a);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glDisableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, (int)(verts.size() / 2));
    glBindVertexArray(0);
  }

  void drawTrisGradient(const std::vector<float> &verts, const VFill &fill,
                        const vmath::AABB &bb, const float mvp[16]) {
    if (verts.empty())
      return;

    // Map object-space gradient coords (0→1) to world space using the AABB
    float wx0 = bb.x0 + fill.gx0 * bb.width();
    float wy0 = bb.y0 + fill.gy0 * bb.height();
    float wx1 = bb.x0 + fill.gx1 * bb.width();
    float wy1 = bb.y0 + fill.gy1 * bb.height();

    // Upload stop data — pad unused slots to avoid reading garbage
    float colArr[16] = {}; // 4 stops × 4 floats
    float posArr[4] = {};
    int cnt = min((int)fill.stops.size(), 4);
    for (int i = 0; i < cnt; i++) {
      colArr[i * 4 + 0] = fill.stops[i].color.r;
      colArr[i * 4 + 1] = fill.stops[i].color.g;
      colArr[i * 4 + 2] = fill.stops[i].color.b;
      colArr[i * 4 + 3] = fill.stops[i].color.a;
      posArr[i] = fill.stops[i].position;
    }

    glUseProgram(prog_);
    glUniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    glUniform1i(u_.mode, 2);
    glUniform2f(u_.gStart, wx0, wy0);
    glUniform2f(u_.gEnd, wx1, wy1);
    glUniform4fv(u_.gColors, 4, colArr);
    glUniform1fv(u_.gPositions, 4, posArr);
    glUniform1i(u_.gStopCount, cnt);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glDisableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, (int)(verts.size() / 2));
    glBindVertexArray(0);
  }

  void drawLineLoop(const std::vector<vmath::Vec2> &pts, const RGBA &col,
                    float w, const float mvp[16]) {
    if (pts.size() < 2)
      return;
    bool closed =
        (pts.size() >= 3 && vmath::len(pts.back() - pts.front()) < 1e-4f);
    std::vector<vmath::Vec2> clean = pts;
    if (closed && clean.size() > 1)
      clean.pop_back();
    std::vector<float> tris;
    vtess::expandStroke(clean, closed, w * .5f, LineCapV::Round,
                        LineJoinV::Round, tris);
    drawTris(tris, col, mvp);
  }

  void drawBorderRect(float x0, float y0, float x1, float y1, float lineWScreen,
                      const RGBA &col, const float mvp[16]) {
    float w = lineWScreen / currentZoom_;
    std::vector<float> tris;
    auto addQuad = [&](float ax, float ay, float bx, float by, float cx,
                       float cy, float dx, float dy) {
      tris.insert(tris.end(), {ax, ay, bx, by, cx, cy, cx, cy, dx, dy, ax, ay});
    };
    addQuad(x0, y1 - w, x1, y1 - w, x1, y1, x0, y1);
    addQuad(x0, y0, x1, y0, x1, y0 + w, x0, y0 + w);
    addQuad(x0, y0 + w, x0 + w, y0 + w, x0 + w, y1 - w, x0, y1 - w);
    addQuad(x1 - w, y0 + w, x1, y0 + w, x1, y1 - w, x1 - w, y1 - w);
    drawTris(tris, col, mvp);
  }

  void drawDiamond(float x, float y, float r, const RGBA &col,
                   const float mvp[16]) {
    std::vector<float> v = {x, y + r, x + r, y,     x,     y - r,
                            x, y + r, x,     y - r, x - r, y};
    drawTris(v, col, mvp);
  }
  void drawSquare(float x, float y, float r, const RGBA &col,
                  const float mvp[16]) {
    std::vector<float> v = {x - r, y - r, x + r, y - r, x + r, y + r,
                            x - r, y - r, x + r, y + r, x - r, y + r};
    drawTris(v, col, mvp);
  }
  void drawCircle(float x, float y, float r, const RGBA &col,
                  const float mvp[16]) {
    const int N = 12;
    std::vector<float> v;
    for (int i = 0; i < N; i++) {
      float a0 = float(i) / N * 6.2831853f, a1 = float(i + 1) / N * 6.2831853f;
      v.push_back(x);
      v.push_back(y);
      v.push_back(x + cosf(a0) * r);
      v.push_back(y + sinf(a0) * r);
      v.push_back(x + cosf(a1) * r);
      v.push_back(y + sinf(a1) * r);
    }
    drawTris(v, col, mvp);
  }
  void drawCircleRing(float x, float y, float r, float lw, const RGBA &col,
                      const float mvp[16]) {
    const int N = 32;
    std::vector<vmath::Vec2> pts;
    pts.reserve(N + 1);
    for (int i = 0; i <= N; i++) {
      float a = float(i) / N * 6.2831853f;
      pts.push_back({x + cosf(a) * r, y + sinf(a) * r});
    }
    std::vector<float> tris;
    vtess::expandStroke(pts, true, lw * .5f, LineCapV::Butt, LineJoinV::Round,
                        tris);
    drawTris(tris, col, mvp);
  }

  void drawRotateArrow(float cx, float cy, float r, float startA, float sweepA,
                       float lw, const RGBA &col, const float mvp[16]) {
    const int N = 20;
    std::vector<vmath::Vec2> pts;
    for (int i = 0; i <= N; i++) {
      float a = startA + sweepA * float(i) / N;
      pts.push_back({cx + cosf(a) * r, cy + sinf(a) * r});
    }
    std::vector<float> tris;
    vtess::expandStroke(pts, false, lw * .5f, LineCapV::Butt, LineJoinV::Round,
                        tris);
    drawTris(tris, col, mvp);
    float endA = startA + sweepA;
    vmath::Vec2 tip = {cx + cosf(endA) * r, cy + sinf(endA) * r};
    float tanA = endA + (sweepA > 0 ? 3.14159265f / 2 : -3.14159265f / 2);
    vmath::Vec2 dir = {cosf(tanA), sinf(tanA)};
    vmath::Vec2 perp_ = {-dir.y, dir.x};
    float hs = lw * 2.5f;
    vmath::Vec2 a = tip + dir * hs * (-1.f);
    vmath::Vec2 b = a + perp_ * hs;
    vmath::Vec2 cc = a - perp_ * hs;
    std::vector<float> arrow = {tip.x, tip.y, b.x, b.y, cc.x, cc.y};
    drawTris(arrow, col, mvp);
  }

  // =========================================================================
  // ── Text texture  ─────────────────────────────────────────────────────────

  uint64_t textHash(const VText &t) const {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](uint64_t v) {
      h ^= v;
      h *= 1099511628211ULL;
    };
    for (wchar_t c : t.text)
      mix(c);
    // NOTE: x/y NOT hashed — position lives in s.xform, not baked into texture
    mix(t.style.fontSize);
    mix(uint64_t(t.style.bold) | (uint64_t(t.style.italic) << 1) |
        (uint64_t(t.style.underline) << 2));
    for (wchar_t c : t.style.fontFace)
      mix(c);
    // Include color in hash so color changes invalidate the cache
    uint32_t rc = uint32_t(t.style.r * 255), gc = uint32_t(t.style.g * 255),
             bc = uint32_t(t.style.b * 255);
    mix((rc << 16) | (gc << 8) | bc);
    return h;
  }

  // Renders text at canvas origin (0,0) — position is baked into the shape's
  // xform, so the texture is purely about glyphs, style, and color.  This lets
  // the standard transform pipeline (translate / scale / rotate) work on text
  // just like any other shape.
  const TextTex &getTextTex(const VShape &s) {
    uint64_t key = textHash(s.text);
    auto it = textTexCache_.find(key);
    if (it != textTexCache_.end())
      return it->second;

    TextTex tt;

    // Size the off-screen bitmap to be just large enough for the text glyph.
    // We measure first, then render into a tight bitmap — this avoids wasting
    // a full-canvas-sized surface and makes the UV coordinates trivial (0→1).
    auto &ts = s.text.style;

    // ── Measure phase ────────────────────────────────────────────────────────
    HDC hdcRef = CreateCompatibleDC(nullptr);
    int weight = ts.bold ? FW_BOLD : FW_NORMAL;
    HFONT hFont = CreateFontW(
        -ts.fontSize, 0, 0, 0, weight, ts.italic ? TRUE : FALSE,
        ts.underline ? TRUE : FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        ts.fontFace.empty() ? L"Arial" : ts.fontFace.c_str());
    HGDIOBJ oldFontRef = SelectObject(hdcRef, hFont);
    SIZE tsz = {};
    GetTextExtentPoint32W(hdcRef, s.text.text.c_str(), (int)s.text.text.size(),
                          &tsz);
    SelectObject(hdcRef, oldFontRef);
    DeleteDC(hdcRef);

    // Pad a little so descenders / ClearType fringing aren't clipped
    const int kPad = 4;
    int bw = tsz.cx + kPad * 2;
    int bh = tsz.cy + kPad * 2;
    if (bw <= 0 || bh <= 0) {
      DeleteObject(hFont);
      textTexCache_[key] = tt;
      return textTexCache_[key];
    }

    // ── Render phase ─────────────────────────────────────────────────────────
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bw;
    bmi.bmiHeader.biHeight = -bh; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hBmp =
        CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp) {
      DeleteObject(hFont);
      DeleteDC(hdcMem);
      textTexCache_[key] = tt;
      return textTexCache_[key];
    }
    HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);

    // Clear to black (alpha = 0 later for black pixels)
    RECT rc0 = {0, 0, bw, bh};
    HBRUSH br = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdcMem, &rc0, br);
    DeleteObject(br);

    HGDIOBJ oldFont2 = SelectObject(hdcMem, hFont);
    COLORREF cr = RGB(int(ts.r * 255 + .5f), int(ts.g * 255 + .5f),
                      int(ts.b * 255 + .5f));
    SetTextColor(hdcMem, cr);
    SetBkMode(hdcMem, TRANSPARENT);

    // Render at (kPad, kPad) inside the tight bitmap
    TextOutW(hdcMem, kPad, kPad, s.text.text.c_str(), (int)s.text.text.size());
    GdiFlush();

    // ── Upload to GL ─────────────────────────────────────────────────────────
    const uint8_t *dib = reinterpret_cast<const uint8_t *>(bits);
    std::vector<uint8_t> rgba(size_t(bw) * bh * 4);
    for (int row = 0; row < bh; row++) {
      for (int col = 0; col < bw; col++) {
        const uint8_t *p = dib + (size_t(row) * bw + col) * 4;
        uint8_t *d = rgba.data() + (size_t(row) * bw + col) * 4;
        d[0] = p[2];
        d[1] = p[1];
        d[2] = p[0];
        d[3] = (p[0] || p[1] || p[2]) ? 255 : 0;
      }
    }
    glGenTextures(1, &tt.tex);
    glBindTexture(GL_TEXTURE_2D, tt.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bw, bh, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // Store pixel dimensions; ox/oy = offset from anchor in canvas-pixels
    // (pre-transform) The anchor point is the text baseline-left, which maps to
    // canvas (0,0) before xform.
    tt.w = bw;
    tt.h = bh;
    tt.ox = -(float)kPad;        // left edge relative to anchor
    tt.oy = -(float)(bh - kPad); // bottom edge relative to anchor (canvas Y up)
    tt.pxW = tsz.cx;             // actual glyph width (without padding)
    tt.pxH = tsz.cy;             // actual glyph height

    SelectObject(hdcMem, oldFont2);
    DeleteObject(hFont);
    SelectObject(hdcMem, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    textTexCache_[key] = tt;
    return textTexCache_[key];
  }

  // Blits a text texture applying the shape's full affine transform.
  // The quad is defined in "glyph space" (ox,oy) → (ox+w, oy+h), then the
  // shape's xform is composed into the MVP so translate/scale/rotate all work.
  void blitTextTex(const TextTex &tt, const VShape &s, const float mvp[16]) {
    if (!tt.tex || tt.w <= 0 || tt.h <= 0)
      return;

    // Quad corners in glyph-local space (Y-up canvas coords)
    float qx0 = tt.ox, qy0 = tt.oy;
    float qx1 = tt.ox + tt.w, qy1 = tt.oy + tt.h;

    // Apply the shape's xform to each corner, then feed into the mvp.
    // We build a 4×4 column-major matrix from the shape's 3×3 affine Mat3
    // and pre-multiply it with the incoming mvp so we make a single draw call.
    //
    //  localMat =  [ m0  m3  0  m6 ]     (column-major 4×4, rows are X Y Z W)
    //              [ m1  m4  0  m7 ]
    //              [ 0   0   1   0 ]
    //              [ 0   0   0   1 ]
    //
    const vmath::Mat3 &xf = s.xform;
    // combined = mvp * localMat  (both column-major)
    float local[16] = {
        xf.m[0], xf.m[1], 0.f, 0.f, // col 0
        xf.m[3], xf.m[4], 0.f, 0.f, // col 1
        0.f,     0.f,     1.f, 0.f, // col 2
        xf.m[6], xf.m[7], 0.f, 1.f  // col 3
    };
    // combined = mvp * local  (4×4 matrix multiply)
    float combined[16];
    for (int col = 0; col < 4; col++) {
      for (int row = 0; row < 4; row++) {
        float v = 0.f;
        for (int k = 0; k < 4; k++)
          v += mvp[k * 4 + row] * local[col * 4 + k];
        combined[col * 4 + row] = v;
      }
    }

    // Also factor in text.x / text.y anchor (stored in the VText, applied
    // before xform) The anchor translates the glyph-local coords so the text
    // origin sits at (text.x, text.y) *before* xform.  We fold this in as an
    // additional translation column in the local matrix rather than baking it
    // into the texture, keeping things clean. Actually, text.x/y IS the
    // pre-xform anchor — add it directly to the quad corners:
    float ax = s.text.x, ay = s.text.y;
    float q[] = {ax + qx0, ay + qy0, 0.f, 1.f, ax + qx1, ay + qy0, 1.f, 1.f,
                 ax + qx1, ay + qy1, 1.f, 0.f, ax + qx1, ay + qy1, 1.f, 0.f,
                 ax + qx0, ay + qy1, 0.f, 0.f, ax + qx0, ay + qy0, 0.f, 1.f};

    glUseProgram(prog_);
    glUniformMatrix4fv(u_.mvp, 1, GL_FALSE, combined);
    glUniform1i(u_.mode, 0);
    glUniform1i(u_.tex, 0);
    glUniform1f(u_.alpha, 1.f);
    glBindTexture(GL_TEXTURE_2D, tt.tex);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                          (void *)(sizeof(float) * 2));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  float snapCoord(float cc, float zoom, float offsetScreen,
                  float frac_hw) const {
    float screen = cc * zoom + offsetScreen;
    float snapped = std::floorf(screen - frac_hw + .5f) + frac_hw;
    return (snapped - offsetScreen) / zoom;
  }
  vtess::Contour snapContour(const vtess::Contour &c,
                             float strokeWidthScreen) const {
    float hw = strokeWidthScreen * .5f, frac_x = hw - std::floorf(hw);
    float oxs = currentOffsetX_ * currentZoom_,
          oys = currentOffsetY_ * currentZoom_;
    vtess::Contour out;
    out.reserve(c.size());
    for (auto &p : c)
      out.push_back({snapCoord(p.x, currentZoom_, oxs, frac_x),
                     snapCoord(p.y, currentZoom_, oys, frac_x)});
    return out;
  }

  // =========================================================================
  // ── renderShape ───────────────────────────────────────────────────────────
  void renderShape(const VShape &s, const float mvp[16]) {
    if (s.kind == VShapeKind::Text) {
      auto &tt = const_cast<VectorSurface *>(this)->getTextTex(s);
      blitTextTex(tt, s, mvp);
      return;
    }
    VShape adjusted = s;
    if (!adjusted.stroke.none) {
      adjusted.stroke.width = s.stroke.width / currentZoom_;
      if (adjusted.stroke.width * currentZoom_ < .5f)
        adjusted.stroke.width = .5f / currentZoom_;
    }
    auto ellipseSegs = [&](float rx, float ry) -> int {
      float a = rx * currentZoom_, b = ry * currentZoom_;
      float hh = (a - b) * (a - b) / ((a + b) * (a + b) + 1e-9f);
      float perim = 3.14159265f * (a + b) *
                    (1.f + 3.f * hh / (10.f + sqrtf(4.f - 3.f * hh)));
      return (int)std::clamp(perim / 1.5f, 32.f, 2048.f);
    };
    if (!adjusted.fill.none) {
      vtess::Contours fillContours;
      switch (s.kind) {
      case VShapeKind::Rect:
        fillContours.push_back(vtess::rectContour(
            s.rect.x, s.rect.y, s.rect.w, s.rect.h, s.rect.rx, s.rect.ry));
        break;
      case VShapeKind::Ellipse:
        fillContours.push_back(vtess::ellipseContour(
            s.ellipse.cx, s.ellipse.cy, s.ellipse.rx, s.ellipse.ry,
            ellipseSegs(s.ellipse.rx, s.ellipse.ry)));
        break;
      case VShapeKind::Poly:
        fillContours.push_back(s.poly.pts);
        break;
      case VShapeKind::Path:
        vtess::flattenPath(s.path, fillContours);
        break;
      default:
        break;
      }
      std::vector<float> fillTris;
      if (s.kind == VShapeKind::Path) {
        // Use Clipper2 to clean self-intersections before triangulating
        clipperCleanFill(fillContours,
                         s.fill.rule == VFillRule::EvenOdd ? VFillRule::EvenOdd
                                                           : VFillRule::NonZero,
                         fillTris);
      } else {
        for (auto &c : fillContours)
          vtess::earClip(c, fillTris);
      }
      std::vector<float> fillWorld;
      fillWorld.reserve(fillTris.size());
      for (int i = 0; i + 1 < (int)fillTris.size(); i += 2) {
        auto p = s.xform.apply({fillTris[i], fillTris[i + 1]});
        fillWorld.push_back(p.x);
        fillWorld.push_back(p.y);
      }
      if (!fillWorld.empty()) {
        if (s.fill.mode == VFillMode::LinearGradient &&
            s.fill.stops.size() >= 2) {
          auto bb = vtess::shapeAABB(s);
          drawTrisGradient(fillWorld, s.fill, bb, mvp);
        } else {
          drawTris(fillWorld, adjusted.fill.color, mvp);
        }
      }
    }
    if (!adjusted.stroke.none) {
      float hw = adjusted.stroke.width * .5f;
      bool snapApplicable = (s.kind == VShapeKind::Rect ||
                             (s.kind == VShapeKind::Poly && s.poly.closed));
      vtess::Contours strokeContours;
      switch (s.kind) {
      case VShapeKind::Rect:
        strokeContours.push_back(vtess::rectContour(
            s.rect.x, s.rect.y, s.rect.w, s.rect.h, s.rect.rx, s.rect.ry));
        break;
      case VShapeKind::Ellipse:
        strokeContours.push_back(vtess::ellipseContour(
            s.ellipse.cx, s.ellipse.cy, s.ellipse.rx, s.ellipse.ry,
            ellipseSegs(s.ellipse.rx, s.ellipse.ry)));
        break;
      case VShapeKind::Poly:
        strokeContours.push_back(s.poly.pts);
        break;
      case VShapeKind::Path:
        vtess::flattenPath(s.path, strokeContours);
        break;
      default:
        break;
      }
      static constexpr float kMaxSegPx = 10.f;
      float maxSegCanvas = kMaxSegPx / currentZoom_;
      auto subdivideContour = [&](const vtess::Contour &c,
                                  bool closed) -> vtess::Contour {
        vtess::Contour out;
        int n = (int)c.size();
        if (n == 0)
          return out;
        int numSegs = closed ? n : n - 1;
        for (int i = 0; i < numSegs; i++) {
          vmath::Vec2 a = c[i], b = c[(i + 1) % n];
          out.push_back(a);
          float segLen = vmath::len(b - a);
          int steps = (int)std::ceil(segLen / maxSegCanvas);
          if (steps > 1)
            for (int k = 1; k < steps; k++)
              out.push_back(vmath::lerp(a, b, float(k) / float(steps)));
        }
        if (!closed)
          out.push_back(c.back());
        return out;
      };
      std::vector<float> strokeTris;
      for (auto &c : strokeContours) {
        bool isClosed =
            (s.kind == VShapeKind::Rect || s.kind == VShapeKind::Ellipse ||
             (s.kind == VShapeKind::Poly && s.poly.closed));
        LineJoinV ej = adjusted.stroke.join;
        if (s.kind == VShapeKind::Rect)
          ej = LineJoinV::Miter;

        vtess::Contour subd = subdivideContour(c, isClosed);
        vtess::Contour snapped =
            snapApplicable ? snapContour(subd, s.stroke.width) : subd;

        // ── Dash pattern ────────────────────────────────────────────────
        // Scale pattern from screen-pixels to canvas units
        std::vector<float> scaledPattern;
        for (float v : s.stroke.dash)
          scaledPattern.push_back(v / currentZoom_);

        // For closed shapes, explicitly append the first point so the dash
        // walker covers the closing edge (BL→TL on a rect, etc.).
        // We then pass closed=false because the polyline is now self-closing.
        vtess::Contour dashInput = snapped;
        if (isClosed && !dashInput.empty() &&
            vmath::len(dashInput.back() - dashInput.front()) > 1e-4f) {
          dashInput.push_back(dashInput.front());
        }

        auto dashSegs = vtess::applyDash(dashInput, false, scaledPattern,
                                         s.stroke.dashOffset / currentZoom_);
        for (auto &seg : dashSegs) {
          vtess::expandStroke(seg, false, hw, adjusted.stroke.cap, ej,
                              strokeTris);
        }
      }
      std::vector<float> strokeWorld;
      strokeWorld.reserve(strokeTris.size());
      for (int i = 0; i + 1 < (int)strokeTris.size(); i += 2) {
        auto p = s.xform.apply({strokeTris[i], strokeTris[i + 1]});
        strokeWorld.push_back(p.x);
        strokeWorld.push_back(p.y);
      }
      if (!strokeWorld.empty())
        drawTris(strokeWorld, s.stroke.color, mvp);
    }
  }

  // =========================================================================
  // ── renderGhost ───────────────────────────────────────────────────────────
  void renderGhost(const float mvp[16]) {
    RGBA ghostStroke = {0.2f, 0.5f, 0.9f, 1.0f};
    if (tool_ == VTool::Pen && penInProgress_ && !penAnchors_.empty()) {
      std::vector<vmath::Vec2> ghostPts = penAnchors_;
      ghostPts.push_back(penCursor_);
      std::vector<float> strokeTris;
      float ghostHW = 1.5f / currentZoom_;
      vtess::expandStroke(ghostPts, false, ghostHW, LineCapV::Round,
                          LineJoinV::Round, strokeTris);
      drawTris(strokeTris, ghostStroke, mvp);
      float dR = 4.f / currentZoom_;
      for (auto &a : penAnchors_)
        drawDiamond(a.x, a.y, dR, {1, 1, 1, 0.9f}, mvp);
      if (vmath::len(penCPs_.back() - penAnchors_.back()) > 2.f) {
        float cpR = 4.f / currentZoom_;
        drawCircle(penCPs_.back().x, penCPs_.back().y, cpR, {1, .8f, .2f, 1.f},
                   mvp);
        std::vector<float> cpLine;
        vtess::expandStroke({penAnchors_.back(), penCPs_.back()}, false,
                            0.8f / currentZoom_, LineCapV::Butt,
                            LineJoinV::Miter, cpLine);
        drawTris(cpLine, {1, .8f, .2f, .6f}, mvp);
      }
    }
    if (shapeDragging_) {
      float fw_ = float(w_), fh_ = float(h_);
      float gx = std::clamp(mx_, 0.f, fw_), gy = std::clamp(my_, 0.f, fh_);
      float x0 = min(shapeX0_, gx), y0 = min(shapeY0_, gy);
      float x1 = max(shapeX0_, gx), y1 = max(shapeY0_, gy);
      VShape ghost;
      ghost.fill = activeFill_;
      ghost.fill.color.a *= .5f;
      ghost.stroke = activeStroke_;
      ghost.id = 0;
      switch (tool_) {
      case VTool::Rect:
        ghost.kind = VShapeKind::Rect;
        ghost.rect = {x0, y0, x1 - x0, y1 - y0, 0, 0};
        break;
      case VTool::Ellipse:
        ghost.kind = VShapeKind::Ellipse;
        ghost.ellipse = {(x0 + x1) * .5f, (y0 + y1) * .5f, (x1 - x0) * .5f,
                         (y1 - y0) * .5f};
        break;
      case VTool::Line:
        ghost.kind = VShapeKind::Poly;
        ghost.poly.pts = {{shapeX0_, shapeY0_}, {mx_, my_}};
        ghost.poly.closed = false;
        ghost.fill.none = true;
        break;
      default:
        break;
      }
      if (ghost.kind != VShapeKind::Path)
        renderShape(ghost, mvp);
    }
    if (draggingBox_) {
      std::vector<vmath::Vec2> box = {{boxX0_, boxY0_},
                                      {boxX1_, boxY0_},
                                      {boxX1_, boxY1_},
                                      {boxX0_, boxY1_}};
      std::vector<float> fill;
      vtess::earClip(box, fill);
      drawTris(fill, {0.3f, 0.5f, 1.f, 0.12f}, mvp);
      box.push_back(box.front());
      drawLineLoop(box, {0.4f, 0.6f, 1.f, 0.8f}, 1.f / currentZoom_, mvp);
    }

    // ── Text tool: draw a placement cursor when no session is active ────
    if (tool_ == VTool::Text && !textSession_.active) {
      // Draw an I-beam cursor at the current mouse position
      float cw = 8.f / currentZoom_,
            ch = float(activeTextStyle_.fontSize) / currentZoom_;
      if (ch < 12.f / currentZoom_)
        ch = 12.f / currentZoom_;
      std::vector<float> ibeam;
      // Vertical bar
      vtess::expandStroke({{mx_, my_}, {mx_, my_ - ch}}, false,
                          1.f / currentZoom_, LineCapV::Butt, LineJoinV::Miter,
                          ibeam);
      // Top serif
      vtess::expandStroke(
          {{mx_ - cw * .5f, my_ - ch}, {mx_ + cw * .5f, my_ - ch}}, false,
          1.f / currentZoom_, LineCapV::Butt, LineJoinV::Miter, ibeam);
      // Bottom serif
      vtess::expandStroke({{mx_ - cw * .5f, my_}, {mx_ + cw * .5f, my_}}, false,
                          1.f / currentZoom_, LineCapV::Butt, LineJoinV::Miter,
                          ibeam);
      drawTris(ibeam, {0.3f, 0.7f, 1.f, 0.85f}, mvp);
    }
  }

  // =========================================================================
  // ── renderSelection ───────────────────────────────────────────────────────
  void renderSelection(const float mvp[16]) {
    if (selection_.empty())
      return;
    bool rotateMode = (transformMode_ == TransformMode::Rotate);
    bool moveMode = (transformMode_ == TransformMode::Move);
    float pad = 4.f / currentZoom_;
    vmath::AABB bb = selectionBB();
    if (!bb.valid())
      return;
    RGBA boxCol = rotateMode ? RGBA{0.9f, 0.6f, 0.1f, 0.85f}
                  : moveMode ? RGBA{0.3f, 0.9f, 0.4f, 0.85f}
                             : RGBA{0.3f, 0.6f, 1.0f, 0.9f};
    std::vector<vmath::Vec2> box = {{bb.x0 - pad, bb.y0 - pad},
                                    {bb.x1 + pad, bb.y0 - pad},
                                    {bb.x1 + pad, bb.y1 + pad},
                                    {bb.x0 - pad, bb.y1 + pad},
                                    {bb.x0 - pad, bb.y0 - pad}};
    float lw = 1.f / currentZoom_;
    drawLineLoop(box, boxCol, lw, mvp);

    // For text objects show a special dashed bounding box hint
    if (selection_.size() == 1) {
      auto it = std::find_if(scene_.begin(), scene_.end(),
                             [&](auto &s) { return s.id == selection_[0]; });
      if (it != scene_.end() && it->kind == VShapeKind::Text) {
        // Draw a subtle label under the selection box
        // (no further decoration needed — the text itself is visible)
      }
    }

    vmath::Vec2 hpts[8];
    getHandlePositions(bb, pad, hpts);
    float hs = 4.5f / currentZoom_;

    for (int i = 0; i < 8; i++) {
      float hx = hpts[i].x, hy = hpts[i].y;
      if (rotateMode) {
        drawCircleRing(hx, hy, hs * 1.1f, 1.5f / currentZoom_,
                       {1.f, 0.8f, 0.2f, 1.f}, mvp);
      } else if (moveMode) {
        // Draw a simple cross/plus marker for move mode
        drawCircle(hx, hy, hs * 0.7f, {0.4f, 1.f, 0.6f, 0.9f}, mvp);
      } else {
        float b = hs + 1.5f / currentZoom_;
        drawSquare(hx, hy, b, boxCol, mvp);
        drawSquare(hx, hy, hs, {1, 1, 1, 1}, mvp);
      }
    }
    if (rotateMode) {
      float arrowR = hs * 3.5f, arrowLW = 1.8f / currentZoom_;
      RGBA arrowCol = {1.f, 0.75f, 0.1f, 0.9f};
      const float cornerAngles[4] = {3.14159265f * 1.25f, 3.14159265f * 1.75f,
                                     3.14159265f * 0.75f, 3.14159265f * 0.25f};
      int cornerIdx[4] = {0, 2, 5, 7};
      for (int ci = 0; ci < 4; ci++) {
        float ax = hpts[cornerIdx[ci]].x, ay = hpts[cornerIdx[ci]].y;
        drawRotateArrow(ax, ay, arrowR, cornerAngles[ci], 3.14159265f / 3.f,
                        arrowLW, arrowCol, mvp);
      }
    }
    {
      vmath::Vec2 orig = originSet_ ? transformOrigin_ : bb.center();
      float or_ = 5.f / currentZoom_, lw2 = 1.5f / currentZoom_;
      RGBA origCol = {1.f, 1.f, 1.f, 0.95f},
           origBorderCol = {0.2f, 0.2f, 0.2f, 0.8f};
      std::vector<float> hline, vline;
      vtess::expandStroke(
          {{orig.x - or_ * 1.8f, orig.y}, {orig.x + or_ * 1.8f, orig.y}}, false,
          lw2 * .5f, LineCapV::Butt, LineJoinV::Miter, hline);
      vtess::expandStroke(
          {{orig.x, orig.y - or_ * 1.8f}, {orig.x, orig.y + or_ * 1.8f}}, false,
          lw2 * .5f, LineCapV::Butt, LineJoinV::Miter, vline);
      drawTris(hline, origBorderCol, mvp);
      drawTris(vline, origBorderCol, mvp);
      drawCircleRing(orig.x, orig.y, or_, lw2, origBorderCol, mvp);
      drawCircle(orig.x, orig.y, or_ * .4f, origCol, mvp);
    }
    if (tool_ != VTool::Node)
      return;
    RGBA anchorCol = {1, 1, 1, 1}, cpCol = {1, .8f, .2f, 1};
    float aR = 5.f / currentZoom_, cpR = 4.f / currentZoom_,
          stemW = 0.8f / currentZoom_;
    for (auto id : selection_) {
      auto it = std::find_if(scene_.begin(), scene_.end(),
                             [id](auto &s) { return s.id == id; });
      if (it == scene_.end())
        continue;
      auto &s = *it;
      if (s.kind == VShapeKind::Poly) {
        for (auto &p : s.poly.pts) {
          auto wp = s.xform.apply(p);
          drawDiamond(wp.x, wp.y, aR, anchorCol, mvp);
        }
      } else if (s.kind == VShapeKind::Path) {
        vmath::Vec2 prev{};
        for (auto &cmd : s.path.cmds) {
          if (cmd.kind == PathCmdKind::MoveTo ||
              cmd.kind == PathCmdKind::LineTo) {
            auto wp = s.xform.apply(cmd.pt[0]);
            drawDiamond(wp.x, wp.y, aR, anchorCol, mvp);
            prev = wp;
          } else if (cmd.kind == PathCmdKind::CubicTo) {
            auto cp1 = s.xform.apply(cmd.pt[0]), cp2 = s.xform.apply(cmd.pt[1]),
                 end = s.xform.apply(cmd.pt[2]);
            std::vector<float> stem1, stem2;
            vtess::expandStroke({prev, cp1}, false, stemW, LineCapV::Butt,
                                LineJoinV::Miter, stem1);
            vtess::expandStroke({end, cp2}, false, stemW, LineCapV::Butt,
                                LineJoinV::Miter, stem2);
            drawTris(stem1, {cpCol.r, cpCol.g, cpCol.b, .5f}, mvp);
            drawTris(stem2, {cpCol.r, cpCol.g, cpCol.b, .5f}, mvp);
            drawCircle(cp1.x, cp1.y, cpR, cpCol, mvp);
            drawCircle(cp2.x, cp2.y, cpR, cpCol, mvp);
            drawDiamond(end.x, end.y, aR, anchorCol, mvp);
            prev = end;
          }
        }
      }
    }
  }
};

// ============================================================================
// §V7  FACTORY HELPERS
// ============================================================================

inline std::shared_ptr<CanvasWidget> VectorCanvas(int viewW, int viewH,
                                                  int canvasW, int canvasH) {
  auto c = std::make_shared<CanvasWidget>();
  c->setSize(viewW, viewH);
  c->setCanvasSize(canvasW, canvasH);
  c->setSurface<VectorSurface>();
  return c;
}
inline std::shared_ptr<CanvasWidget> VectorCanvas(int w, int h) {
  return VectorCanvas(w, h, w, h);
}

#endif // FLUX_VECTOR_HPP