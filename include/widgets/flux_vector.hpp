#ifndef FLUX_VECTOR_HPP
#define FLUX_VECTOR_HPP

// ============================================================================
// flux_vector.hpp  —  VectorSurface
// ============================================================================

#include <algorithm>
#include <cassert>
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

enum class FillRule { NonZero, EvenOdd };

struct VFill {
  RGBA color = {0, 0, 0, 1};
  FillRule rule = FillRule::NonZero;
  bool none = false;
};

enum class LineCapV { Butt, Round, Square };
enum class LineJoinV { Miter, Round, Bevel, Butt };

struct VStroke {
  RGBA color = {0, 0, 0, 1};
  float width = 1.f;
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
    int a = idx[(i - 1 + idx.size()) % idx.size()];
    int b = idx[i];
    int c = idx[(i + 1) % idx.size()];
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
        int a = idx[(i - 1 + idx.size()) % idx.size()];
        int b = idx[i];
        int c = idx[(i + 1) % idx.size()];
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
  case VShapeKind::Text:
    cs.push_back({{s.text.x, s.text.y},
                  {s.text.x + 100, s.text.y + float(s.text.style.fontSize)}});
    break;
  }
  AABB bb;
  for (auto &c : cs)
    for (auto &p : c)
      bb.expand(s.xform.apply(p));
  return bb;
}

} // namespace vtess

// ============================================================================
// §V3  TOOL ENUM
// ============================================================================

enum class VTool { Select, Pen, Node, Rect, Ellipse, Line };

// ============================================================================
// §V4  UNDO SNAPSHOT
// ============================================================================

struct VSnapshot {
  VScene scene;
  size_t estBytes;
};

// ============================================================================
// §V5  TRANSFORM HANDLE SYSTEM
// ============================================================================

// Handle indices (8 scale + 8 rotate zones = 16 total)
//
//  Scale mode:
//    0=TL  1=TM  2=TR
//    3=ML        4=MR
//    5=BL  6=BM  7=BR
//
//  Rotate mode (same positions, different cursor/behaviour):
//    8=TL  9=TM  10=TR
//    11=ML       12=MR
//    13=BL 14=BM 15=BR
//
//  17 = transform origin (pivot point)

enum class HandleKind { None, Scale, Rotate, Origin };

struct TransformHandle {
  HandleKind kind = HandleKind::None;
  int index = -1; // 0-7 for scale, 0-7 for rotate (same layout)
};

