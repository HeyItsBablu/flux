#ifndef FLUX_VECTOR_HPP
#define FLUX_VECTOR_HPP

// ============================================================================
// flux_vector.hpp  —  VectorSurface
// ============================================================================
//
// Depends on:  flux_canvas.hpp  (GLProcs, glutil, RGBA, RenderSurface, and
//              all GL defines / typedefs declared there).
//
// Include order:
//   #include "flux_canvas.hpp"
//   #include "flux_vector.hpp"
//
// ── Overview ─────────────────────────────────────────────────────────────────
//
//   VectorSurface implements a fully interactive vector drawing layer on top
//   of CanvasWidget.  Every visible element is a VShape stored in a flat
//   scene-graph vector; shapes are rendered in order (painter's algorithm).
//
// ── Shape model (§V1) ────────────────────────────────────────────────────────
//
//   VShapeKind:  kRect | kEllipse | kLine | kPolyline | kPath | kText
//
//   VShape holds:
//     • kind        — discriminator
//     • transform   — 2-D affine (translate / rotate / scale) as a 3×3 matrix
//     • fill        — VFill  { color RGBA, rule (nonzero/evenodd) }
//     • stroke      — VStroke{ color RGBA, width, cap, join, dashArray }
//     • geometry    — union:
//         Rect    { x, y, w, h, rx, ry }       rounded corners
//         Ellipse { cx, cy, rx, ry }
//         Poly    { pts[], closed }             line / polyline / polygon
//         Path    { PathCmd[] }                 M/L/C/Q/Z commands
//         Text    { wstring, x, y, TextStyle }
//
// ── Tessellation (§V2) ───────────────────────────────────────────────────────
//
//   Shapes are tessellated into GL_TRIANGLES each frame via a simple CPU
//   tessellator (no retained cache — good enough for hundreds of shapes).
//   Strokes are expanded into quad-strips; fills use an ear-clip triangulator
//   for polygons and a fan for convex shapes.
//
//   Text is rasterised with GDI+ to a per-shape texture (cached by shape id
//   + text content hash).  The texture is composited as a textured quad.
//
// ── Tools (§V3) ──────────────────────────────────────────────────────────────
//
//   kToolSelect   — click to select, drag selected to move, Escape deselects.
//                   Drag on empty space → rubber-band box select.
//   kToolPen      — click to add anchor; double-click or close to finish path.
//                   Shift+drag an anchor drags its out-control-point (Bézier).
//   kToolNode     — click shape to reveal anchors; drag anchor or CP.
//   kToolRect     — drag to create Rect.
//   kToolEllipse  — drag to create Ellipse.
//   kToolLine     — drag to create Line (2-point Poly, open).
//
// ── Undo/Redo (§V4) ──────────────────────────────────────────────────────────
//
//   Snapshots are cheap: the entire scene_ vector is copied.  Each VShape is
//   move-friendly (std::vector<PathCmd> etc.) so deep-copies are O(total pts).
//   Budget is measured in estimated bytes (kBytesPerPoint * total points).
//
// ── Selection overlay (§V5) ──────────────────────────────────────────────────
//
//   Selected shapes get a dashed bounding-box overlay and, in kToolNode mode,
//   anchor diamonds and control-point circles drawn as GL lines / point quads.
//
// ── Render pipeline ──────────────────────────────────────────────────────────
//
//   1. Clear to transparent (surface is composited over the host background).
//   2. For each shape in scene order:
//        a. Tessellate fill → upload → draw.
//        b. Tessellate stroke → upload → draw.
//        c. If text: blit cached texture.
//   3. Draw in-progress pen/rect/ellipse/line ghost in a distinct color.
//   4. Draw selection overlays.
//
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
  // Decompose translate
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
};

} // namespace vmath

// ============================================================================
// §V1  SHAPE DATA MODEL
// ============================================================================

// ── Path commands
// ─────────────────────────────────────────────────────────────
enum class PathCmdKind : uint8_t {
  MoveTo,  // pt[0]
  LineTo,  // pt[0]
  CubicTo, // pt[0]=cp1, pt[1]=cp2, pt[2]=end
  QuadTo,  // pt[0]=cp,  pt[1]=end
  Close
};

struct PathCmd {
  PathCmdKind kind;
  vmath::Vec2 pt[3]; // up to 3 points depending on kind
};

// ── Fill ─────────────────────────────────────────────────────────────────────
enum class FillRule { NonZero, EvenOdd };

struct VFill {
  RGBA color = {0, 0, 0, 1};
  FillRule rule = FillRule::NonZero;
  bool none = false; // transparent / no fill
};

// ── Stroke ───────────────────────────────────────────────────────────────────
enum class LineCapV { Butt, Round, Square };
enum class LineJoinV { Miter, Round, Bevel };

