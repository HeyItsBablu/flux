#ifndef FLUX_RASTER_HPP
#define FLUX_RASTER_HPP

// flux/platform/win32/flux_raster_win32.hpp
#pragma once
#ifndef _WIN32
#error "flux_raster_win32.hpp must only be compiled on Win32"
#endif

#include "../../flux_canvas_types.hpp"
#include "../../flux_render_surface.hpp"

class RasterSurface : public RenderSurface {
public:
  static constexpr size_t kDefaultUndoBudgetBytes = 256ULL * 1024 * 1024;
  static constexpr int kColorHistoryMax = 16;

  explicit RasterSurface(size_t budget = kDefaultUndoBudgetBytes)
      : undoBudget_(budget) {}
  ~RasterSurface() { destroy(); }

  // ── Tool / style setters ─────────────────────────────────────────────────

  void setTool(ToolId t) { tool_ = t; }
  ToolId getTool() const { return tool_; }

  void setStrokeStyle(const StrokeStyle &s) {
    style_ = s;
    style_.tool = tool_;
  }
  const StrokeStyle &getStrokeStyle() const { return style_; }

  void setOpacity(float op) { style_.opacity = std::clamp(op, 0.f, 1.f); }
  float getOpacity() const { return style_.opacity; }

  // ── Color history ────────────────────────────────────────────────────────

  const std::vector<RGBA> &colorHistory() const { return colorHistory_; }

  // ── Undo / Redo ──────────────────────────────────────────────────────────

  void undo() {
    if (undoStack_.empty())
      return;
    size_t sz = snapshotBytes();
    redoStack_.push_back({snapshotCommitted(), sz});
    redoBytes_ += sz;
    Snapshot s = undoStack_.back();
    undoStack_.pop_back();
    undoBytes_ -= s.bytes;
    restoreCommitted(s.tex);
    glDeleteTextures(1, &s.tex);
    dirty_ = true;
  }

  void redo() {
    if (redoStack_.empty())
      return;
    size_t sz = snapshotBytes();
    undoStack_.push_back({snapshotCommitted(), sz});
    undoBytes_ += sz;
    Snapshot s = redoStack_.back();
    redoStack_.pop_back();
    redoBytes_ -= s.bytes;
    restoreCommitted(s.tex);
    glDeleteTextures(1, &s.tex);
    dirty_ = true;
  }

  bool canUndo() const { return !undoStack_.empty(); }
  bool canRedo() const { return !redoStack_.empty(); }

  void clear() {
    pushUndoSnapshot();
    clearFBO(committedFBO_, w_, h_, 255, 255, 255, 255);
    clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
    dirty_ = true;
  }

  // ── PNG export ───────────────────────────────────────────────────────────

  bool savePNG(const std::wstring &path) {
    if (!committedFBO_ || w_ <= 0 || h_ <= 0)
      return false;

    std::vector<uint8_t> buf(size_t(w_) * h_ * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    std::vector<uint8_t> flipped(buf.size());
    const int stride = w_ * 4;
    for (int row = 0; row < h_; ++row) {
      memcpy(flipped.data() + row * stride,
             buf.data() + (h_ - 1 - row) * stride, stride);
    }

    for (int i = 0; i < w_ * h_; ++i) {
      uint8_t r = flipped[i * 4 + 0];
      uint8_t g = flipped[i * 4 + 1];
      uint8_t b = flipped[i * 4 + 2];
      uint8_t a = flipped[i * 4 + 3];
      flipped[i * 4 + 0] = b;
      flipped[i * 4 + 1] = g;
      flipped[i * 4 + 2] = r;
      flipped[i * 4 + 3] = a;
    }

    Gdiplus::Bitmap bmp(w_, h_, stride, PixelFormat32bppARGB, flipped.data());
    if (bmp.GetLastStatus() != Gdiplus::Ok)
      return false;

    CLSID pngClsid;
    {
      UINT num = 0, sz = 0;
      Gdiplus::GetImageEncodersSize(&num, &sz);
      if (sz == 0)
        return false;
      std::vector<uint8_t> ebuf(sz);
      auto *encoders = reinterpret_cast<Gdiplus::ImageCodecInfo *>(ebuf.data());
      Gdiplus::GetImageEncoders(num, sz, encoders);
      bool found = false;
      for (UINT i = 0; i < num; ++i) {
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
          pngClsid = encoders[i].Clsid;
          found = true;
          break;
        }
      }
      if (!found)
        return false;
    }

    Gdiplus::Status st = bmp.Save(path.c_str(), &pngClsid, nullptr);
    return st == Gdiplus::Ok;
  }