// Which axis each scale handle constrains
//   lockX=true means the x-anchor is fixed (edge midpoints)
//   lockY=true means the y-anchor is fixed
struct ScaleConstraint {
  bool lockX, lockY;
};
static constexpr ScaleConstraint kScaleConstraints[8] = {
    {false, false}, // TL — free
    {true, false},  // TM — vertical only
    {false, false}, // TR — free
    {false, true},  // ML — horizontal only
    {false, true},  // MR — horizontal only
    {false, false}, // BL — free
    {true, false},  // BM — vertical only
    {false, false}, // BR — free
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
    tool_ = t;
    cancelInProgress();
    dirty_ = true;
  }
  VTool getTool() const { return tool_; }

  // ── Active style ──────────────────────────────────────────────────────────
  VFill &activeFill() { return activeFill_; }
  VStroke &activeStroke() { return activeStroke_; }
  void setActiveFill(const VFill &f) { activeFill_ = f; }
  void setActiveStroke(const VStroke &s) { activeStroke_ = s; }

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
  void update(double) override {}

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
      std::vector<float> artQuad = {
          0.f,       float(w_), 0.f, 0.f,       float(w_), 0.f,
          float(w_), float(h_), 0.f, float(h_), 0.f,       0.f,
      };
      // rebuild artboard quad correctly
      std::vector<float> aq = {
          0.f,       0.f,       float(w_), 0.f,       float(w_), float(h_),
          float(w_), float(h_), 0.f,       float(h_), 0.f,       0.f,
      };
      drawTris(aq, artboardColor_, mvp);
    }

    for (auto &shape : scene_)
      renderShape(shape, mvp);
    renderGhost(mvp);

    if (showArtboard_) {
      RGBA borderCol = {0.18f, 0.18f, 0.18f, 1.f};
      drawBorderRect(0.f, 0.f, float(w_), float(h_), 1.f, borderCol, mvp);
    }

    renderSelection(mvp);
    glDisable(GL_BLEND);
    GL.useProgram(0);
    dirty_ = false;
  }

  bool needsContinuousRedraw() const override { return false; }

  void onMouseDown(float x, float y) override {
    mx_ = x;
    my_ = y;
    mdownX_ = x;
    mdownY_ = y;
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
    case VTool::Rect:
    case VTool::Ellipse:
    case VTool::Line:
      onShapeMove(x, y);
      break;
    }
    dirty_ = true;
  }

  void onMouseUp(float x, float y) override {
    switch (tool_) {
    case VTool::Select:
      onSelectUp(x, y);
      break;
    case VTool::Pen:
      break;
    case VTool::Node:
      onNodeUp();
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
    if (key == VK_ESCAPE) {
      if (tool_ == VTool::Pen && penInProgress_)
        commitPen();
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
    else
      deselectAll();
    dirty_ = true;
  }

  void destroy() override {
    if (prog_) {
      GL.deleteProgram(prog_);
      prog_ = 0;
    }
    if (vao_) {
      GL.deleteVertexArrays(1, &vao_);
      vao_ = 0;
    }
    if (vbo_) {
      GL.deleteBuffers(1, &vbo_);
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
  bool showArtboard() const { return showArtboard_; }

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
  VFill activeFill_ = {{0.2f, 0.5f, 1.f, 1.f}, FillRule::NonZero, false};
  VStroke activeStroke_ = {{0.f, 0.f, 0.f, 1.f}, 2.f, LineCapV::Round,
                           LineJoinV::Round,     {},  false};

  size_t undoBudget_, undoBytes_ = 0;
  std::vector<VSnapshot> undoStack_, redoStack_;

  float mx_ = 0, my_ = 0, mdownX_ = 0, mdownY_ = 0;

  // ── Select state ─────────────────────────────────────────────────────────
  bool draggingShape_ = false, draggingBox_ = false;
  float boxX0_ = 0, boxY0_ = 0, boxX1_ = 0, boxY1_ = 0;
  std::vector<std::pair<VShapeId, vmath::Mat3>> dragOrigins_;

  // ── Transform handle state ───────────────────────────────────────────────

  // Which mode the selection box is in
  enum class TransformMode { Scale, Rotate };
  TransformMode transformMode_ = TransformMode::Scale;

  // The handle currently being dragged
  bool draggingHandle_ = false;
  HandleKind dragHandleKind_ = HandleKind::None;
  int dragHandleIndex_ = -1;

  // Snapshot of selection AABB at drag start (canvas space)
  vmath::AABB dragStartBB_;

  // Per-shape original transforms at drag start
  std::vector<std::pair<VShapeId, vmath::Mat3>> handleDragOrigins_;

  // For rotate: start angle relative to bbox center
  float rotateStartAngle_ = 0.f;

  // Transform origin (pivot) in canvas space, defaults to bbox center
  vmath::Vec2 transformOrigin_{0, 0};
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

  // ── GL ────────────────────────────────────────────────────────────────────
  GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
  float currentZoom_ = 1.f;
  float currentOffsetX_ = 0.f;
  float currentOffsetY_ = 0.f;
  struct ULocs {
    GLint mvp = -1, mode = -1, color = -1, tex = -1, alpha = -1;
  } u_;

  struct TextTex {
    GLuint tex = 0;
    int w = 0, h = 0;
    float ox = 0, oy = 0;
  };
  std::unordered_map<uint64_t, TextTex> textTexCache_;
  ULONG_PTR gdiplusToken_ = 0;

  // =========================================================================
  // ── Handle geometry helpers ───────────────────────────────────────────────
  // Returns the 8 handle positions for the current selection bbox
  // Index layout:
  //   0=TL 1=TM 2=TR
  //   3=ML       4=MR
  //   5=BL 6=BM 7=BR

  static void getHandlePositions(const vmath::AABB &bb, float pad,
                                 vmath::Vec2 out[8]) {
    float x0 = bb.x0 - pad, y0 = bb.y0 - pad;
    float x1 = bb.x1 + pad, y1 = bb.y1 + pad;
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

  // Compute the combined AABB of the current selection
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

  // Hit-test the 8 scale/rotate handles.
  // Returns handle index 0-7 or -1.
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

  // Hit-test the transform origin circle
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
    // Default origin = bbox center (only reset when not already set by user)
    if (!originSet_) {
      transformOrigin_ = dragStartBB_.center();
    }
    handleDragOrigins_.clear();
    for (auto id : selection_) {
      auto *s = beginEdit(id);
      if (s)
        handleDragOrigins_.push_back({id, s->xform});
    }
    if (dragHandleKind_ == HandleKind::Rotate) {
      rotateStartAngle_ =
          std::atan2(y - transformOrigin_.y, x - transformOrigin_.x);
    }
  }

  // Apply scale transform from handle drag
  void applyScaleHandle(float x, float y, bool shiftConstrain) {
    if (!dragStartBB_.valid())
      return;
    int hi = dragHandleIndex_;

    // Anchor point = opposite handle
    //   TL(0) anchors BR(7), TR(2) anchors BL(5), etc.
    static const int kOpposite[8] = {7, 6, 5, 4, 3, 2, 1, 0};
    vmath::Vec2 pts[8];
    float pad = 4.f / currentZoom_;
    getHandlePositions(dragStartBB_, pad, pts);
    vmath::Vec2 anchor = pts[kOpposite[hi]];

    // The dragged handle's original position
    vmath::Vec2 origHandle = pts[hi];

    // New size defined by mouse position
    float newDX = x - anchor.x, newDY = y - anchor.y;
    float oldDX = origHandle.x - anchor.x, oldDY = origHandle.y - anchor.y;

    auto &c = kScaleConstraints[hi];
    float sx = c.lockX ? 1.f : (std::abs(oldDX) > 1e-4f ? newDX / oldDX : 1.f);
    float sy = c.lockY ? 1.f : (std::abs(oldDY) > 1e-4f ? newDY / oldDY : 1.f);

    // Shift = uniform scale (use the larger axis)
    if (shiftConstrain && !c.lockX && !c.lockY) {
      float s = std::abs(sx) > std::abs(sy) ? sx : sy;
      sx = sy = s;
    }

    // Build transform: translate(anchor) * scale(sx,sy) * translate(-anchor)
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

  // Apply rotate transform from handle drag
  void applyRotateHandle(float x, float y, bool shiftConstrain) {
    float angle = std::atan2(y - transformOrigin_.y, x - transformOrigin_.x);
    float dAngle = angle - rotateStartAngle_;

    // Shift = snap to 45° increments
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

    // 1. Check transform origin drag
    if (!selection_.empty() && hitTestOrigin(x, y)) {
      draggingOrigin_ = true;
      draggingHandle_ = false;
      draggingShape_ = false;
      draggingBox_ = false;
      return;
    }

    // 2. Check handle hit (only when something is selected)
    if (!selection_.empty()) {
      int hi = hitTestHandles(x, y, pad);
      if (hi >= 0) {
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

    // 3. Normal shape click
    VShapeId hit = hitTest(x, y);
    if (hit != kNoShape) {
      bool alreadySelected = std::find(selection_.begin(), selection_.end(),
                                       hit) != selection_.end();

      if (!shift) {
        if (alreadySelected && selection_.size() == 1) {
          // Second click on same shape → toggle rotate mode
          transformMode_ = (transformMode_ == TransformMode::Scale)
                               ? TransformMode::Rotate
                               : TransformMode::Scale;
          // Reset origin to bbox center on mode toggle
          originSet_ = false;
          transformOrigin_ = selectionBB().center();
          dirty_ = true;
          return; // Don't start a drag
        }
        if (!alreadySelected)
          selection_.clear();
      }
      select(hit, shift);
      // Reset transform mode & origin when selecting a new shape
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
      float totalDX = x - mdownX_, totalDY = y - mdownY_;
      for (auto &[id, orig] : dragOrigins_) {
        auto *s = beginEdit(id);
        if (!s)
          continue;
        s->xform.m[6] = orig.m[6] + totalDX;
        s->xform.m[7] = orig.m[7] + totalDY;
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
      // Sync origin to new bbox center after transform
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
    penAnchors_.push_back({x, y});
    penCPs_.push_back({x, y});
    penCursor_ = {x, y};
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
  }

  // =========================================================================
  // ── Tool: Node  (FIXED — uses command index ci, not anchor counter ai) ────

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
      // Use command index (ci) directly — avoids the double-increment bug
      for (int ci = 0; ci < (int)s->path.cmds.size(); ci++) {
        auto &cmd = s->path.cmds[ci];
        if (cmd.kind == PathCmdKind::MoveTo ||
            cmd.kind == PathCmdKind::LineTo) {
          if (checkPt(cmd.pt[0], ci, false, 0))
            return;
        } else if (cmd.kind == PathCmdKind::CubicTo) {
          if (checkPt(cmd.pt[0], ci, true, 0))
            return; // cp1
          if (checkPt(cmd.pt[1], ci, true, 1))
            return; // cp2
          if (checkPt(cmd.pt[2], ci, false, 0))
            return; // endpoint
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
    vmath::Vec2 local{x, y};

    if (s->kind == VShapeKind::Poly) {
      if (dragNodeIdx_ < (int)s->poly.pts.size())
        s->poly.pts[dragNodeIdx_] = local;
    } else if (s->kind == VShapeKind::Path) {
      // dragNodeIdx_ IS the command index now — direct lookup, no re-counting
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
    shapeX0_ = x;
    shapeY0_ = y;
  }
  void onShapeMove(float, float) {}
  void onShapeUp(float x, float y) {
    if (!shapeDragging_)
      return;
    shapeDragging_ = false;
    float x0 = min(shapeX0_, x), y0 = min(shapeY0_, y);
    float x1 = max(shapeX0_, x), y1 = max(shapeY0_, y);
    if (x1 - x0 < 2.f && y1 - y0 < 2.f)
      return;

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
    // Reset transform state for newly created shape
    transformMode_ = TransformMode::Scale;
    originSet_ = false;
    transformOrigin_ = selectionBB().center();
    dirty_ = true;
  }

  // =========================================================================
  // ── GL resource helpers ───────────────────────────────────────────────────

  void buildShaders() {
    const char *vert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=uMVP*vec4(aPos,0,1); }
)GLSL";
    const char *frag = R"GLSL(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4  uColor;
uniform float uAlpha;
uniform int   uMode;
out vec4 fragColor;
void main(){
  if(uMode==0){ vec4 c=texture(uTex,vUV); fragColor=vec4(c.rgb,c.a*uAlpha); }
  else         { fragColor=uColor; }
}
)GLSL";
    prog_ = glutil::linkProgram(vert, frag);
    assert(prog_);
    u_.mvp = GL.getUniformLocation(prog_, "uMVP");
    u_.mode = GL.getUniformLocation(prog_, "uMode");
    u_.color = GL.getUniformLocation(prog_, "uColor");
    u_.tex = GL.getUniformLocation(prog_, "uTex");
    u_.alpha = GL.getUniformLocation(prog_, "uAlpha");
  }

  void buildBuffers() {
    GL.genVertexArrays(1, &vao_);
    GL.genBuffers(1, &vbo_);
    GL.bindVertexArray(vao_);
    GL.bindBuffer(GL_ARRAY_BUFFER, vbo_);
    GL.bufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 4096, nullptr,
                  GL_DYNAMIC_DRAW);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2,
                           nullptr);
    GL.disableVertexAttribArray(1);
    GL.bindVertexArray(0);
  }

  void drawTris(const std::vector<float> &verts, const RGBA &col,
                const float mvp[16]) {
    if (verts.empty())
      return;
    GL.useProgram(prog_);
    GL.uniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    GL.uniform1i(u_.mode, 1);
    GL.uniform4f(u_.color, col.r, col.g, col.b, col.a);
    GL.bindVertexArray(vao_);
    GL.bindBuffer(GL_ARRAY_BUFFER, vbo_);
    GL.bufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(float)),
                  verts.data(), GL_DYNAMIC_DRAW);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2,
                           nullptr);
    GL.disableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, (int)(verts.size() / 2));
    GL.bindVertexArray(0);
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
    std::vector<float> v = {
        x, y + r, x + r, y, x, y - r, x, y + r, x, y - r, x - r, y,
    };
    drawTris(v, col, mvp);
  }

  void drawSquare(float x, float y, float r, const RGBA &col,
                  const float mvp[16]) {
    std::vector<float> v = {
        x - r, y - r, x + r, y - r, x + r, y + r,
        x - r, y - r, x + r, y + r, x - r, y + r,
    };
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

  // Draw a hollow circle (ring) as thick stroke
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

  // Draw a curved rotate-arrow arc with arrowhead (indicate rotate mode)
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

    // Arrowhead at end of arc
    float endA = startA + sweepA;
    vmath::Vec2 tip = {cx + cosf(endA) * r, cy + sinf(endA) * r};
    // Direction tangent to circle at tip
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
  // ── Text texture ──────────────────────────────────────────────────────────
  // (unchanged from original)

  uint64_t textHash(const VText &t) const {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](uint64_t v) {
      h ^= v;
      h *= 1099511628211ULL;
    };
    for (wchar_t c : t.text)
      mix(c);
    mix(*(uint32_t *)&t.x);
    mix(*(uint32_t *)&t.y);
    mix(t.style.fontSize);
    mix(uint64_t(t.style.bold) | (uint64_t(t.style.italic) << 1));
    return h;
  }

  const TextTex &getTextTex(const VShape &s) {
    uint64_t key = textHash(s.text);
    auto it = textTexCache_.find(key);
    if (it != textTexCache_.end())
      return it->second;

    TextTex tt;
    int fw = max(1, w_), fh = max(1, h_);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = fw;
    bmi.bmiHeader.biHeight = -fh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hBmp =
        CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp) {
      DeleteDC(hdcMem);
      textTexCache_[key] = tt;
      return textTexCache_[key];
    }
    HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);
    RECT rc = {0, 0, fw, fh};
    HBRUSH br = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdcMem, &rc, br);
    DeleteObject(br);
    auto &ts = s.text.style;
    int weight = ts.bold ? FW_BOLD : FW_NORMAL;
    HFONT hFont =
        CreateFontW(-ts.fontSize, 0, 0, 0, weight, ts.italic ? TRUE : FALSE,
                    ts.underline ? TRUE : FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, ts.fontFace.c_str());
    HGDIOBJ oldFont = SelectObject(hdcMem, hFont);
    COLORREF cr = RGB(int(ts.r * 255 + .5f), int(ts.g * 255 + .5f),
                      int(ts.b * 255 + .5f));
    SetTextColor(hdcMem, cr);
    SetBkMode(hdcMem, TRANSPARENT);
    int gx = (int)s.text.x, gy = fh - (int)s.text.y - ts.fontSize;
    SIZE tsz = {};
    GetTextExtentPoint32W(hdcMem, s.text.text.c_str(), (int)s.text.text.size(),
                          &tsz);
    TextOutW(hdcMem, gx, gy, s.text.text.c_str(), (int)s.text.text.size());
    GdiFlush();
    int bx0 = max(0, gx - 2), by0 = max(0, gy - 2);
    int bx1 = min(fw, gx + tsz.cx + 2), by1 = min(fh, gy + tsz.cy + 2);
    int bw = bx1 - bx0, bh = by1 - by0;
    tt.w = bw;
    tt.h = bh;
    tt.ox = (float)bx0;
    tt.oy = (float)(fh - by1);
    if (bw > 0 && bh > 0) {
      const uint8_t *dib = reinterpret_cast<const uint8_t *>(bits);
      std::vector<uint8_t> rgba(bw * bh * 4);
      for (int row = 0; row < bh; row++) {
        int gdiRow = by0 + (bh - 1 - row);
        for (int col = 0; col < bw; col++) {
          const uint8_t *p = dib + (size_t(gdiRow) * fw + bx0 + col) * 4;
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
    }
    SelectObject(hdcMem, oldFont);
    DeleteObject(hFont);
    SelectObject(hdcMem, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    textTexCache_[key] = tt;
    return textTexCache_[key];
  }

  void blitTextTex(const TextTex &tt, const float mvp[16]) {
    if (!tt.tex || tt.w <= 0 || tt.h <= 0)
      return;
    float x0 = tt.ox, y0 = tt.oy, x1 = x0 + tt.w, y1 = y0 + tt.h;
    float q[] = {x0, y0, 0, 0, x1, y0, 1, 0, x1, y1, 1, 1,
                 x1, y1, 1, 1, x0, y1, 0, 1, x0, y0, 0, 0};
    GL.useProgram(prog_);
    GL.uniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    GL.uniform1i(u_.mode, 0);
    GL.uniform1i(u_.tex, 0);
    GL.uniform1f(u_.alpha, 1.f);
    glBindTexture(GL_TEXTURE_2D, tt.tex);
    GL.bindVertexArray(vao_);
    GL.bindBuffer(GL_ARRAY_BUFFER, vbo_);
    GL.bufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_DYNAMIC_DRAW);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                           (void *)0);
    GL.enableVertexAttribArray(1);
    GL.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                           (void *)(sizeof(float) * 2));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    GL.bindVertexArray(0);
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
      blitTextTex(tt, mvp);
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
      for (auto &c : fillContours)
        vtess::earClip(c, fillTris);
      std::vector<float> fillWorld;
      fillWorld.reserve(fillTris.size());
      for (int i = 0; i + 1 < (int)fillTris.size(); i += 2) {
        auto p = s.xform.apply({fillTris[i], fillTris[i + 1]});
        fillWorld.push_back(p.x);
        fillWorld.push_back(p.y);
      }
      if (!fillWorld.empty())
        drawTris(fillWorld, adjusted.fill.color, mvp);
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
        vtess::expandStroke(snapped, isClosed, hw, adjusted.stroke.cap, ej,
                            strokeTris);
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
        float stemW = 0.8f / currentZoom_;
        vtess::expandStroke({penAnchors_.back(), penCPs_.back()}, false, stemW,
                            LineCapV::Butt, LineJoinV::Miter, cpLine);
        drawTris(cpLine, {1, .8f, .2f, .6f}, mvp);
      }
    }

    if (shapeDragging_) {
      float x0 = min(shapeX0_, mx_), y0 = min(shapeY0_, my_);
      float x1 = max(shapeX0_, mx_), y1 = max(shapeY0_, my_);
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
  }

  // =========================================================================
  // ── renderSelection  (full transform handles) ─────────────────────────────

  void renderSelection(const float mvp[16]) {
    if (selection_.empty())
      return;

    bool rotateMode = (transformMode_ == TransformMode::Rotate);
    float pad = 4.f / currentZoom_;

    // Gather combined bbox
    vmath::AABB bb = selectionBB();
    if (!bb.valid())
      return;

    // ── Bounding box outline ─────────────────────────────────────────────
    RGBA boxCol = rotateMode
                      ? RGBA{0.9f, 0.6f, 0.1f, 0.85f} // orange in rotate mode
                      : RGBA{0.3f, 0.6f, 1.0f, 0.9f}; // blue in scale mode

    std::vector<vmath::Vec2> box = {{bb.x0 - pad, bb.y0 - pad},
                                    {bb.x1 + pad, bb.y0 - pad},
                                    {bb.x1 + pad, bb.y1 + pad},
                                    {bb.x0 - pad, bb.y1 + pad},
                                    {bb.x0 - pad, bb.y0 - pad}};
    float lw = 1.f / currentZoom_;
    drawLineLoop(box, boxCol, lw, mvp);

    // ── 8 transform handles ───────────────────────────────────────────────
    vmath::Vec2 hpts[8];
    getHandlePositions(bb, pad, hpts);

    float hs = 4.5f / currentZoom_; // half-size of handle square

    for (int i = 0; i < 8; i++) {
      float hx = hpts[i].x, hy = hpts[i].y;
      if (rotateMode) {
        // Rotate mode: open circle handles (Illustrator style)
        drawCircleRing(hx, hy, hs * 1.1f, 1.5f / currentZoom_,
                       {1.f, 0.8f, 0.2f, 1.f}, mvp);
      } else {
        // Scale mode: white square with blue border
        float b = hs + 1.5f / currentZoom_;
        // border square
        drawSquare(hx, hy, b, boxCol, mvp);
        // white fill
        drawSquare(hx, hy, hs, {1, 1, 1, 1}, mvp);
      }
    }

    // ── Rotate-mode: draw curved arrow indicators at corners ─────────────
    if (rotateMode) {
      float arrowR = hs * 3.5f;
      float arrowLW = 1.8f / currentZoom_;
      RGBA arrowCol = {1.f, 0.75f, 0.1f, 0.9f};
      // Draw a small curved arrow arc outside each corner handle
      const float cornerAngles[4] = {
          // center angles for each corner
          3.14159265f * 1.25f, // TL
          3.14159265f * 1.75f, // TR
          3.14159265f * 0.75f, // BL
          3.14159265f * 0.25f, // BR
      };
      int cornerIdx[4] = {0, 2, 5, 7};
      for (int ci = 0; ci < 4; ci++) {
        float ax = hpts[cornerIdx[ci]].x, ay = hpts[cornerIdx[ci]].y;
        float startA = cornerAngles[ci];
        float sweep = 3.14159265f / 3.f; // 60° arc
        drawRotateArrow(ax, ay, arrowR, startA, sweep, arrowLW, arrowCol, mvp);
      }
    }

    // ── Transform origin marker ───────────────────────────────────────────
    {
      vmath::Vec2 orig = originSet_ ? transformOrigin_ : bb.center();
      float or_ = 5.f / currentZoom_;
      float lw2 = 1.5f / currentZoom_;
      RGBA origCol = {1.f, 1.f, 1.f, 0.95f};
      RGBA origBorderCol = {0.2f, 0.2f, 0.2f, 0.8f};

      // Crosshair
      std::vector<float> hline, vline;
      vtess::expandStroke(
          {{orig.x - or_ * 1.8f, orig.y}, {orig.x + or_ * 1.8f, orig.y}}, false,
          lw2 * .5f, LineCapV::Butt, LineJoinV::Miter, hline);
      vtess::expandStroke(
          {{orig.x, orig.y - or_ * 1.8f}, {orig.x, orig.y + or_ * 1.8f}}, false,
          lw2 * .5f, LineCapV::Butt, LineJoinV::Miter, vline);
      drawTris(hline, origBorderCol, mvp);
      drawTris(vline, origBorderCol, mvp);

      // Circle
      drawCircleRing(orig.x, orig.y, or_, lw2, origBorderCol, mvp);
      drawCircle(orig.x, orig.y, or_ * .4f, origCol, mvp);
    }

    // ── Node handles for Node tool ────────────────────────────────────────
    if (tool_ != VTool::Node)
      return;

    RGBA anchorCol = {1, 1, 1, 1}, cpCol = {1, .8f, .2f, 1};
    float aR = 5.f / currentZoom_, cpR = 4.f / currentZoom_;
    float stemW = 0.8f / currentZoom_;

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
            auto cp1 = s.xform.apply(cmd.pt[0]);
            auto cp2 = s.xform.apply(cmd.pt[1]);
            auto end = s.xform.apply(cmd.pt[2]);
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