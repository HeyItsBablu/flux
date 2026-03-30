// paint.cpp
#include "flux/flux.hpp"
#include "flux/flux_layers.hpp" 
#include <commdlg.h>
#include <shlobj.h>
#include <unordered_map>
#include <unordered_set>
#pragma comment(lib, "comdlg32.lib")

// ── App-level tool enum ──────────────────────────────────────────────────────
enum class Tool {
  Brush = kToolBrush,   // 0
  Eraser = kToolEraser, // 1
  // Shape tools (values ≥ 2)
  Line = 2,
  Rect = 3,
  Fill = 4,
  Ellipse = 5,
  Triangle = 6,
  RightTriangle = 7,
  Diamond = 8,
  Pentagon = 9,
  Hexagon = 10,
  Star5 = 11,
  Star6 = 12,
  Arrow = 13,
  ArrowDouble = 14,
  Parallelogram = 15,
  Trapezoid = 16,
  RoundRect = 17,
  Cross = 18,
  Heart = 19,
  Select = 20,
  Text = 21,
};

static bool isShapeToolEnum(Tool t) {
  return t != Tool::Brush && t != Tool::Eraser && t != Tool::Fill &&
         t != Tool::Select && t != Tool::Text;
}
static bool isDrawTool(Tool t) { return t == Tool::Brush || t == Tool::Eraser; }

// ── Hex helpers ──────────────────────────────────────────────────────────────
static COLORREF hexToRef(const std::string &css) {
  RGBA c = parseHexColor(css);
  return RGB((BYTE)(c.r * 255), (BYTE)(c.g * 255), (BYTE)(c.b * 255));
}
static std::string refToHex(COLORREF c) { return ColorToHex(c); }

// ── Save dialog ──────────────────────────────────────────────────────────────
static std::wstring promptSavePath(HWND owner) {
  wchar_t buf[MAX_PATH] = L"painting.png";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner   = owner;
  ofn.lpstrFilter = L"PNG Image\0*.png\0All Files\0*.*\0";
  ofn.lpstrFile   = buf;
  ofn.nMaxFile    = MAX_PATH;
  ofn.lpstrDefExt = L"png";
  ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  ofn.lpstrTitle  = L"Save as PNG";
  return GetSaveFileNameW(&ofn) ? buf : L"";
}

// ============================================================================
// §0  TEXT SESSION
// ============================================================================
struct TextSession {
  bool active = false;
  float x = 0, y = 0;
  std::wstring text;
  TextStyle style;
};

// ============================================================================
// §1  GEOMETRY HELPERS
// ============================================================================

static int appendThickLine(float x0, float y0, float x1, float y1, float half,
                           float *out, int base) {
  float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001f) {
    float v[12] = {x0 - half, y0 - half, x0 + half, y0 - half,
                   x0 + half, y0 + half, x0 + half, y0 + half,
                   x0 - half, y0 + half, x0 - half, y0 - half};
    memcpy(out + base * 2, v, sizeof(v));
    return base + 6;
  }
  float nx = -dy / len * half, ny = dx / len * half;
  float v[12] = {x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 - nx, y1 - ny,
                 x1 - nx, y1 - ny, x1 + nx, y1 + ny, x0 + nx, y0 + ny};
  memcpy(out + base * 2, v, sizeof(v));
  return base + 6;
}

static int buildFan(float cx, float cy,
                    const std::vector<std::pair<float, float>> &pts,
                    float *out) {
  int n  = (int)pts.size();
  out[0] = cx;
  out[1] = cy;
  for (int i = 0; i < n; i++) {
    out[2 + i * 2]     = pts[i].first;
    out[3 + i * 2] = pts[i].second;
  }
  out[2 + n * 2] = pts[0].first;
  out[3 + n * 2] = pts[0].second;
  return n + 2;
}

static std::vector<float> nGonOutline(float cx, float cy, float rx, float ry,
                                      int n, float rot, float half) {
  std::vector<float> buf(6 * n * 2);
  int total = 0;
  for (int i = 0; i < n; i++) {
    float a0 = rot + float(i)     / n * 6.2831853f;
    float a1 = rot + float(i + 1) / n * 6.2831853f;
    float x0 = cx + cosf(a0) * rx, y0 = cy + sinf(a0) * ry;
    float x1 = cx + cosf(a1) * rx, y1 = cy + sinf(a1) * ry;
    total = appendThickLine(x0, y0, x1, y1, half, buf.data(), total);
  }
  buf.resize(total * 2);
  return buf;
}

static std::vector<float> nGonFilled(float cx, float cy, float rx, float ry,
                                     int n, float rot) {
  std::vector<std::pair<float, float>> pts;
  for (int i = 0; i < n; i++) {
    float a = rot + float(i) / n * 6.2831853f;
    pts.push_back({cx + cosf(a) * rx, cy + sinf(a) * ry});
  }
  std::vector<float> buf((n + 2) * 2);
  buildFan(cx, cy, pts, buf.data());
  return buf;
}

static std::vector<float> star5Outline(float cx, float cy, float ro, float ri,
                                       float half) {
  std::vector<float> buf(10 * 6 * 2);
  int total = 0;
  float off = -3.14159265f / 2;
  for (int i = 0; i < 10; i++) {
    float a0 = off + float(i)     / 10 * 6.2831853f;
    float a1 = off + float(i + 1) / 10 * 6.2831853f;
    float r0 = (i % 2 == 0) ? ro : ri, r1 = (i % 2 == 1) ? ro : ri;
    total = appendThickLine(cx + cosf(a0) * r0, cy + sinf(a0) * r0,
                            cx + cosf(a1) * r1, cy + sinf(a1) * r1,
                            half, buf.data(), total);
  }
  buf.resize(total * 2);
  return buf;
}

static std::vector<float> star6Outline(float cx, float cy, float ro, float ri,
                                       float half) {
  std::vector<float> buf(12 * 6 * 2);
  int total = 0;
  float off = 0.f;
  for (int i = 0; i < 12; i++) {
    float a0 = off + float(i)     / 12 * 6.2831853f;
    float a1 = off + float(i + 1) / 12 * 6.2831853f;
    float r0 = (i % 2 == 0) ? ro : ri, r1 = (i % 2 == 1) ? ro : ri;
    total = appendThickLine(cx + cosf(a0) * r0, cy + sinf(a0) * r0,
                            cx + cosf(a1) * r1, cy + sinf(a1) * r1,
                            half, buf.data(), total);
  }
  buf.resize(total * 2);
  return buf;
}

static std::vector<float> arrowVerts(float x0, float y0, float x1, float y1,
                                     float half) {
  float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len < 1.f) return {};
  float ux = dx / len, uy = dy / len;
  float px = -uy, py = ux;
  float hs      = half * 1.2f;
  float headLen = min(len * 0.4f, half * 4.f);
  float headW   = half * 2.5f;
  float mx = x1 - ux * headLen, my = y1 - uy * headLen;
  return {x0 + px * hs, y0 + py * hs, x0 - px * hs, y0 - py * hs,
          mx - px * hs, my - py * hs, mx - px * hs, my - py * hs,
          mx + px * hs, my + py * hs, x0 + px * hs, y0 + py * hs,
          x1,           y1,           mx + px * headW, my + py * headW,
          mx - px * headW, my - py * headW};
}

static std::vector<float> doubleArrowVerts(float x0, float y0, float x1,
                                           float y1, float half) {
  float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len < 1.f) return {};
  float ux = dx / len, uy = dy / len;
  float px = -uy, py = ux;
  float hs      = half * 1.2f;
  float headLen = min(len * 0.3f, half * 4.f);
  float headW   = half * 2.5f;
  float m0x = x0 + ux * headLen, m0y = y0 + uy * headLen;
  float m1x = x1 - ux * headLen, m1y = y1 - uy * headLen;
  return {m0x + px * hs,    m0y + py * hs,    m0x - px * hs,    m0y - py * hs,
          m1x - px * hs,    m1y - py * hs,    m1x - px * hs,    m1y - py * hs,
          m1x + px * hs,    m1y + py * hs,    m0x + px * hs,    m0y + py * hs,
          x0,               y0,               m0x + px * headW, m0y + py * headW,
          m0x - px * headW, m0y - py * headW, x1,               y1,
          m1x + px * headW, m1y + py * headW, m1x - px * headW, m1y - py * headW};
}

static std::vector<float> ellipseOutline(float cx, float cy, float rx, float ry,
                                         int segs, float half) {
  std::vector<float> buf(segs * 6 * 2);
  int total = 0;
  for (int i = 0; i < segs; i++) {
    float a0 = float(i)     / segs * 6.2831853f;
    float a1 = float(i + 1) / segs * 6.2831853f;
    total = appendThickLine(cx + cosf(a0) * rx, cy + sinf(a0) * ry,
                            cx + cosf(a1) * rx, cy + sinf(a1) * ry,
                            half, buf.data(), total);
  }
  buf.resize(total * 2);
  return buf;
}

static std::vector<float> crossVerts(float cx, float cy, float rx, float ry,
                                     float arm) {
  float hx = arm, hy = arm;
  std::vector<float> buf = {
      cx - hx, cy - ry, cx + hx, cy - ry, cx + hx, cy - hy, cx + rx, cy - hy,
      cx + rx, cy + hy, cx + hx, cy + hy, cx + hx, cy + ry, cx - hx, cy + ry,
      cx - hx, cy + hy, cx - rx, cy + hy, cx - rx, cy - hy, cx - hx, cy - hy};
  std::vector<float> fan;
  fan.push_back(cx);
  fan.push_back(cy);
  for (int i = 0; i < 12; i++) {
    fan.push_back(buf[i * 2]);
    fan.push_back(buf[i * 2 + 1]);
  }
  fan.push_back(buf[0]);
  fan.push_back(buf[1]);
  return fan;
}