struct VStroke {
  RGBA color = {0, 0, 0, 1};
  float width = 1.f;
  LineCapV cap = LineCapV::Round;
  LineJoinV join = LineJoinV::Round;
  std::vector<float> dash; // empty = solid; e.g. {6,3} = 6on 3off
  bool none = false;
};

// ── Shape kinds ──────────────────────────────────────────────────────────────
enum class VShapeKind : uint8_t { Rect, Ellipse, Poly, Path, Text };

// ── Per-kind geometry ────────────────────────────────────────────────────────
struct VRect {
  float x, y, w, h, rx, ry;
}; // rx/ry = corner radii
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

// ── Shape ID ─────────────────────────────────────────────────────────────────
using VShapeId = uint32_t;
static constexpr VShapeId kNoShape = 0;

// ── VShape ───────────────────────────────────────────────────────────────────
struct VShape {
  VShapeId id = kNoShape;
  VShapeKind kind = VShapeKind::Path;
  vmath::Mat3 xform; // local→canvas transform
  VFill fill;
  VStroke stroke;

  // geometry (only one is valid per kind)
  VRect rect{};
  VEllipse ellipse{};
  VPoly poly{};
  VPath path{};
  VText text{};

  // z-order is implicit (index in scene_)
};

// ── Scene ────────────────────────────────────────────────────────────────────
using VScene = std::vector<VShape>;

// ============================================================================
// §V2  TESSELLATOR
// ============================================================================
//
// All tessellation produces a flat float buffer of (x,y) pairs for GL upload.
// Fills  → GL_TRIANGLES  (3 verts per tri, winding = counter-clockwise)
// Strokes → GL_TRIANGLES  (quad-strip expanded to tris)

namespace vtess {

using namespace vmath;

static constexpr int kEllipseSegs = 64;
static constexpr int kRoundCapSegs = 8;
static constexpr int kCurveSteps = 16; // subdivisions per Bézier segment

// ── Contour builder
// ─────────────────────────────────────────────────────────── Flattens a path /
// shape into a list of (closed) contours of Vec2.
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
        cur.push_back(cur.front()); // close loop
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
  if (rx <= 0 && ry <= 0) {
    return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
  }
  rx = min(rx, w * .5f);
  ry = min(ry, h * .5f);
  const int segs = 8;
  Contour c;
  // four rounded corners
  auto arc = [&](float cx, float cy, float startA, float endA) {
    for (int i = 0; i <= segs; i++) {
      float a = startA + (endA - startA) * float(i) / segs;
      c.push_back({cx + cosf(a) * rx, cy + sinf(a) * ry});
    }
  };
  static constexpr float PI = 3.14159265f;
  arc(x + w - rx, y + ry, -PI / 2, 0);
  arc(x + w - rx, y + h - ry, 0, PI / 2);
  arc(x + rx, y + h - ry, PI / 2, PI);
  arc(x + rx, y + ry, PI, 3 * PI / 2);
  return c;
}

// ── Simple ear-clip triangulator (fills) ─────────────────────────────────────
// Handles simple (non-self-intersecting) polygons.  Good enough for most
// vector shapes; complex self-intersecting paths fall back to fan.

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
  // ensure CCW winding
  std::vector<Vec2> poly = pts;
  if (polyArea(poly) < 0)
    std::reverse(poly.begin(), poly.end());
  // remove duplicate last point (closed contours)
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
      return false; // reflex
    // check no other vertex inside triangle
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
      break; // give up — complex polygon
  }
}

// ── Stroke expansion ─────────────────────────────────────────────────────────
// Expands a polyline into a quad-strip triangle list.

