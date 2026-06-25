// #pragma once
// // ============================================================================
// // flux_canvas2d.hpp  —  HTML5-style 2-D drawing API  (pure GL, no NanoVG)
// //
// // Drop-in replacement for the stb_truetype-backed version.
// // Font rasterisation now uses FreeType 2 instead of stb_truetype.
// //
// // One Canvas2D is stack-allocated by CanvasWidget each frame and passed to
// // RenderSurface::render().  Do NOT construct one yourself.
// //
// // Thread-safety: must be called on the GL thread only.
// // ============================================================================

// #include "flux_platform.hpp"
// #include <string>
// #include <vector>
// #include <memory>

// #if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
// #include <glad/glad.h>
// #elif defined(__ANDROID__)
// #include <GLES3/gl3.h>
// #elif defined(__EMSCRIPTEN__)
// #include <GLES3/gl3.h>
// #elif defined(__APPLE__)
// #include <stdint.h>
// using GLuint   = uint32_t;
// using GLenum   = uint32_t;
// using GLint    = int32_t;
// using GLsizei  = int32_t;
// using GLfloat  = float;
// using GLubyte  = uint8_t;
// static inline void glGenTextures(int,GLuint*){}
// static inline void glBindTexture(GLenum,GLuint){}
// static inline void glTexParameteri(GLenum,GLenum,GLint){}
// static inline void glTexImage2D(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*){}
// static inline void glTexSubImage2D(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void*){}
// static inline void glDeleteTextures(GLsizei,const GLuint*){}
// static inline void glGenVertexArrays(GLsizei,GLuint*){}
// static inline void glBindVertexArray(GLuint){}
// static inline void glDeleteVertexArrays(GLsizei,const GLuint*){}
// static inline void glGenBuffers(GLsizei,GLuint*){}
// static inline void glBindBuffer(GLenum,GLuint){}
// static inline void glBufferData(GLenum,size_t,const void*,GLenum){}
// static inline void glDeleteBuffers(GLsizei,const GLuint*){}
// static inline void glEnableVertexAttribArray(GLuint){}
// static inline void glVertexAttribPointer(GLuint,GLint,GLenum,uint8_t,GLsizei,const void*){}
// static inline void glUseProgram(GLuint){}
// static inline void glUniformMatrix4fv(GLint,GLsizei,uint8_t,const GLfloat*){}
// static inline void glUniform4f(GLint,GLfloat,GLfloat,GLfloat,GLfloat){}
// static inline void glUniform1i(GLint,GLint){}
// static inline void glDrawArrays(GLenum,GLint,GLsizei){}
// static inline void glEnable(GLenum){}
// static inline void glDisable(GLenum){}
// static inline void glBlendFunc(GLenum,GLenum){}
// static inline void glScissor(GLint,GLint,GLsizei,GLsizei){}
// static inline void glReadPixels(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*){}
// static inline void glActiveTexture(GLenum){}
// static inline void glDeleteProgram(GLuint){}
// static inline GLint glGetUniformLocation(GLuint,const char*){ return -1; }
// static constexpr GLenum GL_TEXTURE_2D=0,GL_R8=0,GL_RED=0,GL_RGBA=0,
//     GL_UNSIGNED_BYTE=0,GL_LINEAR=0,GL_NEAREST=0,GL_CLAMP_TO_EDGE=0,
//     GL_TEXTURE_MIN_FILTER=0,GL_TEXTURE_MAG_FILTER=0,GL_TEXTURE_WRAP_S=0,
//     GL_TEXTURE_WRAP_T=0,GL_ARRAY_BUFFER=0,GL_DYNAMIC_DRAW=0,
//     GL_FLOAT=0,GL_FALSE=0,GL_TRIANGLES=0,GL_TRIANGLE_FAN=0,
//     GL_TRIANGLE_STRIP=0,GL_BLEND=0,GL_SCISSOR_TEST=0,GL_TEXTURE0=0,GL_SRC_ALPHA=0,GL_ONE_MINUS_SRC_ALPHA=0,
//     GL_ONE=0,GL_ZERO=0;
// namespace glutil {
//     static inline GLuint linkProgram(const char*,const char*){ return 0; }
// }
// #endif