static std::vector<float> heartFan(float cx, float cy, float rx, float ry) {
  const int segs = 64;
  std::vector<std::pair<float, float>> pts;
  for (int i = 0; i < segs; i++) {
    float t  = float(i) / segs * 6.2831853f;
    float hx = rx * (16.f * sinf(t) * sinf(t) * sinf(t)) / 16.f;
    float hy = -ry * (13.f * cosf(t) - 5.f * cosf(2 * t) -
                      2.f * cosf(3 * t) - cosf(4 * t)) / 16.f;
    pts.push_back({cx + hx, cy + hy});
  }
  std::vector<float> buf((segs + 2) * 2);
  buildFan(cx, cy, pts, buf.data());
  return buf;
}

static std::vector<float> parallelogramOutline(float x0, float y0, float x1,
                                               float y1, float shear,
                                               float half) {
  float lx = min(x0, x1), rx = max(x0, x1);
  float by = min(y0, y1), ty = max(y0, y1);
  float sh = (rx - lx) * shear;
  float ax = lx + sh, bx = rx + sh, cx = rx, dx = lx;
  float ay = by, bby = by, ccy = ty, ddy = ty;
  std::vector<float> buf(4 * 6 * 2);
  int total = 0;
  total = appendThickLine(ax, ay,  bx,  bby, half, buf.data(), total);
  total = appendThickLine(bx, bby, cx,  ccy, half, buf.data(), total);
  total = appendThickLine(cx, ccy, dx,  ddy, half, buf.data(), total);
  total = appendThickLine(dx, ddy, ax,  ay,  half, buf.data(), total);
  buf.resize(total * 2);
  return buf;
}

static std::vector<float> trapezoidOutline(float x0, float y0, float x1,
                                           float y1, float taper, float half) {
  float lx = min(x0, x1), rx = max(x0, x1);
  float by = min(y0, y1), ty = max(y0, y1);
  float w = rx - lx, t = w * taper;
  float ax = lx, bx = rx, cx = rx - t, dx = lx + t;
  std::vector<float> buf(4 * 6 * 2);
  int total = 0;
  total = appendThickLine(ax, by, bx, by, half, buf.data(), total);
  total = appendThickLine(bx, by, cx, ty, half, buf.data(), total);
  total = appendThickLine(cx, ty, dx, ty, half, buf.data(), total);
  total = appendThickLine(dx, ty, ax, by, half, buf.data(), total);
  buf.resize(total * 2);
  return buf;
}

// ============================================================================
// §2  FLOOD FILL
// ============================================================================
static void floodFill(uint8_t *pixels, int w, int h, int px, int py,
                      uint8_t tr, uint8_t tg, uint8_t tb,
                      uint8_t fr, uint8_t fg, uint8_t fb) {
  if (px < 0 || py < 0 || px >= w || py >= h) return;
  if (tr == fr && tg == fg && tb == fb)         return;
  auto match = [&](int x, int y) -> bool {
    const uint8_t *p = pixels + (y * w + x) * 4;
    return std::abs((int)p[0] - (int)tr) <= 10 &&
           std::abs((int)p[1] - (int)tg) <= 10 &&
           std::abs((int)p[2] - (int)tb) <= 10;
  };
  if (!match(px, py)) return;
  std::vector<std::pair<int, int>> stack;
  stack.reserve(w * h / 8);
  stack.push_back({px, py});
  while (!stack.empty()) {
    auto [x, y] = stack.back();
    stack.pop_back();
    if (x < 0 || x >= w || y < 0 || y >= h || !match(x, y)) continue;
    uint8_t *p = pixels + (y * w + x) * 4;
    p[0] = fr; p[1] = fg; p[2] = fb; p[3] = 255;
    stack.push_back({x + 1, y});
    stack.push_back({x - 1, y});
    stack.push_back({x, y + 1});
    stack.push_back({x, y - 1});
  }
}

// ============================================================================
// §3  PaintSurface  (extends LayeredSurface)
// ============================================================================
class PaintSurface : public LayeredSurface {
public:
  const std::vector<std::pair<std::string, std::string>> kPalette = {
      {"#cba6f7", "Purple"}, {"#f38ba8", "Pink"},  {"#fab387", "Peach"},
      {"#f9e2af", "Yellow"}, {"#a6e3a1", "Green"}, {"#89dceb", "Sky"},
      {"#89b4fa", "Blue"},   {"#ffffff", "White"}, {"#6c7086", "Gray"},
      {"#1e1e2e", "Black"},
  };

  std::string activeColor   = "#cba6f7";
  float activeSize          = 4.f;
  float activeOpacity       = 1.f;
  Tool  activeTool          = Tool::Brush;
  bool  filled              = false;

  std::function<void()>                          onStateChanged;
  std::function<void(const std::vector<RGBA> &)> onHistoryChanged;
  std::function<void(const std::string &)>       onStatusMessage;
  std::function<void()>                          onRedrawNeeded;

  void setColor(const std::string &col) { activeColor = col; syncStyle(); }
  void setSize(float sz)                { activeSize = sz;   syncStyle(); }
  void setOpacity(float op)             { activeOpacity = op; syncStyle(); }
  void setFilled(bool f)                { filled = f; }

  void setTextStyle(const TextStyle &s)    { pendingTextStyle_ = s; }
  const TextStyle &getTextStyle() const    { return pendingTextStyle_; }

  void setActiveTool(Tool t) {
    commitFloatingSelect();
    commitTextSession();
    selDrawing_ = false;
    hasSelection_ = false;
    grabbed_ = false;
    pixelBuf_.clear();
    textSession_.active = false;
    activeTool = t;
    setTool(isDrawTool(t) ? static_cast<ToolId>(t) : kToolBrush);
    syncStyle();
    shapeDrawing_ = false;
    scratchClear();
    markCompositeDirty();
  }

  void initialize(int w, int h) override {
    LayeredSurface::initialize(w, h);
    syncStyle();
  }

  bool needsContinuousRedraw() const override { return textSession_.active; }

  // ── Mouse ──────────────────────────────────────────────────────────────────
  void onMouseDown(float x, float y) override {
    if (activeTool == Tool::Text)   { textMouseDown(x, y);  return; }
    if (activeTool == Tool::Select) { selectMouseDown(x, y); return; }
    if (activeTool == Tool::Fill)   { doFill(x, y);          return; }
    if (isShapeToolEnum(activeTool)){ shapeStart(x, y);      return; }
    LayeredSurface::onMouseDown(x, y);
  }
  void onMouseMove(float x, float y) override {
    if (activeTool == Tool::Text) {
      if (onCursorChange) onCursorChange(LoadCursor(nullptr, IDC_IBEAM));
      return;
    }
    if (activeTool == Tool::Select) {
      if (onCursorChange) {
        bool inside = hasSelection_ && insideSelection(x, y);
        onCursorChange(LoadCursor(nullptr, inside ? IDC_SIZEALL : IDC_CROSS));
      }
      selectMouseMove(x, y);
      return;
    }
    if (isShapeToolEnum(activeTool)) {
      if (shapeDrawing_) shapePreview(x, y);
      return;
    }
    LayeredSurface::onMouseMove(x, y);
  }
  void onMouseUp(float x, float y) override {
    if (activeTool == Tool::Text)   return;
    if (activeTool == Tool::Select) { selectMouseUp(x, y); return; }
    if (activeTool == Tool::Fill)   return;
    if (isShapeToolEnum(activeTool)) {
      if (shapeDrawing_) shapeCommit(x, y);
      return;
    }
    LayeredSurface::onMouseUp(x, y);
    markCompositeDirty();
    if (onStateChanged)   onStateChanged();
    if (onHistoryChanged) onHistoryChanged(colorHistory());
  }

  void onRightMouseDown(float x, float y) override {
    if (activeTool == Tool::Text) {
      commitTextSession();
      if (onRedrawNeeded) onRedrawNeeded();
      return;
    }
    if (activeTool == Tool::Select) {
      commitFloatingSelect();
      hasSelection_ = false;
      selDrawing_   = false;
      scratchClear();
      markCompositeDirty();
      if (onRedrawNeeded) onRedrawNeeded();
      return;
    }
    LayeredSurface::onRightMouseDown(x, y);
  }

  void onKeyDown(int key) override {
    if (activeTool == Tool::Text && textSession_.active) {
      handleTextKey(key);
      return;
    }
    LayeredSurface::onKeyDown(key);
    switch (key) {
    case 'B': setActiveTool(Tool::Brush);  break;
    case 'E': setActiveTool(Tool::Eraser); break;
    case 'L': setActiveTool(Tool::Line);   break;
    case 'R': setActiveTool(Tool::Rect);   break;
    case 'F': setActiveTool(Tool::Fill);   break;
    case 'S': setActiveTool(Tool::Select); break;
    case 'T': setActiveTool(Tool::Text);   break;
    case Key::Escape:
      if (activeTool == Tool::Select) {
        commitFloatingSelect();
        hasSelection_ = false;
        selDrawing_   = false;
        grabbed_      = false;
        pixelBuf_.clear();
        scratchClear();
        markCompositeDirty();
        if (onRedrawNeeded) onRedrawNeeded();
      }
      break;
    }
    if (onStateChanged) onStateChanged();
  }
  void onKeyUp(int) override {}