inline void expandStroke(const std::vector<Vec2> &pts, bool closed, float hw,
                         LineCapV cap, LineJoinV join,
                         std::vector<float> &out) {
  int n = (int)pts.size();
  if (n < 2)
    return;

  auto pushQuad = [&](Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    // two triangles: abc, acd
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

  // miter join between two consecutive segments
  auto miterDir = [&](Vec2 n1, Vec2 n2) -> Vec2 {
    Vec2 m = {n1.x + n2.x, n1.y + n2.y};
    float denom = dot(m, n1);
    if (std::abs(denom) < 1e-6f)
      return n1;
    float scale = 1.f / denom;
    if (scale > 4.f)
      scale = 4.f;
    return {m.x * scale, m.y * scale};
  };

  // Step 1: compute per-SEGMENT normals
  int numSegs = closed ? n : n - 1;
  std::vector<Vec2> segN(numSegs);
  for (int i = 0; i < numSegs; i++) {
    int j = (i + 1) % n;
    segN[i] = segNormal(pts[i], pts[j]);
  }

  // Step 2: compute per-VERTEX normals by averaging adjacent segment normals
  std::vector<Vec2> normals(n);
  for (int i = 0; i < n; i++) {
    if (!closed) {
      if (i == 0) {
        normals[0] = segN[0];
        continue;
      }
      if (i == n - 1) {
        normals[n - 1] = segN[n - 2];
        continue;
      }
    }
    // Join between segment (i-1) and segment i
    int prevSeg = (i - 1 + numSegs) % numSegs;
    int nextSeg = i % numSegs;
    Vec2 nIn = segN[prevSeg];
    Vec2 nOut = segN[nextSeg];
    if (join == LineJoinV::Miter)
      normals[i] = miterDir(nIn, nOut);
    else
      normals[i] = norm(Vec2{nIn.x + nOut.x, nIn.y + nOut.y});
  }

  // Emit quads — rest unchanged
  for (int i = 0; i < n - 1; i++) {
    Vec2 L0 = pts[i] + normals[i] * hw;
    Vec2 R0 = pts[i] - normals[i] * hw;
    Vec2 L1 = pts[i + 1] + normals[i + 1] * hw;
    Vec2 R1 = pts[i + 1] - normals[i + 1] * hw;
    pushQuad(L0, R0, R1, L1);
  }
  if (closed) {
    Vec2 L0 = pts[n - 1] + normals[n - 1] * hw;
    Vec2 R0 = pts[n - 1] - normals[n - 1] * hw;
    Vec2 L1 = pts[0] + normals[0] * hw;
    Vec2 R1 = pts[0] - normals[0] * hw;
    pushQuad(L0, R0, R1, L1);
  }

  // Line caps (only for open paths)
  if (!closed) {
    auto roundCap = [&](Vec2 center, Vec2 dir, int segs = kRoundCapSegs) {
      // half-disk fan
      Vec2 n0 = perp(dir) * hw;
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
      Vec2 ext = dir * hw;
      Vec2 nor = perp(dir) * hw;
      Vec2 a = center + nor, b = center - nor, c = b + ext, d = a + ext;
      pushQuad(a, b, c, d);
    };

    Vec2 d0 = norm(pts[1] - pts[0]);
    Vec2 d1 = norm(pts[n - 1] - pts[n - 2]);
    if (cap == LineCapV::Round) {
      roundCap(pts[0], Vec2{-d0.x, -d0.y});
      roundCap(pts[n - 1], d1);
    } else if (cap == LineCapV::Square) {
      squareCap(pts[0], Vec2{-d0.x, -d0.y});
      squareCap(pts[n - 1], d1);
    }
    // LineCapV::Butt — no cap needed
  }
}

// ── Shape → triangles
// ─────────────────────────────────────────────────────────

struct ShapeTris {
  std::vector<float> fill;   // (x,y) pairs, GL_TRIANGLES
  std::vector<float> stroke; // (x,y) pairs, GL_TRIANGLES
};

inline vmath::AABB contoursAABB(const vtess::Contours &cs) {
  vmath::AABB bb;
  for (auto &c : cs)
    for (auto &p : c)
      bb.expand(p);
  return bb;
}

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
    // Text geometry is handled separately via GDI texture; no tess needed
    return result;
  }

  // Apply transform to all points
  for (auto &c : contours)
    for (auto &p : c)
      p = s.xform.apply(p);

  // Fill
  if (!s.fill.none) {
    for (auto &c : contours)
      earClip(c, result.fill);
  }

  // Stroke
  if (!s.stroke.none && s.stroke.width > 0) {
    float hw = s.stroke.width * 0.5f;
    for (auto &c : contours) {
      bool closed =
          (s.kind == VShapeKind::Rect || s.kind == VShapeKind::Ellipse ||
           (s.kind == VShapeKind::Poly && s.poly.closed));
      expandStroke(c, closed, hw, s.stroke.cap, s.stroke.join, result.stroke);
    }
  }

  return result;
}

// ── Compute AABB for a shape (world space) ───────────────────────────────────
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

enum class VTool {
  Select,  // click select / drag move / box-select
  Pen,     // click to place anchors, double-click / close to finish
  Node,    // reveal + drag anchors and control points
  Rect,    // drag to create VRect
  Ellipse, // drag to create VEllipse
  Line,    // drag to create open VPoly (2 pts)
};

// ============================================================================
// §V4  UNDO SNAPSHOT
// ============================================================================

struct VSnapshot {
  VScene scene;
  size_t estBytes;
};

// ============================================================================
// §V5  VECTOR SURFACE
// ============================================================================

class VectorSurface : public RenderSurface {
public:
  // ── Undo budget (estimated bytes = kBytesPerPoint * Σ points) ────────────
  static constexpr size_t kDefaultUndoBudget = 64ULL * 1024 * 1024;
  static constexpr size_t kBytesPerPoint = 32; // conservative

  explicit VectorSurface(size_t budget = kDefaultUndoBudget)
      : undoBudget_(budget) {}
  ~VectorSurface() { destroy(); }