// // FreeType forward declarations — keeps this header clean of ft2build.h.
// // The full FreeType headers are included only in flux_canvas2d_gl.cpp.
// typedef struct FT_LibraryRec_  *FT_Library;
// typedef struct FT_FaceRec_     *FT_Face;

// // ─────────────────────────────────────────────────────────────────────────────
// // Canvas2DImage — returned by Canvas2D::loadImage()
// // ─────────────────────────────────────────────────────────────────────────────
// struct Canvas2DImage
// {
//     GLuint texId = 0; // GL texture id  (0 = invalid)
//     int    width  = 0;
//     int    height = 0;
// };

// // ─────────────────────────────────────────────────────────────────────────────
// // Enums  (identical to the NanoVG version)
// // ─────────────────────────────────────────────────────────────────────────────
// enum class CanvasTextAlign  { Left, Right, Center };
// enum class TextBaseline     { Top, Middle, Bottom, Alphabetic };
// enum class LineCap          { Butt, Round, Square };
// enum class LineJoin         { Miter, Round, Bevel };
// enum class CompositeOp      { SourceOver, Copy, Xor, Multiply, Screen };
// enum class FillRule         { NonZero, EvenOdd };

// // ─────────────────────────────────────────────────────────────────────────────
// // Canvas2DGL — internal shared GL resources (one per GL context)
// // Created by CanvasWidget::ensureWindows, destroyed by CanvasWidget::destroyGL.
// // ─────────────────────────────────────────────────────────────────────────────
// struct Canvas2DGL
// {
//     // ── Flat-colour / textured shader ─────────────────────────────────────
//     GLuint flatProg  = 0;
//     GLint  flatMVP   = -1;
//     GLint  flatColor = -1;
//     GLint  flatMode  = -1; // 0=solid colour, 1=font atlas (R8), 2=RGBA tex

//     // ── Textured-quad shader (legacy slot, unused when flatProg covers it) ─
//     GLuint texProg    = 0;
//     GLint  texMVP     = -1;
//     GLint  texSampler = -1;
//     GLint  texAlpha   = -1;

//     // ── Dynamic geometry ──────────────────────────────────────────────────
//     GLuint vao = 0, vbo = 0;

//     // ── FreeType state ────────────────────────────────────────────────────
//     FT_Library ftLibrary = nullptr; // initialised in init(), destroyed in destroy()

//     // ── Font atlas (R8, single channel) ──────────────────────────────────
//     GLuint fontTex = 0;
//     int    atlasW  = 1024;
//     int    atlasH  = 1024;

//     struct FontFace
//     {
//         std::string            name;
//         std::vector<uint8_t>   ttfData; // kept alive — FT_Face references it
//         FT_Face                ftFace = nullptr;
//     };
//     std::vector<FontFace>      fonts;
//     std::vector<unsigned char> atlasPixels; // single-channel, atlasW*atlasH

//     // atlas packing cursor (simple shelf packer — identical to old version)
//     int shelfX = 0, shelfY = 0, shelfH = 0;

//     struct GlyphKey
//     {
//         int fontIdx;
//         int codepoint;
//         int pixelSize;
//         bool operator==(const GlyphKey &o) const
//         {
//             return fontIdx   == o.fontIdx   &&
//                    codepoint == o.codepoint &&
//                    pixelSize == o.pixelSize;
//         }
//     };
//     struct GlyphEntry
//     {
//         GlyphKey key;
//         int atlasX, atlasY, glyphW, glyphH;
//         int xoff, yoff;  // left / top bearing (pixels)
//         int advance;     // horizontal advance (pixels)
//     };
//     std::vector<GlyphEntry> glyphs;

//     bool  init();
//     void  destroy();
//     int   findFont(const std::string &name) const;
//     int   addFont(const std::string &name, const std::string &path);
//     const GlyphEntry *getGlyph(int fontIdx, int codepoint, int pixelSize);
//     float getKernAdvance(int fontIdx, int cp1, int cp2, int pixelSize) const;

// private:
//     void uploadAtlas();
//     bool bakeGlyph(int fontIdx, int codepoint, int pixelSize, GlyphEntry &out);
// };

// // ─────────────────────────────────────────────────────────────────────────────
// // Canvas2D — main drawing API  (identical public surface to old version)
// // ─────────────────────────────────────────────────────────────────────────────
// struct Canvas2D
// {
//     explicit Canvas2D(Canvas2DGL *gl, int canvasW, int canvasH,
//                       const float mvp[16]);
//     ~Canvas2D() = default;