  void render(const float mvp[16]) override {
    LayeredSurface::render(mvp);
    redrawSelectionOverlay();
    advanceTextBlink();
  }

  std::function<void(HCURSOR)> onCursorChange;

private:
  // ══════════════════════════════════════════════════════════════════════════
  // TEXT SESSION
  // ══════════════════════════════════════════════════════════════════════════
  TextSession textSession_;
  TextStyle   pendingTextStyle_;

  bool cursorVisible_ = true;
  std::chrono::steady_clock::time_point lastBlink_ =
      std::chrono::steady_clock::now();

  void textMouseDown(float x, float y) {
    if (textSession_.active) commitTextSession();
    textSession_.active = true;
    textSession_.text.clear();
    textSession_.x     = x;
    textSession_.y     = y;
    textSession_.style = pendingTextStyle_;
    cursorVisible_     = true;
    lastBlink_         = std::chrono::steady_clock::now();
    scratchClear();
    renderTextToScratch(textSession_.text, x, y, textSession_.style, true);
    markCompositeDirty();
    if (onRedrawNeeded) onRedrawNeeded();
  }

  void advanceTextBlink() {
    if (!textSession_.active) return;
    auto now     = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - lastBlink_).count();
    if (elapsed >= 0.5) {
      cursorVisible_ = !cursorVisible_;
      lastBlink_     = now;
      scratchClear();
      renderTextToScratch(textSession_.text, textSession_.x, textSession_.y,
                          textSession_.style, cursorVisible_);
      markCompositeDirty();
      if (onRedrawNeeded) onRedrawNeeded();
    }
  }

  void handleTextKey(int vk) {
    const bool ctrl = (GetKeyState(Key::Control) & 0x8000) != 0;
    if (vk == Key::Return) { commitTextSession(); if (onRedrawNeeded) onRedrawNeeded(); return; }
    if (vk == Key::Escape) {
      textSession_.active = false;
      textSession_.text.clear();
      scratchClear();
      markCompositeDirty();
      if (onRedrawNeeded) onRedrawNeeded();
      return;
    }
    if (vk == Key::Backspace) {
      if (!textSession_.text.empty()) textSession_.text.pop_back();
      refreshTextPreview();
      return;
    }
    if (ctrl && vk == 'A') { textSession_.text.clear(); refreshTextPreview(); return; }
    BYTE ks[256] = {};
    GetKeyboardState(ks);
    wchar_t buf[4] = {};
    int n = ToUnicode(vk, MapVirtualKey(vk, MAPVK_VK_TO_VSC), ks, buf, 4, 0);
    if (n == 1 && buf[0] >= 0x20) { textSession_.text += buf[0]; refreshTextPreview(); }
  }

  void refreshTextPreview() {
    cursorVisible_ = true;
    lastBlink_     = std::chrono::steady_clock::now();
    scratchClear();
    renderTextToScratch(textSession_.text, textSession_.x, textSession_.y,
                        textSession_.style, true);
    markCompositeDirty();
    if (onRedrawNeeded) onRedrawNeeded();
  }

  void commitTextSession() {
    if (!textSession_.active) return;
    if (!textSession_.text.empty())
      commitTextToCanvas(textSession_.text, textSession_.x, textSession_.y,
                         textSession_.style);
    textSession_.active = false;
    textSession_.text.clear();
    scratchClear();
    markCompositeDirty();
    if (onStateChanged) onStateChanged();
  }

  // ══════════════════════════════════════════════════════════════════════════
  // SHAPE STATE
  // ══════════════════════════════════════════════════════════════════════════
  bool  shapeDrawing_ = false;
  float shapeX0_ = 0, shapeY0_ = 0;

  void shapeStart(float x, float y) {
    shapeX0_ = x; shapeY0_ = y;
    shapeDrawing_ = true;
    scratchClear();
    markCompositeDirty();
  }
  void shapePreview(float x, float y) {
    scratchClear();
    drawShapeInto(scratchFBOHandle(), shapeX0_, shapeY0_, x, y);
    markCompositeDirty();
    if (onRedrawNeeded) onRedrawNeeded();
  }
  void shapeCommit(float x, float y) {
    shapeDrawing_ = false;
    scratchClear();
    pushUndoSnapshotPublic();
    drawShapeInto(committedFBOHandle(), shapeX0_, shapeY0_, x, y);
    markCompositeDirty();
    if (onStateChanged)   onStateChanged();
    if (onRedrawNeeded)   onRedrawNeeded();
  }

  // ══════════════════════════════════════════════════════════════════════════
  // SELECTION STATE
  // ══════════════════════════════════════════════════════════════════════════
  bool  selDrawing_ = false, hasSelection_ = false;
  float selX0_ = 0, selY0_ = 0, selX1_ = 0, selY1_ = 0;
  bool  moving_ = false, grabbed_ = false;
  float moveAnchorX_ = 0, moveAnchorY_ = 0;
  float moveCurX_    = 0, moveCurY_    = 0;
  std::vector<uint8_t> pixelBuf_;
  int grabW_ = 0, grabH_ = 0, grabLX_ = 0, grabBY_ = 0;

  void selectMouseDown(float x, float y) {
    if (hasSelection_ && insideSelection(x, y)) {
      moving_      = true;
      moveAnchorX_ = x; moveAnchorY_ = y;
      moveCurX_    = x; moveCurY_    = y;
      if (!grabbed_) {
        pushUndoSnapshotPublic();
        grabPixels();
        eraseSource();
        grabbed_ = true;
      }
    } else {
      commitFloatingSelect();
      hasSelection_ = false;
      moving_       = false;
      grabbed_      = false;
      pixelBuf_.clear();
      selDrawing_   = true;
      selX0_ = selX1_ = x;
      selY0_ = selY1_ = y;
      scratchClear();
      markCompositeDirty();
    }
  }

  void selectMouseMove(float x, float y) {
    if (moving_) {
      moveCurX_ = x; moveCurY_ = y;
      float dx = moveCurX_ - moveAnchorX_, dy = moveCurY_ - moveAnchorY_;
      scratchClear();
      blitPixelsToScratch(selX0_ + dx, selY0_ + dy);
      drawDots(selX0_ + dx, selY0_ + dy, selX1_ + dx, selY1_ + dy);
      markCompositeDirty();
      if (onRedrawNeeded) onRedrawNeeded();
    } else if (selDrawing_) {
      selX1_ = x; selY1_ = y;
      scratchClear();
      drawDots(selX0_, selY0_, selX1_, selY1_);
      markCompositeDirty();
      if (onRedrawNeeded) onRedrawNeeded();
    }
  }

  void selectMouseUp(float x, float y) {
    if (moving_) {
      moving_  = false;
      float dx = x - moveAnchorX_, dy = y - moveAnchorY_;
      selX0_ += dx; selX1_ += dx;
      selY0_ += dy; selY1_ += dy;
      scratchClear();
      blitPixelsToScratch(selX0_, selY0_);
      drawDots(selX0_, selY0_, selX1_, selY1_);
      markCompositeDirty();
      if (onRedrawNeeded) onRedrawNeeded();
    } else if (selDrawing_) {
      selDrawing_ = false;
      selX1_ = x; selY1_ = y;
      hasSelection_ = (fabsf(selX1_ - selX0_) > 2.f && fabsf(selY1_ - selY0_) > 2.f);
      if (selX0_ > selX1_) std::swap(selX0_, selX1_);
      if (selY0_ > selY1_) std::swap(selY0_, selY1_);
      scratchClear();
      if (hasSelection_) drawDots(selX0_, selY0_, selX1_, selY1_);
      markCompositeDirty();
      if (onRedrawNeeded) onRedrawNeeded();
    }
  }

  void commitFloatingSelect() {
    if (pixelBuf_.empty()) return;
    if (moving_) {
      float dx = moveCurX_ - moveAnchorX_, dy = moveCurY_ - moveAnchorY_;
      selX0_ += dx; selX1_ += dx;
      selY0_ += dy; selY1_ += dy;
      moving_ = false;
    }
    commitPixels(selX0_, selY0_);
    grabbed_ = false;
    scratchClear();
    markCompositeDirty();
  }

  void redrawSelectionOverlay() {
    if (!hasSelection_ && !selDrawing_) return;
    float x0, y0, x1, y1;
    if (moving_) {
      float dx = moveCurX_ - moveAnchorX_, dy = moveCurY_ - moveAnchorY_;
      x0 = selX0_ + dx; y0 = selY0_ + dy;
      x1 = selX1_ + dx; y1 = selY1_ + dy;
    } else {
      x0 = selX0_; y0 = selY0_;
      x1 = selX1_; y1 = selY1_;
    }
    if (grabbed_ && !moving_) blitPixelsToScratch(x0, y0);
    drawDots(x0, y0, x1, y1);
  }

  bool insideSelection(float x, float y) const {
    return x >= selX0_ && x <= selX1_ && y >= selY0_ && y <= selY1_;
  }

  void grabPixels() {
    int cw = canvasWidth(), ch = canvasHeight();
    grabLX_ = max(0, min((int)selX0_, cw - 1));
    grabBY_ = max(0, min((int)selY0_, ch - 1));
    grabW_  = max(1, min((int)(selX1_ - selX0_), cw - grabLX_));
    grabH_  = max(1, min((int)(selY1_ - selY0_), ch - grabBY_));
    std::vector<uint8_t> full(size_t(cw) * ch * 4);
    readCommitted(full.data());
    pixelBuf_.resize(size_t(grabW_) * grabH_ * 4);
    for (int r = 0; r < grabH_; r++)
      for (int c = 0; c < grabW_; c++) {
        const uint8_t *src = full.data() + ((grabBY_ + r) * cw + grabLX_ + c) * 4;
        uint8_t *dst       = pixelBuf_.data() + (r * grabW_ + c) * 4;
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        dst[3] = (src[0] >= 250 && src[1] >= 250 && src[2] >= 250) ? 0 : src[3];
      }
  }

  void eraseSource() {
    int cw = canvasWidth(), ch = canvasHeight();
    std::vector<uint8_t> full(size_t(cw) * ch * 4);
    readCommitted(full.data());
    for (int row = 0; row < grabH_; row++) {
      uint8_t *p = full.data() + ((grabBY_ + row) * cw + grabLX_) * 4;
      for (int col = 0; col < grabW_; col++) {
        p[col * 4 + 0] = 255; p[col * 4 + 1] = 255;
        p[col * 4 + 2] = 255; p[col * 4 + 3] = 255;
      }
    }
    uploadToCommitted(full.data());
    markCompositeDirty();
  }

  void blitPixelsToScratch(float x0, float y0) {
    if (pixelBuf_.empty()) return;
    int cw = canvasWidth(), ch = canvasHeight();
    int dstLX = (int)x0, dstBY = (int)y0, srcOffX = 0, srcOffY = 0;
    if (dstLX < 0) { srcOffX = -dstLX; dstLX = 0; }
    if (dstBY < 0) { srcOffY = -dstBY; dstBY = 0; }
    int dstW = min(grabW_ - srcOffX, cw - dstLX);
    int dstH = min(grabH_ - srcOffY, ch - dstBY);
    if (dstW <= 0 || dstH <= 0) return;
    std::vector<uint8_t> sub(size_t(dstW) * dstH * 4);
    for (int r = 0; r < dstH; r++)
      memcpy(sub.data() + r * dstW * 4,
             pixelBuf_.data() + ((srcOffY + r) * grabW_ + srcOffX) * 4,
             size_t(dstW) * 4);
    glBindTexture(GL_TEXTURE_2D, scratchTexHandle());
    glTexSubImage2D(GL_TEXTURE_2D, 0, dstLX, dstBY, dstW, dstH,
                    GL_RGBA, GL_UNSIGNED_BYTE, sub.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    markCompositeDirty();
  }

  void commitPixels(float x0, float y0) {
    if (pixelBuf_.empty()) return;
    int cw = canvasWidth(), ch = canvasHeight();
    int dstLX = (int)x0, dstBY = (int)y0, srcOffX = 0, srcOffY = 0;
    if (dstLX < 0) { srcOffX = -dstLX; dstLX = 0; }
    if (dstBY < 0) { srcOffY = -dstBY; dstBY = 0; }
    int dstW = min(grabW_ - srcOffX, cw - dstLX);
    int dstH = min(grabH_ - srcOffY, ch - dstBY);
    if (dstW <= 0 || dstH <= 0) { pixelBuf_.clear(); return; }
    std::vector<uint8_t> full(size_t(cw) * ch * 4);
    readCommitted(full.data());
    for (int r = 0; r < dstH; r++)
      for (int c = 0; c < dstW; c++) {
        const uint8_t *s = pixelBuf_.data() + ((srcOffY + r) * grabW_ + srcOffX + c) * 4;
        uint8_t *d       = full.data() + ((dstBY + r) * cw + dstLX + c) * 4;
        float sa = s[3] / 255.f;
        if (sa <= 0.f) continue;
        if (sa >= 1.f) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
        else {
          float da = d[3] / 255.f, oa = sa + da * (1.f - sa);
          if (oa > 0.f) {
            d[0] = (uint8_t)((s[0] * sa + d[0] * da * (1.f - sa)) / oa);
            d[1] = (uint8_t)((s[1] * sa + d[1] * da * (1.f - sa)) / oa);
            d[2] = (uint8_t)((s[2] * sa + d[2] * da * (1.f - sa)) / oa);
            d[3] = (uint8_t)(oa * 255.f);
          }
        }
      }
    uploadToCommitted(full.data());
    markCompositeDirty();
    pixelBuf_.clear();
  }

  void drawDots(float x0, float y0, float x1, float y1) {
    float lx = min(x0, x1), rx = max(x0, x1);
    float by = min(y0, y1), ty = max(y0, y1);
    const float dotSize = 1.5f, gap = 6.f;
    std::vector<float> buf;
    auto dotEdge = [&](float ax, float ay, float bx, float by2) {
      float dx = bx - ax, dy = by2 - ay, len = sqrtf(dx * dx + dy * dy);
      if (len < 1.f) return;
      float ux = dx / len, uy = dy / len;
      for (float d = 0.f; d < len; d += gap) {
        float cx = ax + ux * d, cy = ay + uy * d;
        buf.insert(buf.end(),
                   {cx - dotSize, cy - dotSize, cx + dotSize, cy - dotSize,
                    cx + dotSize, cy + dotSize, cx + dotSize, cy + dotSize,
                    cx - dotSize, cy + dotSize, cx - dotSize, cy - dotSize});
      }
    };
    dotEdge(lx, by, rx, by);
    dotEdge(rx, by, rx, ty);
    dotEdge(rx, ty, lx, ty);
    dotEdge(lx, ty, lx, by);
    drawVertsToFBO(scratchFBOHandle(), buf.data(), int(buf.size() / 2),
                   GL_TRIANGLES, 0.f, 0.f, 0.f, 1.f);
  }

  // ── Shape drawing ─────────────────────────────────────────────────────────
  void drawShapeInto(GLuint fbo, float x0, float y0, float x1, float y1) {
    RGBA c = parseHexColor(activeColor);
    float r = c.r, g = c.g, b = c.b, a = activeOpacity;
    float half = max(0.5f, activeSize * 0.5f);
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float rx = std::abs(x1 - x0) * 0.5f, ry = std::abs(y1 - y0) * 0.5f;

    auto emit = [&](const std::vector<float> &v, GLenum mode) {
      if (!v.empty())
        drawVertsToFBO(fbo, v.data(), (int)(v.size() / 2), mode, r, g, b, a);
    };
    auto emitOutline = [&](const std::vector<float> &v) { emit(v, GL_TRIANGLES); };
    auto emitFan     = [&](const std::vector<float> &v) { emit(v, GL_TRIANGLE_FAN); };

    auto rectOutline = [&](float lx, float by, float rx2, float ty) {
      float buf[4 * 12]; int total = 0;
      total = appendThickLine(lx, by,  rx2, by,  half, buf, total);
      total = appendThickLine(rx2, by, rx2, ty,  half, buf, total);
      total = appendThickLine(rx2, ty, lx,  ty,  half, buf, total);
      total = appendThickLine(lx,  ty, lx,  by,  half, buf, total);
      drawVertsToFBO(fbo, buf, total, GL_TRIANGLES, r, g, b, a);
    };
    auto rectFilled = [&](float lx, float by, float rx2, float ty) {
      float v[] = {lx, by, rx2, by, rx2, ty, rx2, ty, lx, ty, lx, by};
      drawVertsToFBO(fbo, v, 6, GL_TRIANGLES, r, g, b, a);
    };

    switch (activeTool) {
    case Tool::Line: {
      float v[12];
      int n = appendThickLine(x0, y0, x1, y1, half, v, 0);
      drawVertsToFBO(fbo, v, n, GL_TRIANGLES, r, g, b, a);
      break;
    }
    case Tool::Rect: {
      float lx = min(x0, x1), bby = min(y0, y1), rxx = max(x0, x1), ty = max(y0, y1);
      if (filled) rectFilled(lx, bby, rxx, ty);
      else        rectOutline(lx, bby, rxx, ty);
      break;
    }
    case Tool::Ellipse: {
      if (filled) emitFan(nGonFilled(cx, cy, rx, ry, 64, 0));
      else        emitOutline(ellipseOutline(cx, cy, rx, ry, 64, half));
      break;
    }
    case Tool::Triangle: {
      std::vector<std::pair<float, float>> pts = {
          {cx, cy - ry}, {cx + rx, cy + ry}, {cx - rx, cy + ry}};
      if (filled) {
        auto v = std::vector<float>((3 + 2) * 2);
        buildFan(cx, cy + ry * 0.3f, pts, v.data());
        emitFan(v);
      } else {
        float buf[3 * 12]; int total = 0;
        for (int i = 0; i < 3; i++) {
          int j = (i + 1) % 3;
          total = appendThickLine(pts[i].first, pts[i].second,
                                  pts[j].first, pts[j].second, half, buf, total);
        }
        drawVertsToFBO(fbo, buf, total, GL_TRIANGLES, r, g, b, a);
      }
      break;
    }
    case Tool::RightTriangle: {
      std::vector<std::pair<float, float>> pts = {{x0, y1}, {x1, y1}, {x0, y0}};
      if (filled) {
        float v[(3 + 2) * 2];
        buildFan((x0 + x1) / 3.f, (y0 + y1 * 2) / 3.f, pts, v);
        emitFan(std::vector<float>(v, v + 10));
      } else {
        float buf[3 * 12]; int total = 0;
        for (int i = 0; i < 3; i++) {
          int j = (i + 1) % 3;
          total = appendThickLine(pts[i].first, pts[i].second,
                                  pts[j].first, pts[j].second, half, buf, total);
        }
        drawVertsToFBO(fbo, buf, total, GL_TRIANGLES, r, g, b, a);
      }
      break;
    }
    case Tool::Diamond: {
      std::vector<std::pair<float, float>> pts = {
          {cx, cy - ry}, {cx + rx, cy}, {cx, cy + ry}, {cx - rx, cy}};
      if (filled) {
        auto v = std::vector<float>((4 + 2) * 2);
        buildFan(cx, cy, pts, v.data());
        emitFan(v);
      } else {
        float buf[4 * 12]; int total = 0;
        for (int i = 0; i < 4; i++) {
          int j = (i + 1) % 4;
          total = appendThickLine(pts[i].first, pts[i].second,
                                  pts[j].first, pts[j].second, half, buf, total);
        }
        drawVertsToFBO(fbo, buf, total, GL_TRIANGLES, r, g, b, a);
      }
      break;
    }
    case Tool::Pentagon: {
      if (filled) emitFan(nGonFilled(cx, cy, rx, ry, 5, -1.5708f));
      else        emitOutline(nGonOutline(cx, cy, rx, ry, 5, -1.5708f, half));
      break;
    }
    case Tool::Hexagon: {
      if (filled) emitFan(nGonFilled(cx, cy, rx, ry, 6, 0));
      else        emitOutline(nGonOutline(cx, cy, rx, ry, 6, 0, half));
      break;
    }
    case Tool::Star5: {
      float ro = min(rx, ry), ri = ro * 0.4f;
      if (filled) {
        std::vector<std::pair<float, float>> pts;
        float off = -1.5708f;
        for (int i = 0; i < 10; i++) {
          float aa = off + float(i) / 10 * 6.2831853f;
          float rr = (i % 2 == 0) ? ro : ri;
          pts.push_back({cx + cosf(aa) * rr, cy + sinf(aa) * rr});
        }
        auto v = std::vector<float>((10 + 2) * 2);
        buildFan(cx, cy, pts, v.data());
        emitFan(v);
      } else {
        emitOutline(star5Outline(cx, cy, ro, ri, half));
      }
      break;
    }
    case Tool::Star6: {
      float ro = min(rx, ry), ri = ro * 0.5f;
      if (filled) {
        std::vector<std::pair<float, float>> pts;
        for (int i = 0; i < 12; i++) {
          float aa = float(i) / 12 * 6.2831853f;
          float rr = (i % 2 == 0) ? ro : ri;
          pts.push_back({cx + cosf(aa) * rr, cy + sinf(aa) * rr});
        }
        auto v = std::vector<float>((12 + 2) * 2);
        buildFan(cx, cy, pts, v.data());
        emitFan(v);
      } else {
        emitOutline(star6Outline(cx, cy, ro, ri, half));
      }
      break;
    }
    case Tool::Arrow: {
      auto v = arrowVerts(x0, y0, x1, y1, half);
      if (!v.empty()) drawVertsToFBO(fbo, v.data(), (int)(v.size() / 2), GL_TRIANGLES, r, g, b, a);
      break;
    }
    case Tool::ArrowDouble: {
      auto v = doubleArrowVerts(x0, y0, x1, y1, half);
      if (!v.empty()) drawVertsToFBO(fbo, v.data(), (int)(v.size() / 2), GL_TRIANGLES, r, g, b, a);
      break;
    }
    case Tool::Parallelogram: emitOutline(parallelogramOutline(x0, y0, x1, y1, 0.25f, half)); break;
    case Tool::Trapezoid:     emitOutline(trapezoidOutline(x0, y0, x1, y1, 0.2f,  half)); break;
    case Tool::RoundRect: {
      const int cornSegs = 8;
      float lx = min(x0, x1), bby = min(y0, y1), rxx = max(x0, x1), ty = max(y0, y1);
      float cr = min(min(rx, ry) * 0.3f, 12.f);
      std::vector<std::pair<float, float>> pts;
      auto addCorner = [&](float ox, float oy, float startA) {
        for (int i = 0; i <= cornSegs; i++) {
          float aa = startA + float(i) / cornSegs * 1.5708f;
          pts.push_back({ox + cosf(aa) * cr, oy + sinf(aa) * cr});
        }
      };
      addCorner(rxx - cr, ty  - cr, 0);
      addCorner(lx  + cr, ty  - cr, 1.5708f);
      addCorner(lx  + cr, bby + cr, 3.1416f);
      addCorner(rxx - cr, bby + cr, 4.7124f);
      if (filled) {
        auto v = std::vector<float>((pts.size() + 2) * 2);
        buildFan(cx, cy, pts, v.data());
        emitFan(v);
      } else {
        int n = (int)pts.size(), total = 0;
        std::vector<float> bufv(n * 12);
        for (int i = 0; i < n; i++) {
          int j = (i + 1) % n;
          total = appendThickLine(pts[i].first, pts[i].second,
                                  pts[j].first, pts[j].second, half, bufv.data(), total);
        }
        drawVertsToFBO(fbo, bufv.data(), total, GL_TRIANGLES, r, g, b, a);
      }
      break;
    }
    case Tool::Cross: emitFan(crossVerts(cx, cy, rx, ry, min(rx, ry) * 0.35f)); break;
    case Tool::Heart: emitFan(heartFan(cx, cy, rx, ry));                         break;
    default: break;
    }
  }

  // ── Flood fill ────────────────────────────────────────────────────────────
  void doFill(float cx, float cy) {
    int w = canvasWidth(), h = canvasHeight();
    int px = int(std::floor(cx)), py = int(std::floor(cy));
    if (px < 0 || py < 0 || px >= w || py >= h) return;
    std::vector<uint8_t> buf(size_t(w) * h * 4);
    readCommitted(buf.data());
    uint8_t *seed = buf.data() + (py * w + px) * 4;
    uint8_t tr = seed[0], tg = seed[1], tb = seed[2];
    RGBA fc = parseHexColor(activeColor);
    pushUndoSnapshotPublic();
    floodFill(buf.data(), w, h, px, py, tr, tg, tb,
              uint8_t(fc.r * 255), uint8_t(fc.g * 255), uint8_t(fc.b * 255));
    uploadToCommitted(buf.data());
    markCompositeDirty();
    if (onStateChanged)   onStateChanged();
    if (onRedrawNeeded)   onRedrawNeeded();
  }

  // ── Style sync ────────────────────────────────────────────────────────────
  void syncStyle() {
    RGBA c = parseHexColor(activeColor);
    StrokeStyle s;
    s.r       = c.r; s.g = c.g; s.b = c.b; s.a = 1.f;
    s.radius  = activeSize * 0.5f;
    s.opacity = activeOpacity;
    s.tool    = kToolBrush;
    setStrokeStyle(s);
    LayeredSurface::setOpacity(activeOpacity);
    pendingTextStyle_.r = c.r;
    pendingTextStyle_.g = c.g;
    pendingTextStyle_.b = c.b;
  }
};