  // ── Scene access ─────────────────────────────────────────────────────────
  VScene &scene() { return scene_; }
  const VScene &scene() const { return scene_; }

  // Add a shape, returns its id
  VShapeId addShape(VShape s) {
    s.id = nextId_++;
    pushUndo();
    scene_.push_back(std::move(s));
    dirty_ = true;
    return scene_.back().id;
  }

  // Remove by id
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

  // Modify a shape in-place (caller calls beginEdit(id) before, endEdit()
  // after)
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
    dirty_ = true;
  }

  // ── Tool ──────────────────────────────────────────────────────────────────
  void setTool(VTool t) {
    tool_ = t;
    cancelInProgress();
    dirty_ = true;
  }
  VTool getTool() const { return tool_; }

  // ── Active style (applied to newly created shapes) ────────────────────────
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
    // Invalidate text texture cache — sizes may have changed
    textTexCache_.clear();
  }

  void update(double /*dt*/) override {}

  void render(const float mvp[16]) override {
    glDisable(GL_SCISSOR_TEST);

    // ── 1. Fill entire viewport with pasteboard grey ──────────────────────
    glClearColor(pasteboard_.r, pasteboard_.g, pasteboard_.b, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ── 2. Draw artboard drop-shadow (offset 6px down-right in canvas px) ─
    if (showArtboard_) {
      const float kShadowOff = 6.f;
      const float kShadowBlur = 0.f; // hard shadow via simple quad offset
      std::vector<float> shadowQuad = {
          0.f + kShadowOff,       0.f - kShadowOff,
          float(w_) + kShadowOff, 0.f - kShadowOff,
          float(w_) + kShadowOff, float(h_) - kShadowOff,
          float(w_) + kShadowOff, float(h_) - kShadowOff,
          0.f + kShadowOff,       float(h_) - kShadowOff,
          0.f + kShadowOff,       0.f - kShadowOff,
      };
      drawTris(shadowQuad, shadowColor_, mvp);

      // ── 3. Draw white artboard surface ───────────────────────────────────
      std::vector<float> artQuad = {
          0.f,       0.f,       float(w_), 0.f,       float(w_), float(h_),
          float(w_), float(h_), 0.f,       float(h_), 0.f,       0.f,
      };
      drawTris(artQuad, artboardColor_, mvp);
    }

    // ── 4. Render all shapes ──────────────────────────────────────────────
    for (auto &shape : scene_)
      renderShape(shape, mvp);

    renderGhost(mvp);

    // ── 5. Draw artboard border (thin, 1px, dark grey) ───────────────────
    if (showArtboard_) {
      RGBA borderCol = {0.18f, 0.18f, 0.18f, 1.f};
      std::vector<vmath::Vec2> border = {
          {0.f, 0.f},       {float(w_), 0.f}, {float(w_), float(h_)},
          {0.f, float(h_)}, {0.f, 0.f},
      };
      drawLineLoop(border, borderCol, 1.f, mvp);
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
      onShapeDown(x, y);
      break;
    case VTool::Ellipse:
      onShapeDown(x, y);
      break;
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
      onShapeMove(x, y);
      break;
    case VTool::Ellipse:
      onShapeMove(x, y);
      break;
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
    case VTool::Pen: /* pen commits on click, not release */
      break;
    case VTool::Node:
      onNodeUp();
      break;
    case VTool::Rect:
      onShapeUp(x, y);
      break;
    case VTool::Ellipse:
      onShapeUp(x, y);
      break;
    case VTool::Line:
      onShapeUp(x, y);
      break;
    }
    draggingShape_ = false;
    draggingBox_ = false;
    draggingNode_ = false;
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
      pushUndo(); // single undo entry for batch delete
      selection_.clear();
    }

    // Nudge selected shapes
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
    // Right-click cancels pen, or deselects
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
  RGBA pasteboard_ = {0.314f, 0.314f, 0.314f, 1.f}; // #505050 grey
  RGBA artboardColor_ = {1.f, 1.f, 1.f, 1.f};       // white
  RGBA shadowColor_ = {0.f, 0.f, 0.f, 0.32f};       // drop shadow

  VScene scene_;
  VShapeId nextId_ = 1;

  std::vector<VShapeId> selection_;
  VTool tool_ = VTool::Select;
  VFill activeFill_ = {{0.2f, 0.5f, 1.f, 1.f}, FillRule::NonZero, false};
  VStroke activeStroke_ = {{0.f, 0.f, 0.f, 1.f}, 2.f, LineCapV::Round,
                           LineJoinV::Round,     {},  false};

  // ── Undo ─────────────────────────────────────────────────────────────────
  size_t undoBudget_, undoBytes_ = 0;
  std::vector<VSnapshot> undoStack_, redoStack_;

  // ── Mouse state ───────────────────────────────────────────────────────────
  float mx_ = 0, my_ = 0, mdownX_ = 0, mdownY_ = 0;

  // ── Select tool state ─────────────────────────────────────────────────────
  bool draggingShape_ = false;
  bool draggingBox_ = false;
  float boxX0_ = 0, boxY0_ = 0, boxX1_ = 0, boxY1_ = 0;

  // Saved positions for drag-move (id → original xform)
  std::vector<std::pair<VShapeId, vmath::Mat3>> dragOrigins_;

  // ── Pen tool state ────────────────────────────────────────────────────────
  bool penInProgress_ = false;
  std::vector<vmath::Vec2> penAnchors_; // committed anchors
  std::vector<vmath::Vec2>
      penCPs_; // out-CPs for each anchor (same size as penAnchors_)
  vmath::Vec2 penCursor_{}; // live cursor position for ghost

  // ── Node tool state ───────────────────────────────────────────────────────
  bool draggingNode_ = false;
  int dragNodeIdx_ = -1;      // index into path cmd anchor or poly point
  bool dragNodeIsCP_ = false; // dragging a control point (not anchor)
  int dragNodeCPSide_ = 0;    // 0=in-cp, 1=out-cp

  // ── Shape drag-create state ───────────────────────────────────────────────
  bool shapeDragging_ = false;
  float shapeX0_ = 0, shapeY0_ = 0;
  VShapeId ghostId_ = kNoShape; // shape being previewed

  // ── GL resources ──────────────────────────────────────────────────────────
  GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
  struct ULocs {
    GLint mvp = -1, mode = -1, color = -1, tex = -1, alpha = -1;
  } u_;

  // ── Text texture cache ────────────────────────────────────────────────────
  struct TextTex {
    GLuint tex = 0;
    int w = 0, h = 0;
    float ox = 0, oy = 0;
  };
  std::unordered_map<uint64_t, TextTex> textTexCache_;
  ULONG_PTR gdiplusToken_ = 0;

  // =========================================================================
  // ── Undo helpers ─────────────────────────────────────────────────────────

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

  // Delete without recording undo (used in batch delete)
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
  // ── Shape hit-test ────────────────────────────────────────────────────────
  // Returns id of topmost shape under (x,y), or kNoShape.

  VShapeId hitTest(float x, float y) const {
    vmath::Vec2 pt{x, y};
    // iterate back-to-front (topmost first)
    for (int i = (int)scene_.size() - 1; i >= 0; i--) {
      auto &s = scene_[i];
      vmath::AABB bb = vtess::shapeAABB(s);
      // expand by half stroke width
      float sw = s.stroke.none ? 0.f : s.stroke.width * .5f + 2.f;
      bb.x0 -= sw;
      bb.y0 -= sw;
      bb.x1 += sw;
      bb.y1 += sw;
      if (bb.contains(pt))
        return s.id;
    }
    return kNoShape;
  }

  // Shapes whose AABB intersects a rectangle
  std::vector<VShapeId> hitTestBox(float x0, float y0, float x1,
                                   float y1) const {
    vmath::AABB sel;
    sel.x0 = min(x0, x1);
    sel.y0 = min(y0, y1);
    sel.x1 = max(x0, x1);
    sel.y1 = max(y0, y1);
    std::vector<VShapeId> result;
    for (auto &s : scene_) {
      if (vtess::shapeAABB(s).intersects(sel))
        result.push_back(s.id);
    }
    return result;
  }

  // =========================================================================
  // ── Tool: Select ─────────────────────────────────────────────────────────

  void onSelectDown(float x, float y) {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    VShapeId hit = hitTest(x, y);
    if (hit != kNoShape) {
      if (!shift) {
        if (std::find(selection_.begin(), selection_.end(), hit) ==
            selection_.end())
          selection_.clear();
      }
      select(hit, /*additive=*/shift);
      // save drag origins
      dragOrigins_.clear();
      for (auto id : selection_) {
        auto *s = beginEdit(id);
        if (s)
          dragOrigins_.push_back({id, s->xform});
      }
      draggingShape_ = true;
      draggingBox_ = false;
    } else {
      if (!shift)
        selection_.clear();
      draggingBox_ = true;
      draggingShape_ = false;
      boxX0_ = boxX1_ = x;
      boxY0_ = boxY1_ = y;
    }
  }

  void onSelectMove(float x, float y, float dx, float dy) {
    if (draggingShape_) {
      // Move all selected shapes: apply delta to saved origins
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
    if (draggingShape_ &&
        (std::abs(x - mdownX_) > 1.f || std::abs(y - mdownY_) > 1.f)) {
      // Commit move to undo
      pushUndo();
      // Re-apply final delta (pushUndo took a snapshot before move)
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
        select(id, /*additive=*/true);
      draggingBox_ = false;
    }
    draggingShape_ = false;
    dragOrigins_.clear();
  }

  // =========================================================================
  // ── Tool: Pen ────────────────────────────────────────────────────────────

  void onPenDown(float x, float y) {
    // Double-click (within 4px of first anchor) closes the path
    if (penInProgress_ && !penAnchors_.empty()) {
      float dx = x - penAnchors_.front().x, dy = y - penAnchors_.front().y;
      if (std::sqrt(dx * dx + dy * dy) < 8.f) {
        commitPen(/*closed=*/true);
        return;
      }
    }
    if (!penInProgress_) {
      penInProgress_ = true;
      penAnchors_.clear();
      penCPs_.clear();
    }
    penAnchors_.push_back({x, y});
    penCPs_.push_back({x, y}); // default out-CP = anchor itself
    penCursor_ = {x, y};
  }

  void onPenMove(float x, float y) {
    penCursor_ = {x, y};
    // If Shift held and there's an anchor, drag the out-CP of the last anchor
    if ((GetKeyState(VK_SHIFT) & 0x8000) && !penAnchors_.empty()) {
      penCPs_.back() = {x, y};
    }
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
      vmath::Vec2 cp1 = penCPs_[i - 1];
      vmath::Vec2 cp2 = penAnchors_[i]; // symmetric: in-CP = anchor for now
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

    // Check anchor proximity (8px radius)
    auto checkPt = [&](vmath::Vec2 p, int idx, bool isCP, int cpSide) -> bool {
      vmath::Vec2 wp = s->xform.apply(p);
      float dx = wp.x - x, dy = wp.y - y;
      if (dx * dx + dy * dy < 64.f) {
        dragNodeIdx_ = idx;
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
      int ai = 0;
      for (auto &cmd : s->path.cmds) {
        if (cmd.kind == PathCmdKind::MoveTo ||
            cmd.kind == PathCmdKind::LineTo) {
          if (checkPt(cmd.pt[0], ai, false, 0))
            return;
          ai++;
        } else if (cmd.kind == PathCmdKind::CubicTo) {
          if (checkPt(cmd.pt[0], ai, true, 0))
            return; // cp1
          if (checkPt(cmd.pt[1], ai, true, 1))
            return; // cp2
          if (checkPt(cmd.pt[2], ai, false, 0)) {
            ai++;
            return;
          }
          ai++;
        }
      }
    }

    // No anchor hit — try selecting a different shape
    VShapeId hit = hitTest(x, y);
    if (hit != kNoShape)
      select(hit);
  }

  void onNodeMove(float x, float y, float /*dx*/, float /*dy*/) {
    if (!draggingNode_ || dragNodeIdx_ < 0)
      return;
    if (selection_.empty())
      return;
    VShapeId id = selection_.front();
    auto *s = beginEdit(id);
    if (!s)
      return;

    // Inverse-transform the canvas point back to local space
    // (For identity transform, local = world)
    vmath::Vec2 local{x, y}; // simplified: ignore non-identity transforms

    if (s->kind == VShapeKind::Poly) {
      if (dragNodeIdx_ < (int)s->poly.pts.size())
        s->poly.pts[dragNodeIdx_] = local;
    } else if (s->kind == VShapeKind::Path) {
      int ai = 0;
      for (auto &cmd : s->path.cmds) {
        if (cmd.kind == PathCmdKind::MoveTo ||
            cmd.kind == PathCmdKind::LineTo) {
          if (ai == dragNodeIdx_ && !dragNodeIsCP_)
            cmd.pt[0] = local;
          ai++;
        } else if (cmd.kind == PathCmdKind::CubicTo) {
          if (ai == dragNodeIdx_) {
            if (dragNodeIsCP_)
              cmd.pt[dragNodeCPSide_] = local;
            else
              cmd.pt[2] = local;
          }
          ai++;
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

  void onShapeMove(float x, float y) {
    if (!shapeDragging_)
      return;
    // preview is rendered in renderGhost() using current mx_/my_
    (void)x;
    (void)y;
  }

  void onShapeUp(float x, float y) {
    if (!shapeDragging_)
      return;
    shapeDragging_ = false;
    float x0 = min(shapeX0_, x), y0 = min(shapeY0_, y);
    float x1 = max(shapeX0_, x), y1 = max(shapeY0_, y);
    if (x1 - x0 < 2.f && y1 - y0 < 2.f)
      return; // too small, ignore

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
    case VTool::Line: {
      s.kind = VShapeKind::Poly;
      s.poly.pts = {{shapeX0_, shapeY0_}, {x, y}};
      s.poly.closed = false;
      s.fill.none = true; // lines have no fill
      break;
    }
    default:
      return;
    }

    pushUndo();
    scene_.push_back(std::move(s));
    selection_ = {scene_.back().id};
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
uniform int   uMode; // 0=texture, 1=flat color
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

  // Upload and draw a flat-color triangle list
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

  // Draw a dashed line loop from a list of (x,y) pairs (as thin quads)
  void drawLineLoop(const std::vector<vmath::Vec2> &pts, const RGBA &col,
                    float w, const float mvp[16]) {
    if (pts.size() < 2)
      return;
    std::vector<float> tris;
    auto &p = pts;
    int n = (int)p.size();
    for (int i = 0; i < n; i++) {
      vmath::Vec2 a = p[i], b = p[(i + 1) % n];
      vmath::Vec2 nor = vmath::perp(vmath::norm(b - a)) * (w * .5f);
      tris.push_back(a.x + nor.x);
      tris.push_back(a.y + nor.y);
      tris.push_back(a.x - nor.x);
      tris.push_back(a.y - nor.y);
      tris.push_back(b.x - nor.x);
      tris.push_back(b.y - nor.y);
      tris.push_back(b.x - nor.x);
      tris.push_back(b.y - nor.y);
      tris.push_back(b.x + nor.x);
      tris.push_back(b.y + nor.y);
      tris.push_back(a.x + nor.x);
      tris.push_back(a.y + nor.y);
    }
    drawTris(tris, col, mvp);
  }

  // Draw a small diamond (anchor marker)
  void drawDiamond(float x, float y, float r, const RGBA &col,
                   const float mvp[16]) {
    std::vector<float> v = {
        x, y + r, x + r, y, x, y - r, x, y + r, x, y - r, x - r, y,
    };
    drawTris(v, col, mvp);
  }

  // Draw a small circle (control point marker)
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

  // =========================================================================
  // ── Text texture ─────────────────────────────────────────────────────────

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
    // Rasterize via GDI into a DIB
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
    tt.oy = (float)(fh - by1); // canvas-space origin (Y-up)

    if (bw > 0 && bh > 0) {
      const uint8_t *dib = reinterpret_cast<const uint8_t *>(bits);
      std::vector<uint8_t> rgba(bw * bh * 4);
      for (int row = 0; row < bh; row++) {
        int gdiRow = by0 + (bh - 1 - row);
        for (int col = 0; col < bw; col++) {
          const uint8_t *p = dib + (size_t(gdiRow) * fw + bx0 + col) * 4;
          uint8_t b8 = p[0], g8 = p[1], r8 = p[2];
          uint8_t *d = rgba.data() + (size_t(row) * bw + col) * 4;
          d[0] = r8;
          d[1] = g8;
          d[2] = b8;
          d[3] = (r8 || g8 || b8) ? 255 : 0;
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

  // Blit a text texture as a textured quad
  void blitTextTex(const TextTex &tt, const float mvp[16]) {
    if (!tt.tex || tt.w <= 0 || tt.h <= 0)
      return;
    float x0 = tt.ox, y0 = tt.oy, x1 = x0 + tt.w, y1 = y0 + tt.h;
    float q[] = {
        x0, y0, 0, 0, x1, y0, 1, 0, x1, y1, 1, 1,
        x1, y1, 1, 1, x0, y1, 0, 1, x0, y0, 0, 0,
    };
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

  // =========================================================================
  // ── Render helpers ────────────────────────────────────────────────────────

  void renderShape(const VShape &s, const float mvp[16]) {
    if (s.kind == VShapeKind::Text) {
      auto &tt = const_cast<VectorSurface *>(this)->getTextTex(s);
      blitTextTex(tt, mvp);
      return;
    }
    auto tris = vtess::tessShape(s);
    // Stroke first, fill on top — inner half of stroke hidden by fill.
    // This matches Illustrator's centered-stroke paint order.
    if (!s.stroke.none && !tris.stroke.empty())
      drawTris(tris.stroke, s.stroke.color, mvp);
    if (!s.fill.none && !tris.fill.empty())
      drawTris(tris.fill, s.fill.color, mvp);
  }

  // In-progress ghost for pen / rect / ellipse / line tools
  void renderGhost(const float mvp[16]) {
    RGBA ghostCol = {0.3f, 0.6f, 1.f, 0.7f};
    RGBA ghostStroke = {0.2f, 0.5f, 0.9f, 1.0f};

    // Pen ghost: path so far + line to cursor
    if (tool_ == VTool::Pen && penInProgress_ && !penAnchors_.empty()) {
      std::vector<vmath::Vec2> ghostPts = penAnchors_;
      ghostPts.push_back(penCursor_);
      std::vector<float> strokeTris;
      vtess::expandStroke(ghostPts, false, 1.f, LineCapV::Round,
                          LineJoinV::Round, strokeTris);
      drawTris(strokeTris, ghostStroke, mvp);
      // Draw anchor diamonds
      for (auto &a : penAnchors_)
        drawDiamond(a.x, a.y, 4.f, {1, 1, 1, 0.9f}, mvp);
      // Draw out-CPs for last anchor if Shift was dragged
      if (vmath::len(penCPs_.back() - penAnchors_.back()) > 2.f) {
        drawCircle(penCPs_.back().x, penCPs_.back().y, 4.f, {1, .8f, .2f, 1.f},
                   mvp);
        std::vector<float> cpLine;
        vtess::expandStroke({penAnchors_.back(), penCPs_.back()}, false, 0.8f,
                            LineCapV::Butt, LineJoinV::Miter, cpLine);
        drawTris(cpLine, {1, .8f, .2f, .6f}, mvp);
      }
    }

    // Shape drag ghost
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

    // Box-select rubber-band
    if (draggingBox_) {
      std::vector<vmath::Vec2> box = {{boxX0_, boxY0_},
                                      {boxX1_, boxY0_},
                                      {boxX1_, boxY1_},
                                      {boxX0_, boxY1_}};
      // fill
      std::vector<float> fill;
      vtess::earClip(box, fill);
      drawTris(fill, {0.3f, 0.5f, 1.f, 0.12f}, mvp);
      // outline
      box.push_back(box.front());
      drawLineLoop(box, {0.4f, 0.6f, 1.f, 0.8f}, 1.f, mvp);
    }
  }

  // Selection overlays and node handles
  void renderSelection(const float mvp[16]) {
    if (selection_.empty())
      return;

    for (auto id : selection_) {
      auto it = std::find_if(scene_.begin(), scene_.end(),
                             [id](auto &s) { return s.id == id; });
      if (it == scene_.end())
        continue;
      auto &s = *it;

      // Bounding box
      auto bb = vtess::shapeAABB(s);
      if (bb.valid()) {
        float pad = 4.f;
        std::vector<vmath::Vec2> box = {{bb.x0 - pad, bb.y0 - pad},
                                        {bb.x1 + pad, bb.y0 - pad},
                                        {bb.x1 + pad, bb.y1 + pad},
                                        {bb.x0 - pad, bb.y1 + pad},
                                        {bb.x0 - pad, bb.y0 - pad}};
        drawLineLoop(box, {0.3f, 0.6f, 1.f, 0.9f}, 1.f, mvp);
        // Corner handles
        float hs = 4.f;
        for (auto &c : std::vector<vmath::Vec2>{{bb.x0 - pad, bb.y0 - pad},
                                                {bb.x1 + pad, bb.y0 - pad},
                                                {bb.x1 + pad, bb.y1 + pad},
                                                {bb.x0 - pad, bb.y1 + pad}})
          drawDiamond(c.x, c.y, hs, {1, 1, 1, 1}, mvp);
      }

      // Node handles (only in Node tool)
      if (tool_ != VTool::Node)
        continue;

      RGBA anchorCol = {1, 1, 1, 1};
      RGBA cpCol = {1, .8f, .2f, 1};

      if (s.kind == VShapeKind::Poly) {
        for (auto &p : s.poly.pts) {
          auto wp = s.xform.apply(p);
          drawDiamond(wp.x, wp.y, 5.f, anchorCol, mvp);
        }
      } else if (s.kind == VShapeKind::Path) {
        vmath::Vec2 prev{};
        for (auto &cmd : s.path.cmds) {
          if (cmd.kind == PathCmdKind::MoveTo ||
              cmd.kind == PathCmdKind::LineTo) {
            auto wp = s.xform.apply(cmd.pt[0]);
            drawDiamond(wp.x, wp.y, 5.f, anchorCol, mvp);
            prev = wp;
          } else if (cmd.kind == PathCmdKind::CubicTo) {
            auto cp1 = s.xform.apply(cmd.pt[0]);
            auto cp2 = s.xform.apply(cmd.pt[1]);
            auto end = s.xform.apply(cmd.pt[2]);
            // control-point stems
            std::vector<float> stem1, stem2;
            vtess::expandStroke({prev, cp1}, false, 0.8f, LineCapV::Butt,
                                LineJoinV::Miter, stem1);
            vtess::expandStroke({end, cp2}, false, 0.8f, LineCapV::Butt,
                                LineJoinV::Miter, stem2);
            drawTris(stem1, {cpCol.r, cpCol.g, cpCol.b, .5f}, mvp);
            drawTris(stem2, {cpCol.r, cpCol.g, cpCol.b, .5f}, mvp);
            drawCircle(cp1.x, cp1.y, 4.f, cpCol, mvp);
            drawCircle(cp2.x, cp2.y, 4.f, cpCol, mvp);
            drawDiamond(end.x, end.y, 5.f, anchorCol, mvp);
            prev = end;
          }
        }
      }
    }
  }
};

// ============================================================================
// §V6  FACTORY HELPERS
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