  // ── RenderSurface interface ───────────────────────────────────────────────

  void initialize(int w, int h) override {
    w_ = w;
    h_ = h;

    if (!gdiplusToken_) {
      Gdiplus::GdiplusStartupInput gsi;
      Gdiplus::GdiplusStartup(&gdiplusToken_, &gsi, nullptr);
    }

    buildShaders();
    buildQuadVAO();
    buildDabVerts();
    allocFBOPair(committedFBO_, committedTex_, w_, h_);
    allocFBOPair(scratchFBO_, scratchTex_, w_, h_);
    clearFBO(committedFBO_, w_, h_, 255, 255, 255, 255);
    clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
  }

  void resize(int w, int h) override {
    if (w == w_ && h == h_)
      return;
    GLuint nCF = 0, nCT = 0, nSF = 0, nST = 0;
    allocFBOPair(nCF, nCT, w, h);
    allocFBOPair(nSF, nST, w, h);
    clearFBO(nCF, w, h, 255, 255, 255, 255);
    clearFBO(nSF, w, h, 0, 0, 0, 0);
    int cw = std::min(w_, w), ch = std::min(h_, h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, nCF);
    glBlitFramebuffer(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    destroyFBOPair(committedFBO_, committedTex_);
    destroyFBOPair(scratchFBO_, scratchTex_);
    committedFBO_ = nCF;
    committedTex_ = nCT;
    scratchFBO_ = nSF;
    scratchTex_ = nST;
    flushDeque(undoStack_, undoBytes_);
    flushDeque(redoStack_, redoBytes_);
    w_ = w;
    h_ = h;
    dirty_ = true;
  }

  void update(double) override {}

  void render(const float mvp[16]) override {
    GLboolean blendWas = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.15f, 0.15f, 0.17f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    blitTexture(committedTex_, 1.f, mvp);
    blitTexture(scratchTex_, 1.f, mvp);

    glDisable(GL_BLEND);

    dirty_ = false;
    glUseProgram(0);
    if (blendWas)
      glEnable(GL_BLEND);
  }

  void onMouseDown(float x, float y) override { beginStroke(x, y); }
  void onMouseMove(float x, float y) override {
    if (drawing_)
      drawSegment(x, y);
  }
  void onMouseUp(float x, float y) override {
    if (drawing_)
      endStroke(x, y);
  }

  void onKeyDown(int key) override {
    bool ctrl = (GetKeyState(Key::Control) & 0x8000) != 0;
    bool shift = (GetKeyState(Key::Shift) & 0x8000) != 0;
    if (ctrl && key == 'Z') {
      if (shift)
        redo();
      else
        undo();
    }
    if (ctrl && key == 'Y')
      redo();
  }
  void onKeyUp(int) override {}

  void destroy() override {
    destroyFBOPair(committedFBO_, committedTex_);
    destroyFBOPair(scratchFBO_, scratchTex_);
    if (blitProg_) {
      glDeleteProgram(blitProg_);
      blitProg_ = 0;
    }
    if (quadVAO_) {
      glDeleteVertexArrays(1, &quadVAO_);
      quadVAO_ = 0;
    }
    if (quadVBO_) {
      glDeleteBuffers(1, &quadVBO_);
      quadVBO_ = 0;
    }
    if (shapeVBO_) {
      glDeleteBuffers(1, &shapeVBO_);
      shapeVBO_ = 0;
    }
    flushDeque(undoStack_, undoBytes_);
    flushDeque(redoStack_, redoBytes_);

    if (gdiplusToken_) {
      Gdiplus::GdiplusShutdown(gdiplusToken_);
      gdiplusToken_ = 0;
    }
  }

protected:
  int canvasWidth() const { return w_; }
  int canvasHeight() const { return h_; }
  GLuint committedFBOHandle() const { return committedFBO_; }
  GLuint committedTexHandle() const { return committedTex_; }
  GLuint scratchFBOHandle() const { return scratchFBO_; }
  GLuint scratchTexHandle() const { return scratchTex_; }
  GLuint blitProgHandle() const { return blitProg_; }
  GLuint quadVAOHandle() const { return quadVAO_; }
  GLuint quadVBOHandle() const { return quadVBO_; }
  bool isDrawing() const { return drawing_; }

  void pushUndoSnapshotPublic() { pushUndoSnapshot(); }

  void scratchClear() { clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0); }
  void swapActiveFBOs(GLuint newCFBO, GLuint newCTex, GLuint newSFBO,
                      GLuint newSTex, GLuint &oldCFBO, GLuint &oldCTex,
                      GLuint &oldSFBO, GLuint &oldSTex) {
    oldCFBO = committedFBO_;
    oldCTex = committedTex_;
    oldSFBO = scratchFBO_;
    oldSTex = scratchTex_;
    committedFBO_ = newCFBO;
    committedTex_ = newCTex;
    scratchFBO_ = newSFBO;
    scratchTex_ = newSTex;
  }