// ============================================================================
// §4  PaintApp
// ============================================================================
class PaintApp : public Component {
  State<std::string>         activeColor;
  State<double>              activeSize;
  State<double>              activeOpacity;
  State<Tool>                activeTool;
  State<bool>                filledShapes;
  State<bool>                canUndo, canRedo;
  State<double>              zoomLevel;
  State<std::vector<RGBA>>   colorHistory;
  State<std::string>         statusMsg;
  State<int>                 fontSize;

  // ── Layer state ───────────────────────────────────────────────────────────
  using LayerRef = ReactiveItemPtr<LayerDesc>;
  State<std::vector<LayerRef>> layerDescs;
  std::unordered_map<std::string, LayerRef> layerRefByName_;

  std::shared_ptr<PaintSurface> surface_;
  CanvasWidget *canvasPtr_ = nullptr;
  HWND          mainHwnd_  = nullptr;

  static constexpr double kZoomMin = 6.25, kZoomMax = 200.0;
  static constexpr int kSidebarWidth    = 268;
  static constexpr int kToolbarHeight   = 48;
  static constexpr int kHintsHeight     = 24;
  static constexpr int kAppBarHeight    = 32;
  static constexpr int kLayerPanelWidth = 192;

  void refreshUndoRedo() {
    if (!surface_) return;
    canUndo.set(surface_->canUndo());
    canRedo.set(surface_->canRedo());
  }
  void syncZoomState(float z) {
    double pct = z * 100.0;
    if (std::abs(pct - zoomLevel.get()) > 0.5) zoomLevel.set(pct);
  }
  void applyZoomFromSlider(double pct) {
    if (!canvasPtr_) return;
    Viewport &vp = canvasPtr_->viewport();
    float t = float(pct / 100.0), cur = vp.zoom();
    if (std::abs(t - cur) < 0.0001f) return;
    vp.zoomToward(vp.viewW() * .5f, vp.viewH() * .5f, t / cur);
    canvasPtr_->redraw();
  }
  void doSavePNG() {
    if (!surface_ || !canvasPtr_) return;
    std::wstring path = promptSavePath(mainHwnd_);
    if (path.empty()) return;
    if (!surface_->savePNG(path))
      MessageBoxW(mainHwnd_, L"Failed to save PNG.", L"Error", MB_ICONERROR | MB_OK);
  }

public:
  PaintApp()
      : activeColor("#cba6f7", context),
        activeSize(4.0, context),
        activeOpacity(1.0, context),
        activeTool(Tool::Brush, context),
        filledShapes(false, context),
        canUndo(false, context),
        canRedo(false, context),
        zoomLevel(100.0, context),
        colorHistory({}, context),
        statusMsg("", context),
        fontSize(20, context),
        layerDescs({}, context) {}