//     Canvas2D(const Canvas2D &)            = delete;
//     Canvas2D &operator=(const Canvas2D &) = delete;

//     // ── Dimensions ────────────────────────────────────────────────────────
//     int width()  const { return canvasW_; }
//     int height() const { return canvasH_; }

//     // ── State stack ───────────────────────────────────────────────────────
//     void save();
//     void restore();

//     // ── Transform ─────────────────────────────────────────────────────────
//     void translate(float dx, float dy);
//     void scale(float sx, float sy);
//     void rotate(float radians);
//     void resetTransform();

//     // ── Style ─────────────────────────────────────────────────────────────
//     void setFillColor(Color c);
//     void setStrokeColor(Color c);
//     void setLineWidth(float w);
//     void setLineCap(LineCap cap);
//     void setLineJoin(LineJoin join);
//     void setMiterLimit(float limit);
//     void setGlobalAlpha(float a);
//     void setCompositeOp(CompositeOp op);
//     void setFillRule(FillRule rule);

//     // ── Linear gradient ───────────────────────────────────────────────────
//     void beginLinearGradient(float x0, float y0, float x1, float y1);
//     void addColorStop(float t, Color c);
//     void setFillGradient();

//     // ── Radial gradient ───────────────────────────────────────────────────
//     void beginRadialGradient(float cx, float cy, float innerR, float outerR);

//     // ── Primitive drawing ─────────────────────────────────────────────────
//     void clearRect(float x, float y, float w, float h);
//     void fillRect(float x, float y, float w, float h);
//     void strokeRect(float x, float y, float w, float h);
//     void fillRoundedRect(float x, float y, float w, float h, float r);
//     void strokeRoundedRect(float x, float y, float w, float h, float r);
//     void fillCircle(float cx, float cy, float r);
//     void strokeCircle(float cx, float cy, float r);

//     // ── Path API ──────────────────────────────────────────────────────────
//     void beginPath();
//     void closePath();
//     void moveTo(float x, float y);
//     void lineTo(float x, float y);
//     void arc(float cx, float cy, float radius,
//              float startAngle, float endAngle, bool anticlockwise = false);
//     void arcTo(float x1, float y1, float x2, float y2, float radius);
//     void quadraticCurveTo(float cpx, float cpy, float x, float y);
//     void bezierCurveTo(float cp1x, float cp1y,
//                        float cp2x, float cp2y,
//                        float x, float y);
//     void rect(float x, float y, float w, float h);
//     void ellipse(float cx, float cy, float rx, float ry,
//                  float rotation    = 0.f,
//                  float startAngle  = 0.f,
//                  float endAngle    = 6.28318530f,
//                  bool  anticlockwise = false);

//     void fill();
//     void stroke();
//     void clip();

//     // ── Image drawing ─────────────────────────────────────────────────────
//     Canvas2DImage *loadImage(const std::string &path);
//     Canvas2DImage *loadImageFromMemory(const unsigned char *data, int byteLen);
//     Canvas2DImage *wrapTexture(GLuint texId, int w, int h);
//     void           updateTexture(Canvas2DImage *img,
//                                  const unsigned char *rgba, int w, int h);
//     void           freeImage(Canvas2DImage *img);

//     void drawImage(const Canvas2DImage *img, float dx, float dy);
//     void drawImage(const Canvas2DImage *img,
//                    float dx, float dy, float dw, float dh);
//     void drawImage(const Canvas2DImage *img,
//                    float sx, float sy, float sw, float sh,
//                    float dx, float dy, float dw, float dh);

//     // ── Text ──────────────────────────────────────────────────────────────
//     static bool registerFont(Canvas2DGL *gl,
//                              const std::string &name,
//                              const std::string &ttfPath);

//     void  setFont(const std::string &fontDesc);
//     void  setTextAlign(CanvasTextAlign align);
//     void  setTextBaseline(TextBaseline baseline);
//     void  fillText(const std::string &text, float x, float y,
//                    float maxWidth = -1.f);
//     void  strokeText(const std::string &text, float x, float y,
//                      float maxWidth = -1.f);
//     float measureText(const std::string &text);