  void flushUndoRedoPublic() {
    flushDeque(undoStack_, undoBytes_);
    flushDeque(redoStack_, redoBytes_);
  }

  void scratchCommit() {
    pushUndoSnapshot();
    mergeScratchIntoCommitted();
    clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
  }

  GLint uMVP() const { return u_.mvp; }
  GLint uMode() const { return u_.mode; }
  GLint uColor() const { return u_.color; }
  GLint uTex() const { return u_.tex; }
  GLint uAlpha() const { return u_.alpha; }

  void getCanvasOrtho(float out[16]) const { canvasOrtho(out); }

  void drawVertsToFBO(GLuint fbo, const float *verts, int count, GLenum mode,
                      float r, float g, float b, float a) {
    float mvp[16];
    canvasOrtho(mvp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(blitProg_);
    glUniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    glUniform1i(u_.mode, 1);
    glUniform4f(u_.color, r, g, b, a);

    GLsizeiptr needed = GLsizeiptr(sizeof(float)) * 2 * count;
    glBindBuffer(GL_ARRAY_BUFFER, shapeVBO_);
    glBufferData(GL_ARRAY_BUFFER, needed, verts, GL_DYNAMIC_DRAW);

    glBindVertexArray(quadVAO_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glDisableVertexAttribArray(1);

    glDrawArrays(mode, 0, count);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void uploadToCommitted(const uint8_t *pixels) {
    glBindTexture(GL_TEXTURE_2D, committedTex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE,
                    pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void readCommitted(uint8_t *pixels) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  }

  // ── Text rendering primitives ─────────────────────────────────────────────

  void renderTextToScratch(const std::wstring &text, float canvasX,
                           float canvasY, const TextStyle &style,
                           bool showCursor = false) {
    if (w_ <= 0 || h_ <= 0)
      return;

    int bx, by, bw, bh;
    std::wstring display = showCursor ? text + L'|' : text;
    std::vector<uint8_t> rgba =
        rasterizeTextGDI(display, canvasX, canvasY, style, bx, by, bw, bh);
    if (rgba.empty())
      return;

    int uploadX = std::max(0, bx);
    int uploadY = std::max(0, by);
    int cropLeft = uploadX - bx;
    int cropBottom = uploadY - by;
    int uploadW = std::min(bw - cropLeft, w_ - uploadX);
    int uploadH = std::min(bh - cropBottom, h_ - uploadY);
    if (uploadW <= 0 || uploadH <= 0)
      return;

    const uint8_t *src = rgba.data();
    std::vector<uint8_t> cropped;
    if (cropLeft > 0 || cropBottom > 0) {
      cropped.resize(size_t(uploadW) * uploadH * 4);
      for (int row = 0; row < uploadH; ++row) {
        memcpy(cropped.data() + row * uploadW * 4,
               src + (size_t(cropBottom + row) * bw + cropLeft) * 4,
               size_t(uploadW) * 4);
      }
      src = cropped.data();
    }

    glBindTexture(GL_TEXTURE_2D, scratchTex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, uploadX, uploadY, uploadW, uploadH,
                    GL_RGBA, GL_UNSIGNED_BYTE, src);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void commitTextToCanvas(const std::wstring &text, float canvasX,
                          float canvasY, const TextStyle &style) {
    if (text.empty() || w_ <= 0 || h_ <= 0)
      return;

    int bx, by, bw, bh;
    std::vector<uint8_t> rgba =
        rasterizeTextGDI(text, canvasX, canvasY, style, bx, by, bw, bh);
    if (rgba.empty())
      return;

    int dstX = std::max(0, bx);
    int dstY = std::max(0, by);
    int srcOffX = dstX - bx;
    int srcOffY = dstY - by;
    int dstW = std::min(bw - srcOffX, w_ - dstX);
    int dstH = std::min(bh - srcOffY, h_ - dstY);
    if (dstW <= 0 || dstH <= 0)
      return;

    pushUndoSnapshot();

    std::vector<uint8_t> committed(size_t(w_) * h_ * 4);
    readCommitted(committed.data());

    for (int row = 0; row < dstH; ++row) {
      for (int col = 0; col < dstW; ++col) {
        const uint8_t *s =
            rgba.data() + ((size_t(srcOffY + row) * bw) + srcOffX + col) * 4;
        uint8_t *d =
            committed.data() + ((size_t(dstY + row) * w_) + dstX + col) * 4;
        float sa = s[3] / 255.f;
        if (sa <= 0.f)
          continue;

        if (sa >= 1.f) {
          d[0] = s[0];
          d[1] = s[1];
          d[2] = s[2];
          d[3] = 255;
        } else {
          float da = d[3] / 255.f;
          float oa = sa + da * (1.f - sa);
          if (oa > 0.f) {
            d[0] = (uint8_t)((s[0] * sa + d[0] * da * (1.f - sa)) / oa);
            d[1] = (uint8_t)((s[1] * sa + d[1] * da * (1.f - sa)) / oa);
            d[2] = (uint8_t)((s[2] * sa + d[2] * da * (1.f - sa)) / oa);
            d[3] = (uint8_t)(oa * 255.f);
          }
        }
      }
    }

    uploadToCommitted(committed.data());
    dirty_ = true;
  }

private:
  int w_ = 0, h_ = 0;
  bool drawing_ = false, dirty_ = false;
  StrokePoint lastPt_{};
  StrokeStyle style_{};
  ToolId tool_ = kToolBrush;

  GLuint committedFBO_ = 0, committedTex_ = 0;
  GLuint scratchFBO_ = 0, scratchTex_ = 0;
  GLuint blitProg_ = 0, quadVAO_ = 0, quadVBO_ = 0;
  GLuint shapeVBO_ = 0;

  ULONG_PTR gdiplusToken_ = 0;

  struct ULocs {
    GLint mvp = -1, mode = -1, tex = -1, alpha = -1, color = -1;
  } u_;

  struct Snapshot {
    GLuint tex;
    size_t bytes;
  };
  size_t undoBudget_, undoBytes_ = 0, redoBytes_ = 0;
  std::deque<Snapshot> undoStack_, redoStack_;

  std::vector<RGBA> colorHistory_;

  void pushColorHistory(const RGBA &c) {
    if (!colorHistory_.empty()) {
      const RGBA &last = colorHistory_.front();
      if (std::abs(last.r - c.r) < 0.01f && std::abs(last.g - c.g) < 0.01f &&
          std::abs(last.b - c.b) < 0.01f)
        return;
    }
    colorHistory_.insert(colorHistory_.begin(), c);
    if ((int)colorHistory_.size() > kColorHistoryMax)
      colorHistory_.resize(kColorHistoryMax);
  }

  size_t snapshotBytes() const { return size_t(w_) * h_ * 4; }

  static constexpr int kDabVerts = 32;
  float dabVerts[(kDabVerts + 2) * 2]{};

  static void flushDeque(std::deque<Snapshot> &dq, size_t &cnt) {
    for (auto &s : dq)
      glDeleteTextures(1, &s.tex);
    dq.clear();
    cnt = 0;
  }

  // ── rasterizeTextGDI ──────────────────────────────────────────────────────
  std::vector<uint8_t> rasterizeTextGDI(const std::wstring &text, float canvasX,
                                        float canvasY, const TextStyle &style,
                                        int &outX, int &outY, int &outW,
                                        int &outH) {
    outX = outY = outW = outH = 0;
    if (text.empty() || w_ <= 0 || h_ <= 0)
      return {};

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w_;
    bmi.bmiHeader.biHeight = -h_;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hBmp =
        CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp) {
      DeleteDC(hdcMem);
      return {};
    }
    HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);

    {
      RECT rc = {0, 0, w_, h_};
      HBRUSH br = CreateSolidBrush(RGB(255, 255, 255));
      FillRect(hdcMem, &rc, br);
      DeleteObject(br);
    }

    int weight = style.bold ? FW_BOLD : FW_NORMAL;
    DWORD italic = style.italic ? TRUE : FALSE;
    DWORD uline = style.underline ? TRUE : FALSE;
    HFONT hFont = CreateFontW(
        -style.fontSize, 0, 0, 0, weight, italic, uline, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, style.fontFace.c_str());
    HGDIOBJ oldFont = SelectObject(hdcMem, hFont);

    COLORREF cr =
        RGB((int)(style.r * 255.f + 0.5f), (int)(style.g * 255.f + 0.5f),
            (int)(style.b * 255.f + 0.5f));
    SetTextColor(hdcMem, cr);
    SetBkMode(hdcMem, TRANSPARENT);

    int gdiX = (int)canvasX;
    int gdiY = h_ - (int)canvasY - style.fontSize;

    SIZE tsz = {};
    GetTextExtentPoint32W(hdcMem, text.c_str(), (int)text.size(), &tsz);

    TextOutW(hdcMem, gdiX, gdiY, text.c_str(), (int)text.size());
    GdiFlush();

    const int kMargin = 2;
    int bboxGdiX0 = std::max(0, gdiX - kMargin);
    int bboxGdiY0 = std::max(0, gdiY - kMargin);
    int bboxGdiX1 = std::min(w_, gdiX + (int)tsz.cx + kMargin);
    int bboxGdiY1 = std::min(h_, gdiY + (int)tsz.cy + kMargin);
    int bboxW = bboxGdiX1 - bboxGdiX0;
    int bboxH = bboxGdiY1 - bboxGdiY0;

    if (bboxW <= 0 || bboxH <= 0) {
      SelectObject(hdcMem, oldFont);
      DeleteObject(hFont);
      SelectObject(hdcMem, oldBmp);
      DeleteObject(hBmp);
      DeleteDC(hdcMem);
      return {};
    }

    const uint8_t *dibBits = reinterpret_cast<const uint8_t *>(bits);

    std::vector<uint8_t> rgba(size_t(bboxW) * bboxH * 4);
    for (int row = 0; row < bboxH; ++row) {
      int gdiRow = bboxGdiY0 + (bboxH - 1 - row);
      for (int col = 0; col < bboxW; ++col) {
        const uint8_t *p =
            dibBits + (size_t(gdiRow) * w_ + bboxGdiX0 + col) * 4;
        uint8_t b8 = p[0], g8 = p[1], r8 = p[2];
        bool isBg = (r8 >= 250 && g8 >= 250 && b8 >= 250);
        uint8_t *d = rgba.data() + (size_t(row) * bboxW + col) * 4;
        d[0] = r8;
        d[1] = g8;
        d[2] = b8;
        d[3] = isBg ? 0 : 255;
      }
    }

    SelectObject(hdcMem, oldFont);
    DeleteObject(hFont);
    SelectObject(hdcMem, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);

    outX = bboxGdiX0;
    outY = h_ - bboxGdiY1;
    outW = bboxW;
    outH = bboxH;

    return rgba;
  }

  // ── Stroke lifecycle ──────────────────────────────────────────────────────

  void beginStroke(float x, float y) {
    if (drawing_)
      return;
    drawing_ = true;
    lastPt_ = {x, y, 1.f};
    flushDeque(redoStack_, redoBytes_);

    if (tool_ == kToolBrush) {
      pushColorHistory({style_.r, style_.g, style_.b, 1.f});
      clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
      paintDabBrush(scratchFBO_, x, y, style_);
    } else {
      pushUndoSnapshot();
      paintDabEraser(committedFBO_, x, y, style_);
    }
    dirty_ = true;
  }

  void drawSegment(float x, float y) {
    float dx = x - lastPt_.x, dy = y - lastPt_.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.0001f)
      return;
    float step = std::max(1.f, style_.radius * .25f);
    int steps = std::max(1, (int)(dist / step));
    for (int i = 1; i <= steps; ++i) {
      float t = float(i) / steps;
      if (tool_ == kToolBrush)
        paintDabBrush(scratchFBO_, lastPt_.x + dx * t, lastPt_.y + dy * t,
                      style_);
      else
        paintDabEraser(committedFBO_, lastPt_.x + dx * t, lastPt_.y + dy * t,
                       style_);
    }
    lastPt_ = {x, y, 1.f};
    dirty_ = true;
  }

  void endStroke(float x, float y) {
    drawSegment(x, y);
    if (tool_ == kToolBrush) {
      pushUndoSnapshot();
      mergeScratchIntoCommitted();
      clearFBO(scratchFBO_, w_, h_, 0, 0, 0, 0);
    }
    drawing_ = false;
    dirty_ = true;
  }

  // ── FBO helpers ───────────────────────────────────────────────────────────

  void allocFBOPair(GLuint &fbo, GLuint &tex, int w, int h) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void destroyFBOPair(GLuint &fbo, GLuint &tex) {
    if (fbo) {
      glDeleteFramebuffers(1, &fbo);
      fbo = 0;
    }
    if (tex) {
      glDeleteTextures(1, &tex);
      tex = 0;
    }
  }

  void clearFBO(GLuint fbo, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                uint8_t a) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glClearColor(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  GLuint snapshotCommitted() {
    GLuint st, sf;
    glGenTextures(1, &st);
    glBindTexture(GL_TEXTURE_2D, st);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w_, h_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &sf);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sf);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, st, 0);
    assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) ==
           GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, committedFBO_);
    glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &sf);
    return st;
  }

  void restoreCommitted(GLuint snapTex) {
    GLuint rf;
    glGenFramebuffers(1, &rf);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, rf);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, snapTex, 0);
    assert(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) ==
           GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, committedFBO_);
    glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &rf);
  }

  void pushUndoSnapshot() {
    size_t sz = snapshotBytes();
    while (undoBudget_ > 0 && !undoStack_.empty() &&
           undoBytes_ + sz > undoBudget_) {
      auto &o = undoStack_.front();
      glDeleteTextures(1, &o.tex);
      undoBytes_ -= o.bytes;
      undoStack_.pop_front();
    }
    undoStack_.push_back({snapshotCommitted(), sz});
    undoBytes_ += sz;
  }

  void canvasOrtho(float out[16]) const {
    glutil::ortho(0.f, float(w_), 0.f, float(h_), out);
  }

  void mergeScratchIntoCommitted() {
    float mvp[16];
    canvasOrtho(mvp);
    glBindFramebuffer(GL_FRAMEBUFFER, committedFBO_);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    blitTexture(scratchTex_, 1.f, mvp);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  // ── Dab painting ─────────────────────────────────────────────────────────

  void paintDabBrush(GLuint fbo, float cx, float cy, const StrokeStyle &s) {
    float mvp[16];
    canvasOrtho(mvp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    paintDabImpl(cx, cy, s.r, s.g, s.b, s.a * s.opacity, s.radius, mvp);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void paintDabEraser(GLuint fbo, float cx, float cy, const StrokeStyle &s) {
    float mvp[16];
    canvasOrtho(mvp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w_, h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    paintDabImpl(cx, cy, 1.f, 1.f, 1.f, s.opacity, s.radius, mvp);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void paintDabImpl(float cx, float cy, float r, float g, float b, float alpha,
                    float radius, const float mvp[16]) {
    glUseProgram(blitProg_);
    glUniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    glUniform1i(u_.mode, 1);
    glUniform4f(u_.color, r, g, b, alpha);

    float verts[(kDabVerts + 2) * 2];
    for (int i = 0; i < kDabVerts + 2; ++i) {
      verts[i * 2 + 0] = cx + dabVerts[i * 2 + 0] * radius;
      verts[i * 2 + 1] = cy + dabVerts[i * 2 + 1] * radius;
    }
    glBindVertexArray(quadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glDisableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_FAN, 0, kDabVerts + 2);
    glBindVertexArray(0);
    glUseProgram(0);
  }

  void blitTexture(GLuint tex, float alpha, const float *mvp) {
    glUseProgram(blitProg_);
    glUniformMatrix4fv(u_.mvp, 1, GL_FALSE, mvp);
    glUniform1i(u_.mode, 0);
    glUniform1i(u_.tex, 0);
    glUniform1f(u_.alpha, alpha);
    glBindTexture(GL_TEXTURE_2D, tex);
    float q[] = {
        0.f,       0.f,       0.f, 0.f, float(w_), 0.f,       1.f, 0.f,
        float(w_), float(h_), 1.f, 1.f, float(w_), float(h_), 1.f, 1.f,
        0.f,       float(h_), 0.f, 1.f, 0.f,       0.f,       0.f, 0.f,
    };
    glBindVertexArray(quadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(q), q);
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
uniform vec4 uColor;
uniform float uAlpha;
uniform int uMode;
out vec4 fragColor;
void main(){
    if(uMode==0){ vec4 c=texture(uTex,vUV); fragColor=vec4(c.rgb,c.a*uAlpha); }
    else         { fragColor=uColor; }
}
)GLSL";
    blitProg_ = glutil::linkProgram(vert, frag);
    assert(blitProg_);
    u_.mvp = glGetUniformLocation(blitProg_, "uMVP");
    u_.mode = glGetUniformLocation(blitProg_, "uMode");
    u_.tex = glGetUniformLocation(blitProg_, "uTex");
    u_.alpha = glGetUniformLocation(blitProg_, "uAlpha");
    u_.color = glGetUniformLocation(blitProg_, "uColor");
  }

  void buildQuadVAO() {
    constexpr GLsizeiptr kBlitBytes = sizeof(float) * 6 * 4;
    constexpr GLsizeiptr kDabBytes = sizeof(float) * (kDabVerts + 2) * 2;
    constexpr GLsizeiptr kQuadBufSz =
        kBlitBytes > kDabBytes ? kBlitBytes : kDabBytes;

    glGenVertexArrays(1, &quadVAO_);
    glGenBuffers(1, &quadVBO_);
    glBindVertexArray(quadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferData(GL_ARRAY_BUFFER, kQuadBufSz, nullptr, GL_DYNAMIC_DRAW);
    glBindVertexArray(0);

    glGenBuffers(1, &shapeVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, shapeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 12, nullptr,
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  void buildDabVerts() {
    dabVerts[0] = 0.f;
    dabVerts[1] = 0.f;
    for (int i = 0; i <= kDabVerts; ++i) {
      float a = float(i) / kDabVerts * 6.2831853f;
      dabVerts[2 + i * 2 + 0] = cosf(a);
      dabVerts[2 + i * 2 + 1] = sinf(a);
    }
  }
};

// ============================================================================
// §R7  FACTORY HELPERS
// ============================================================================

// Bare canvas widget with a RasterSurface; viewport and scrollbars disabled
// (1:1 pixel mapping — ideal for thumbnail pickers, stamps, etc.).
inline std::shared_ptr<CanvasWidget> RasterCanvas(int w, int h) {
  auto c = std::make_shared<CanvasWidget>()->setSize(w, h);
  c->setCanvasSize(w, h);
  c->setViewportEnabled(false);
  c->setSurface<RasterSurface>();
  return c;
}

// Same but with an explicit undo-budget (bytes).
inline std::shared_ptr<CanvasWidget> RasterCanvas(int w, int h,
                                                  size_t undoBudget) {
  auto c = std::make_shared<CanvasWidget>()->setSize(w, h);
  c->setCanvasSize(w, h);
  c->setSurface<RasterSurface>(undoBudget);
  return c;
}

// Separate view and canvas dimensions — viewport + scrollbars enabled so the
// user can pan/zoom a canvas larger (or smaller) than the widget footprint.
inline std::shared_ptr<CanvasWidget> RasterCanvas(int viewW, int viewH,
                                                  int canvasW, int canvasH) {
  auto c = std::make_shared<CanvasWidget>()->setSize(viewW, viewH);
  c->setCanvasSize(canvasW, canvasH);
  c->setSurface<RasterSurface>();
  return c;
}

// Same with an explicit undo-budget.
inline std::shared_ptr<CanvasWidget> RasterCanvas(int viewW, int viewH,
                                                  int canvasW, int canvasH,
                                                  size_t undoBudget) {
  auto c = std::make_shared<CanvasWidget>()->setSize(viewW, viewH);
  c->setCanvasSize(canvasW, canvasH);
  c->setSurface<RasterSurface>(undoBudget);
  return c;
}

#endif // FLUX_RASTER_HPP