  WidgetPtr build() override {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int viewW   = screenW - kSidebarWidth - kLayerPanelWidth;
    int viewH   = screenH - kAppBarHeight - kToolbarHeight - kHintsHeight;
    static constexpr int kPaperW = 1240, kPaperH = 1754;

    auto canvas  = LayeredCanvas(viewW, viewH, kPaperW, kPaperH);
    surface_     = canvas->setSurface<PaintSurface>();
    canvasPtr_   = canvas.get();
    mainHwnd_    = GetActiveWindow();

    surface_->onStateChanged   = [this]() { refreshUndoRedo(); };
    surface_->onHistoryChanged = [this](const std::vector<RGBA> &h) { colorHistory.set(h); };
    surface_->onStatusMessage  = [this](const std::string &m) { statusMsg.set(m); };
    surface_->onRedrawNeeded   = [this]() { if (canvasPtr_) canvasPtr_->redraw(); };
    surface_->onCursorChange   = [](HCURSOR c) { SetCursor(c); };
    canvas->onViewportChanged  = [this](float z) { syncZoomState(z); };

    // ── Layer change handler ───────────────────────────────────────────────
    surface_->onLayersChanged = [this](const LayerState &ls) {
      int count = int(ls.layers.size());
      std::vector<LayerDesc> ordered;
      ordered.reserve(count);
      for (int i = count - 1; i >= 0; i--)
        ordered.push_back(ls.layers[i]);

      std::unordered_set<std::string> activeNames;
      for (auto &ld : ordered) activeNames.insert(ld.name);
      for (auto it = layerRefByName_.begin(); it != layerRefByName_.end();) {
        if (!activeNames.count(it->first)) it = layerRefByName_.erase(it);
        else                               ++it;
      }

      bool structuralChange = false;
      std::vector<LayerRef> newVec;
      newVec.reserve(count);
      for (auto &ld : ordered) {
        auto it = layerRefByName_.find(ld.name);
        if (it != layerRefByName_.end()) {
          it->second->set(ld);
          newVec.push_back(it->second);
        } else {
          auto ref = MakeReactive(ld);
          layerRefByName_[ld.name] = ref;
          newVec.push_back(ref);
          structuralChange = true;
        }
      }

      bool orderChanged = false;
      {
        const auto &cur = layerDescs.get();
        if (cur.size() != newVec.size()) {
          orderChanged = true;
        } else {
          for (size_t i = 0; i < cur.size(); i++) {
            if (cur[i].get() != newVec[i].get()) { orderChanged = true; break; }
          }
        }
      }
      if (structuralChange || int(layerDescs.get().size()) != count || orderChanged)
        layerDescs.set(newVec);
      if (canvasPtr_) canvasPtr_->redraw();
    };

    activeColor.listen([this](const std::string &col) { if (surface_) surface_->setColor(col); });
    activeSize.listen([this](double sz)               { if (surface_) surface_->setSize(float(sz)); });
    activeOpacity.listen([this](double op)            { if (surface_) surface_->setOpacity(float(op)); });
    activeTool.listen([this](Tool t)                  { if (surface_) surface_->setActiveTool(t); });
    filledShapes.listen([this](bool f)                { if (surface_) surface_->setFilled(f); });
    zoomLevel.listen([this](double pct)               { applyZoomFromSlider(pct); });
    fontSize.listen([this](int sz) {
      if (!surface_) return;
      TextStyle s = surface_->getTextStyle();
      s.fontSize  = sz;
      surface_->setTextStyle(s);
    });

    // ── Shared style helpers ───────────────────────────────────────────────
    auto sectionLabel = [](const std::string &txt) -> WidgetPtr {
      return Text(txt)
          ->setFontSize(9)
          ->setTextColor(RGB(100, 105, 125))
          ->setFontWeight(FontWeight::Bold);
    };

    COLORREF kAccent  = RGB(174, 129, 255);
    COLORREF kBg1     = RGB(17,  17,  27);
    COLORREF kBg2     = RGB(24,  24,  37);
    COLORREF kBorder  = RGB(49,  50,  68);
    COLORREF kText    = RGB(205, 214, 244);
    COLORREF kDim     = RGB(100, 105, 130);
    COLORREF kSubtext = RGB(130, 132, 155);
    COLORREF kCard    = RGB(30,  30,  46);

    auto cardWrap = [&](WidgetPtr child) -> WidgetPtr {
      return Container(child)
          ->setBackgroundColor(kBg2)
          ->setBorderRadius(8)
          ->setBorderWidth(1)
          ->setBorderColor(kBorder)
          ->setPaddingAll(10, 10, 10, 10);
    };

    // ── Tool button factory ────────────────────────────────────────────────
    auto makeToolBtn = [&](Tool tv, const std::string &icon,
                           const std::string &lbl) -> WidgetPtr {
      return GestureDetector(
                 Container(
                     Center(
                         Column({
                             Text(icon)
                                 ->setFontSize(16)
                                 ->setTextColor(activeTool, [tv](const Tool &at) {
                                   return at == tv ? RGB(203, 166, 247) : RGB(140, 140, 160);
                                 }),
                         })
                         ->setSpacing(0)
                         ->setCrossAxisAlignment(CrossAxisAlignment::Center)))
                     ->setWidth(44)->setHeight(32)->setBorderRadius(5)
                     ->setBackgroundColor(activeTool, [tv](const Tool &at) {
                       return at == tv ? RGB(40, 35, 58) : RGB(24, 24, 37);
                     })
                     ->setBorderWidth(activeTool, [tv](const Tool &at) {
                       return at == tv ? 2 : 1;
                     })
                     ->setBorderColor(activeTool, [tv](const Tool &at) {
                       return at == tv ? RGB(147, 112, 219) : RGB(49, 50, 68);
                     }))
          ->setOnTap([this, tv]() { activeTool.set(tv); });
    };

    // ── §A  DRAW TOOLS ─────────────────────────────────────────────────────
    auto drawSection = cardWrap(
        Column({
            sectionLabel("DRAW"),
            SizedBox(0, 8),
            Row({
                makeToolBtn(Tool::Brush,  "🖌", "Brush"),
                makeToolBtn(Tool::Eraser, "⬜", "Eraser"),
                makeToolBtn(Tool::Fill,   "🪣", "Fill"),
                makeToolBtn(Tool::Select, "⬚", "Select"),
            })
            ->setSpacing(4),
            SizedBox(0, 4),
            Row({ makeToolBtn(Tool::Text, "𝐓", "Text") })->setSpacing(4),
        })
        ->setSpacing(0));

    // ── §B  SHAPE TOOLS ────────────────────────────────────────────────────
    auto fillToggle =
        GestureDetector(
            Container(
                Row({
                    Container(nullptr)
                        ->setWidth(14)->setHeight(14)->setBorderRadius(3)
                        ->setBackgroundColor(filledShapes, [](const bool &f) {
                          return f ? RGB(174, 129, 255) : RGB(17, 17, 27);
                        })
                        ->setBorderWidth(1)
                        ->setBorderColor(RGB(147, 112, 219)),
                    SizedBox(6, 0),
                    Text("Fill shapes")->setFontSize(10)->setTextColor(RGB(160, 165, 190)),
                })
                ->setSpacing(0)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setPaddingAll(6, 4, 6, 4)
            ->setBorderRadius(5)
            ->setBackgroundColor(RGB(24, 24, 37))
            ->setBorderWidth(1)
            ->setBorderColor(kBorder))
        ->setOnTap([this]() { filledShapes.set(!filledShapes.get()); });

    auto shapeSection = cardWrap(
        Column({
            sectionLabel("SHAPES"),
            SizedBox(0, 8),
            Row({
                makeToolBtn(Tool::Line,     "╱",  "Line"),
                makeToolBtn(Tool::Rect,     "▭",  "Rect"),
                makeToolBtn(Tool::RoundRect,"▢",  "RndRect"),
                makeToolBtn(Tool::Ellipse,  "⬭",  "Ellipse"),
            })->setSpacing(4),
            SizedBox(0, 4),
            Row({
                makeToolBtn(Tool::Triangle,      "△", "Triangle"),
                makeToolBtn(Tool::RightTriangle,  "◺", "R.Tri"),
                makeToolBtn(Tool::Diamond,        "◇", "Diamond"),
                makeToolBtn(Tool::Pentagon,       "⬠", "Pentagon"),
            })->setSpacing(4),
            SizedBox(0, 4),
            Row({
                makeToolBtn(Tool::Hexagon, "⬡", "Hexagon"),
                makeToolBtn(Tool::Star5,   "★", "Star 5"),
                makeToolBtn(Tool::Star6,   "✡", "Star 6"),
                makeToolBtn(Tool::Cross,   "✛", "Cross"),
            })->setSpacing(4),
            SizedBox(0, 4),
            Row({
                makeToolBtn(Tool::Arrow,         "→", "Arrow"),
                makeToolBtn(Tool::ArrowDouble,   "↔", "Dbl Arrow"),
                makeToolBtn(Tool::Parallelogram, "▱", "Parallel."),
                makeToolBtn(Tool::Trapezoid,     "⏢", "Trapezoid"),
            })->setSpacing(4),
            SizedBox(0, 4),
            Row({ makeToolBtn(Tool::Heart, "♥", "Heart") })->setSpacing(4),
            SizedBox(0, 8),
            fillToggle,
        })
        ->setSpacing(0));

    // ── §B2  FONT SIZE ─────────────────────────────────────────────────────
    auto fontSizeSection = cardWrap(
        Row({
            sectionLabel("FONT SIZE"),
            SizedBox(6, 0),
            Row({
                GestureDetector(
                    Container(Text("–")->setFontSize(14)->setTextColor(RGB(180, 180, 200)))
                        ->setWidth(18)->setHeight(18)->setBorderRadius(4)
                        ->setBackgroundColor(RGB(35, 35, 55))
                        ->setBorderWidth(1)->setBorderColor(kBorder))
                    ->setOnTap([this]() { fontSize.set(max(8, fontSize.get() - 2)); }),
                SizedBox(4, 0),
                Container(
                    Text(fontSize, [](const int &sz) { return std::to_string(sz); })
                        ->setFontSize(10)->setTextColor(RGB(200, 200, 220)))
                    ->setWidth(26)->setHeight(18)->setBorderRadius(4)
                    ->setBackgroundColor(RGB(22, 22, 38))
                    ->setBorderWidth(1)->setBorderColor(kBorder),
                SizedBox(4, 0),
                GestureDetector(
                    Container(Text("+")->setFontSize(14)->setTextColor(RGB(180, 180, 200)))
                        ->setWidth(18)->setHeight(18)->setBorderRadius(4)
                        ->setBackgroundColor(RGB(35, 35, 55))
                        ->setBorderWidth(1)->setBorderColor(kBorder))
                    ->setOnTap([this]() { fontSize.set(min(96, fontSize.get() + 2)); }),
            })
            ->setSpacing(0),
        })
        ->setCrossAxisAlignment(CrossAxisAlignment::Center)
        ->setSpacing(0));

    // ── Color picker ───────────────────────────────────────────────────────
    auto picker =
        ColorPicker(hexToRef(activeColor.get()))
            ->setShowAlpha(false)
            ->setOnColorChanged([this](COLORREF c) { activeColor.set(refToHex(c)); });
    auto pickerSection = cardWrap(picker);

    // ── Color history ──────────────────────────────────────────────────────
    auto historyList =
        ListView(colorHistory)
            ->itemBuilder([this](int, const RGBA &c) -> WidgetPtr {
              char hex[8];
              _snprintf_s(hex, sizeof(hex), _TRUNCATE, "#%02x%02x%02x",
                          int(c.r * 255), int(c.g * 255), int(c.b * 255));
              std::string col = hex;
              return GestureDetector(
                         Container(nullptr)
                             ->setWidth(20)->setHeight(20)->setBorderRadius(10)
                             ->setBackgroundColor(hexToRef(col))
                             ->setBorderWidth(1)->setBorderColor(RGB(80, 80, 100)))
                  ->setOnTap([this, col]() { activeColor.set(col); });
            })
            ->setHorizontal(true)
            ->setSpacing(4)
            ->setScrollbarSize(3)
            ->setScrollbarColor(RGB(60, 62, 80))
            ->setScrollbarHoverColor(RGB(100, 102, 120));

    // ── Palette swatches ───────────────────────────────────────────────────
    auto pc1 = std::make_shared<RowWidget>(); pc1->setSpacing(4);
    auto pc2 = std::make_shared<RowWidget>(); pc2->setSpacing(4);
    for (int i = 0; i < (int)surface_->kPalette.size(); i++) {
      const auto &[col, lbl] = surface_->kPalette[i];
      std::string c = col;
      auto sw = GestureDetector(
                    Container(nullptr)
                        ->setWidth(20)->setHeight(20)->setBorderRadius(10)
                        ->setBackgroundColor(hexToRef(col))
                        ->setBorderWidth(activeColor, [c](const std::string &a) {
                          return a == c ? 2 : 0;
                        })
                        ->setBorderColor(activeColor, [c](const std::string &a) {
                          return a == c ? RGB(255, 255, 255) : RGB(0, 0, 0);
                        }))
          ->setOnTap([this, c]() { activeColor.set(c); });
      if (i % 2 == 0) pc1->addChild(sw);
      else             pc2->addChild(sw);
    }
    auto paletteSection = cardWrap(
        Column({
            sectionLabel("PRESETS"),
            SizedBox(0, 8),
            Row({ pc1, pc2 })->setSpacing(0),
        })
        ->setSpacing(0));

    // ── Size / Opacity sliders ─────────────────────────────────────────────
    auto sizeSection = cardWrap(
        Column({
            Row({
                sectionLabel("SIZE"),
                SizedBox(6, 0),
                Text(activeSize, [](double v) {
                        return std::to_string(int(std::round(v))) + "px";
                    })
                    ->setFontSize(9)->setTextColor(RGB(160, 160, 180)),
            })
            ->setSpacing(0)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center),
            SizedBox(0, 8),
            Slider(1.0, 40.0, 0.5)
                ->setValue(activeSize)
                ->setTrackFillColor(kAccent)
                ->setWidth(224),
        })
        ->setSpacing(0));

    auto opacitySection = cardWrap(
        Column({
            Row({
                sectionLabel("OPACITY"),
                SizedBox(6, 0),
                Text(activeOpacity, [](double op) {
                        return std::to_string(int(std::round(op * 100))) + "%";
                    })
                    ->setFontSize(9)->setTextColor(RGB(160, 160, 180)),
            })
            ->setSpacing(0)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center),
            SizedBox(0, 8),
            Slider(0.0, 1.0, 0.01)
                ->setValue(activeOpacity)
                ->setTrackFillColor(kAccent)
                ->setWidth(224),
        })
        ->setSpacing(0));