//     // ── Clip rect ─────────────────────────────────────────────────────────
//     void pushClipRect(float x, float y, float w, float h);
//     void popClipRect();

//     // ── Pixel access ──────────────────────────────────────────────────────
//     void getImageData(float x, float y, float w, float h,
//                       std::vector<uint8_t> &out);
//     void putImageData(const std::vector<uint8_t> &data,
//                       int srcW, int srcH,
//                       float dx, float dy);

//     float currentFontSize() const { return fontSize_; }
//     int   currentFontIdx()  const { return resolveFont(); }

//     // ── Public members accessed by Canvas2DGL internals ───────────────────
//     Canvas2DGL *gl_      = nullptr;
//     int         canvasW_ = 0;
//     int         canvasH_ = 0;

// private:
//     // ── MVP / transform stack ─────────────────────────────────────────────
//     struct Mat3
//     {
//         float m[9] = {1,0,0, 0,1,0, 0,0,1};
//         static Mat3 identity();
//         Mat3 multiply(const Mat3 &b) const;
//         Mat3 translated(float dx, float dy) const;
//         Mat3 scaled(float sx, float sy) const;
//         Mat3 rotated(float r) const;
//         void apply(float &x, float &y) const;
//     };

//     float baseMVP_[16];
//     Mat3  ctm_;

//     struct SaveState
//     {
//         Mat3  ctm;
//         Color fillColor, strokeColor;
//         float lineWidth, globalAlpha;
//         bool  fillIsGrad;
//         int   clipDepth;
//         float gx0, gy0, gx1, gy1, gcx, gcy, gInR, gOutR;
//         enum class GradType { None, Linear, Radial } gradType;
//         std::vector<std::pair<float,Color>> stops;
//         std::string fontFace;
//         float fontSize;
//         bool  fontBold, fontItalic;
//         CanvasTextAlign textAlign;
//         TextBaseline    textBaseline;
//     };
//     std::vector<SaveState> stateStack_;

//     // ── Path buffer ───────────────────────────────────────────────────────
//     struct PathPt { float x, y; bool move; };
//     std::vector<PathPt> path_;
//     float pathStartX_ = 0, pathStartY_ = 0;
//     float curX_ = 0, curY_ = 0;

//     // ── Gradient state ────────────────────────────────────────────────────
//     enum class GradType { None, Linear, Radial };
//     GradType gradType_ = GradType::None;
//     float gx0_=0, gy0_=0, gx1_=0, gy1_=0;
//     float gcx_=0, gcy_=0, gInR_=0, gOutR_=0;
//     std::vector<std::pair<float,Color>> gStops_;
//     bool fillIsGrad_ = false;

//     // ── Draw state ────────────────────────────────────────────────────────
//     Color      fillColor_   = {0,0,0,255};
//     Color      strokeColor_ = {0,0,0,255};
//     float      lineWidth_   = 1.f;
//     float      globalAlpha_ = 1.f;
//     FillRule   fillRule_    = FillRule::NonZero;
//     int        clipDepth_   = 0;
//     CompositeOp compositeOp_ = CompositeOp::SourceOver;

//     // ── Text state ────────────────────────────────────────────────────────
//     std::string     fontFace_     = "sans";
//     float           fontSize_     = 14.f;
//     bool            fontBold_     = false;
//     bool            fontItalic_   = false;
//     CanvasTextAlign textAlign_    = CanvasTextAlign::Left;
//     TextBaseline    textBaseline_ = TextBaseline::Alphabetic;

//     // ── Helpers ───────────────────────────────────────────────────────────
//     void  buildMVP(float out[16]) const;
//     void  uploadAndDraw(const float *verts, int count, GLenum mode,
//                         Color col, float alpha = 1.f);
//     void  drawTexturedQuad(GLuint tex,
//                            float sx, float sy, float sw, float sh,
//                            float dx, float dy, float dw, float dh,
//                            int texW, int texH, float alpha = 1.f);
//     void  tessellateArc(float cx, float cy, float r,
//                         float a0, float a1, bool ccw, bool move);
//     void  strokePath();
//     void  fillPath();
//     Color blendAlpha(Color c) const;
//     void  parseFontDesc(const std::string &desc);
//     int   resolveFont() const;
// };