    // ── §D  Sidebar assembly ───────────────────────────────────────────────
    auto sidebar =
        Container(
            Column({
                drawSection,
                SizedBox(0, 6),
                shapeSection,
                SizedBox(0, 6),
                pickerSection,
                SizedBox(0, 6),
                paletteSection,
                SizedBox(0, 6),
                sizeSection,
                SizedBox(0, 6),
                opacitySection,
            })
            ->setSpacing(0))
        ->setWidth(kSidebarWidth)
        ->setBackgroundColor(kBg1)
        ->setPaddingAll(10, 10, 10, 10);

    // ── §E  Toolbar ────────────────────────────────────────────────────────
    auto makeBtn = [&](const std::string &lbl, COLORREF txt,
                       std::function<void()> fn) -> WidgetPtr {
      return Button(lbl, fn)
          ->setBackgroundColor(RGB(30, 30, 46))
          ->setTextColor(txt)
          ->setBorderRadius(5)
          ->setHeight(26)
          ->setPadding(4);
    };

    auto toolbar =
        Container(
            Row({
                Text("Zoom")->setFontSize(11)->setTextColor(RGB(140, 140, 160)),
                Slider(kZoomMin, kZoomMax, 0.25)
                    ->setValue(zoomLevel)
                    ->setTrackFillColor(kAccent)
                    ->setWidth(110),
                Text(zoomLevel, [](double v) {
                        return std::to_string(int(std::round(v))) + "%";
                    })
                    ->setFontSize(11)->setTextColor(RGB(180, 180, 200))->setMinWidth(38),
                fontSizeSection,
                Button("1:1", [this] {
                        if (canvasPtr_) {
                          canvasPtr_->viewport().resetZoom();
                          canvasPtr_->redraw();
                          syncZoomState(1.f);
                        }
                    })
                    ->setBackgroundColor(RGB(24, 24, 37))->setTextColor(kAccent)
                    ->setBorderRadius(5)->setWidth(30)->setHeight(26)->setPadding(4),
                Button("Fit", [this] {
                        if (canvasPtr_) {
                          canvasPtr_->viewport().fitToView();
                          canvasPtr_->redraw();
                          syncZoomState(canvasPtr_->viewport().zoom());
                        }
                    })
                    ->setBackgroundColor(RGB(24, 24, 37))->setTextColor(kAccent)
                    ->setBorderRadius(5)->setWidth(30)->setHeight(26)->setPadding(4),
                SizedBox(12, 0),
                makeBtn("↩ Undo", RGB(137, 180, 250), [this] {
                    if (surface_) { surface_->undo(); refreshUndoRedo(); if (canvasPtr_) canvasPtr_->redraw(); }
                }),
                makeBtn("Redo ↪", RGB(166, 226, 46), [this] {
                    if (surface_) { surface_->redo(); refreshUndoRedo(); if (canvasPtr_) canvasPtr_->redraw(); }
                }),
                makeBtn("✕ Clear", RGB(243, 139, 168), [this] {
                    if (surface_) { surface_->clear(); refreshUndoRedo(); if (canvasPtr_) canvasPtr_->redraw(); }
                }),
                makeBtn("💾 Save", RGB(148, 226, 213), [this] { doSavePNG(); }),
            })
            ->setSpacing(8)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center))
        ->setBackgroundColor(kBg1)
        ->setPaddingAll(10, 7, 10, 7)
        ->setHeight(kToolbarHeight);

    // ── §F  Context menu ───────────────────────────────────────────────────
    auto canvasWithMenu = ContextMenu(
        canvas,
        {
            {"Brush (B)",     [this] { activeTool.set(Tool::Brush); }},
            {"Eraser (E)",    [this] { activeTool.set(Tool::Eraser); }},
            {"Fill (F)",      [this] { activeTool.set(Tool::Fill); }},
            {"Select (S)",    [this] { activeTool.set(Tool::Select); }},
            {"Text (T)",      [this] { activeTool.set(Tool::Text); }},
            ContextMenuItem::Separator(),
            {"Line (L)",      [this] { activeTool.set(Tool::Line); }},
            {"Rectangle (R)", [this] { activeTool.set(Tool::Rect); }},
            {"Ellipse",       [this] { activeTool.set(Tool::Ellipse); }},
            {"Triangle",      [this] { activeTool.set(Tool::Triangle); }},
            {"Diamond",       [this] { activeTool.set(Tool::Diamond); }},
            {"Star",          [this] { activeTool.set(Tool::Star5); }},
            {"Arrow",         [this] { activeTool.set(Tool::Arrow); }},
            ContextMenuItem::Separator(),
            {"Undo", [this] {
               if (surface_) { surface_->undo(); refreshUndoRedo(); if (canvasPtr_) canvasPtr_->redraw(); }
            }},
            {"Redo", [this] {
               if (surface_) { surface_->redo(); refreshUndoRedo(); if (canvasPtr_) canvasPtr_->redraw(); }
            }},
            ContextMenuItem::Separator(),
            {"Zoom 1:1", [this] {
               if (canvasPtr_) { canvasPtr_->viewport().resetZoom(); canvasPtr_->redraw(); syncZoomState(1.f); }
            }},
            {"Fit to view", [this] {
               if (canvasPtr_) { canvasPtr_->viewport().fitToView(); canvasPtr_->redraw(); syncZoomState(canvasPtr_->viewport().zoom()); }
            }},
            ContextMenuItem::Separator(),
            {"Save as PNG", [this] { doSavePNG(); }},
            {"Clear", [this] {
               if (surface_) { surface_->clear(); refreshUndoRedo(); if (canvasPtr_) canvasPtr_->redraw(); }
            }},
        });

    // ── §G  Hints strip ────────────────────────────────────────────────────
    auto hints =
        Container(
            Conditional(statusMsg, [](const std::string &s) { return !s.empty(); })
                ->Then([&] {
                  return Text(statusMsg, [](const std::string &s) { return s; })
                      ->setFontSize(10)
                      ->setTextColor(RGB(148, 226, 213));
                })
                ->Else([&] {
                  return Row({
                      Text("MMB/Space: Pan")      ->setFontSize(10)->setTextColor(RGB(75, 75, 95)),
                      SizedBox(10, 0),
                      Text("Ctrl+Scroll: Zoom")   ->setFontSize(10)->setTextColor(RGB(75, 75, 95)),
                      SizedBox(10, 0),
                      Text("Ctrl+Z/Y: Undo/Redo") ->setFontSize(10)->setTextColor(RGB(75, 75, 95)),
                      SizedBox(10, 0),
                      Text("B E L R F S T: Tools  |  T=Text, click to place, type, Enter=commit, Esc=cancel")
                          ->setFontSize(10)->setTextColor(RGB(75, 75, 95)),
                  })
                  ->setSpacing(0);
                }))
        ->setBackgroundColor(kBg1)
        ->setPaddingAll(10, 3, 10, 3)
        ->setHeight(kHintsHeight);

    // ── §H  Layer panel ────────────────────────────────────────────────────
    auto layerList =
        ListView(layerDescs)
            ->itemBuilder([this, kCard, kBorder, kAccent, kDim,
                           kSubtext](int di, const LayerRef &ri) -> WidgetPtr {
              const LayerDesc &ld  = ri->get();
              const bool       act = ld.isActive;
              const COLORREF rowBg   = act ? RGB(38, 30, 58) : kCard;
              const COLORREF rowBord = act ? kAccent : kBorder;
              const int      bw      = act ? 2 : 1;

              auto liveStackIndex = [this](int displayIdx) -> int {
                const auto &snap = layerDescs.get();
                if (displayIdx >= int(snap.size())) return -1;
                return int(snap.size()) - 1 - displayIdx;
              };

              auto eye = Tooltip(
                  GestureDetector(
                      Container(
                          Text(ld.visible ? "👁" : "🚫")
                              ->setFontSize(11)
                              ->setTextColor(ld.visible ? RGB(180, 180, 210) : RGB(80, 80, 100)))
                          ->setWidth(24)->setHeight(24)->setBorderRadius(4)
                          ->setBackgroundColor(ld.visible ? RGB(30, 30, 48) : RGB(22, 22, 32))
                          ->setBorderWidth(1)->setBorderColor(kBorder))
                      ->setOnTap([this, di, liveStackIndex]() {
                        if (!surface_) return;
                        int li = liveStackIndex(di);
                        if (li < 0) return;
                        const auto &snap = layerDescs.get();
                        surface_->setLayerVisible(li, !snap[di]->get().visible);
                      }),
                  ld.visible ? "Hide layer" : "Show layer");

              auto name =
                  GestureDetector(
                      Text(ld.name)
                          ->setFontSize(10)
                          ->setTextColor(act ? RGB(220, 200, 255) : kSubtext)
                          ->setMinWidth(56))
                      ->setOnTap([this, di, liveStackIndex]() {
                        if (!surface_) return;
                        int li = liveStackIndex(di);
                        if (li < 0) return;
                        surface_->setActiveLayer(li);
                      });

              auto upBtn = Tooltip(
                  GestureDetector(
                      Container(Text("▲")->setFontSize(8)->setTextColor(kDim))
                          ->setWidth(20)->setHeight(20)->setBorderRadius(3)
                          ->setBackgroundColor(kCard)->setBorderWidth(1)->setBorderColor(kBorder))
                      ->setOnTap([this, di, liveStackIndex]() {
                        if (!surface_) return;
                        int li = liveStackIndex(di);
                        if (li < 0) return;
                        surface_->moveLayerUp(li);
                      }),
                  "Move up");

              auto dnBtn = Tooltip(
                  GestureDetector(
                      Container(Text("▼")->setFontSize(8)->setTextColor(kDim))
                          ->setWidth(20)->setHeight(20)->setBorderRadius(3)
                          ->setBackgroundColor(kCard)->setBorderWidth(1)->setBorderColor(kBorder))
                      ->setOnTap([this, di, liveStackIndex]() {
                        if (!surface_) return;
                        int li = liveStackIndex(di);
                        if (li < 0) return;
                        surface_->moveLayerDown(li);
                      }),
                  "Move down");

              auto delBtn = Tooltip(
                  GestureDetector(
                      Container(Text("✕")->setFontSize(9)->setTextColor(RGB(190, 70, 70)))
                          ->setWidth(20)->setHeight(20)->setBorderRadius(3)
                          ->setBackgroundColor(RGB(36, 20, 20))
                          ->setBorderWidth(1)->setBorderColor(kBorder))
                      ->setOnTap([this, di, liveStackIndex]() {
                        if (!surface_) return;
                        int li = liveStackIndex(di);
                        if (li < 0) return;
                        surface_->deleteLayer(li);
                      }),
                  "Delete layer");

              return Container(
                         Row({
                             eye, SizedBox(5, 0), name,
                             upBtn, SizedBox(2, 0), dnBtn, SizedBox(3, 0), delBtn,
                         })
                         ->setSpacing(0)
                         ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                  ->setBackgroundColor(rowBg)
                  ->setBorderRadius(6)
                  ->setBorderWidth(bw)
                  ->setBorderColor(rowBord)
                  ->setPaddingAll(6, 5, 6, 5)
                  ->setHeight(34);
            })
            ->setSpacing(4)
            ->setScrollbarColor(RGB(60, 60, 90))
            ->setScrollbarHoverColor(RGB(90, 90, 130));

    auto layerPanel =
        Container(
            Column({
                Row({
                    Text("LAYERS")
                        ->setFontSize(8)->setFontWeight(FontWeight::Bold)->setTextColor(kDim),
                    Tooltip(
                        GestureDetector(
                            Container(Text("+")->setFontSize(16)->setTextColor(RGB(160, 220, 160)))
                                ->setWidth(28)->setHeight(28)->setBorderRadius(6)
                                ->setBackgroundColor(RGB(24, 40, 24))
                                ->setBorderWidth(1)->setBorderColor(RGB(60, 100, 60))
                                ->setHoverBackgroundColor(RGB(30, 52, 30)))
                            ->setOnTap([this]() { if (surface_) surface_->addLayer(); }),
                        "Add layer"),
                })
                ->setSpacing(0)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                SizedBox(0, 10),
                Expanded(layerList),
                SizedBox(0, 8),
                Tooltip(
                    GestureDetector(
                        Container(
                            Row({
                                Text("⊞")->setFontSize(12)->setTextColor(RGB(170, 170, 200)),
                                SizedBox(5, 0),
                                Text("Flatten All")->setFontSize(10)->setTextColor(RGB(160, 160, 185)),
                            })
                            ->setSpacing(0)
                            ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                            ->setBackgroundColor(kCard)
                            ->setBorderRadius(6)->setBorderWidth(1)->setBorderColor(kBorder)
                            ->setHoverBackgroundColor(RGB(36, 36, 56))
                            ->setPaddingAll(8, 6, 8, 6))
                        ->setOnTap([this]() { if (surface_) surface_->flattenToSingle(); }),
                    "Merge all visible layers into one"),
            })
            ->setSpacing(0))
        ->setWidth(kLayerPanelWidth)
        ->setBackgroundColor(RGB(24, 24, 36))
        ->setPaddingAll(12, 12, 12, 12);

    // ── §I  Root layout ────────────────────────────────────────────────────
    auto root =
        Row({
            sidebar,
            Column({ toolbar, hints, canvasWithMenu })->setSpacing(0),
            layerPanel,
        })
        ->setSpacing(0);

    return Scaffold(nullptr,root);
  }
};

// ── Entry point ───────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() {
    return FluxApp("Paint", BuildComponent<PaintApp>(), AppTheme::dark());
  });
  int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = GetSystemMetrics(SM_CYSCREEN);
  app.createWindow("FluxUI - Paint", screenWidth, screenHeight);
  ShowWindow(GetActiveWindow(), SW_MAXIMIZE);
  return app.run();
}