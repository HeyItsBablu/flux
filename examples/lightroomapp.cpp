#include "flux/flux.hpp"
#include "stb_image_write.h"
#include <atomic>
#include <glad/glad.h>
#include <mutex>

// ── Shader sources
// ────────────────────────────────────────────────────────────

static const char *kExposureVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main(){ vUV = aUV; gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char *kExposureFrag = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTex;
uniform float uExposure;
uniform float uContrast;
uniform float uHighlights;
uniform float uShadows;
uniform float uWhites;
uniform float uBlacks;
uniform float uSaturation;
uniform float uVibrance;
uniform float uTemperature;
uniform float uTint;
uniform float uSharpness;
uniform vec2  uTexelSize;
uniform float uAlpha;

uniform sampler2D uCurveLUT;
uniform bool      uCurveEnabled;

uniform sampler2D uHSLHue;
uniform sampler2D uHSLSat;
uniform sampler2D uHSLLum;
uniform bool      uHSLEnabled;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 rgb2hsl(vec3 c) {
    float maxC = max(c.r, max(c.g, c.b));
    float minC = min(c.r, min(c.g, c.b));
    float delta = maxC - minC;
    float l = (maxC + minC) * 0.5;
    float s = 0.0;
    if (delta > 0.0001)
        s = delta / (1.0 - abs(2.0 * l - 1.0));
    float h = 0.0;
    if (delta > 0.0001) {
        if (maxC == c.r)      h = mod((c.g - c.b) / delta, 6.0);
        else if (maxC == c.g) h = (c.b - c.r) / delta + 2.0;
        else                  h = (c.r - c.g) / delta + 4.0;
        h /= 6.0;
        if (h < 0.0) h += 1.0;
    }
    return vec3(h, clamp(s, 0.0, 1.0), clamp(l, 0.0, 1.0));
}
float hue2rgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}
vec3 hsl2rgb(vec3 hsl) {
    float h = hsl.x, s = hsl.y, l = hsl.z;
    if (s < 0.0001) return vec3(l);
    float q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
    float p = 2.0 * l - q;
    return vec3(hue2rgb(p, q, h + 1.0/3.0),
                hue2rgb(p, q, h),
                hue2rgb(p, q, h - 1.0/3.0));
}

vec3 applyCurveLUT(vec3 c) {
    float inR = texture(uCurveLUT, vec2(c.r, 0.0/3.0)).r;
    float inG = texture(uCurveLUT, vec2(c.g, 0.0/3.0)).r;
    float inB = texture(uCurveLUT, vec2(c.b, 0.0/3.0)).r;
    c = vec3(inR, inG, inB);
    c.r = texture(uCurveLUT, vec2(c.r, 1.0/3.0)).r;
    c.g = texture(uCurveLUT, vec2(c.g, 2.0/3.0)).r;
    c.b = texture(uCurveLUT, vec2(c.b, 3.0/3.0)).r;
    return c;
}

vec3 applyHSL(vec3 c) {
    vec3 hsl = rgb2hsl(c);
    float hueNorm = hsl.x;
    float hs  = texture(uHSLHue, vec2(hueNorm, 0.5)).r;
    float ss  = texture(uHSLSat, vec2(hueNorm, 0.5)).r;
    float ls  = texture(uHSLLum, vec2(hueNorm, 0.5)).r;
    hsl.x = fract(hsl.x + (hs - 0.5) / 6.0);
    hsl.y = clamp(hsl.y * ss * 2.0, 0.0, 1.0);
    hsl.z = clamp(hsl.z + (ls - 0.5) * 2.0, 0.0, 1.0);
    return hsl2rgb(hsl);
}

vec3 applyHighlights(vec3 c, float h) {
    float mask = smoothstep(0.5, 1.0, luma(c));
    return c + h * mask * (1.0 - c) * 0.5;
}
vec3 applyShadows(vec3 c, float s) {
    float mask = 1.0 - smoothstep(0.0, 0.5, luma(c));
    return c + s * mask * c * 0.5;
}
vec3 applyWhites(vec3 c, float w) {
    float mask = smoothstep(0.75, 1.0, luma(c));
    return clamp(c + w * mask * 0.3, 0.0, 1.0);
}
vec3 applyBlacks(vec3 c, float b) {
    float mask = 1.0 - smoothstep(0.0, 0.25, luma(c));
    return clamp(c + b * mask * 0.3, 0.0, 1.0);
}
vec3 applySaturation(vec3 c, float s) {
    float grey = luma(c);
    return mix(vec3(grey), c, 1.0 + s);
}
vec3 applyVibrance(vec3 c, float v) {
    float grey = luma(c);
    float sat  = length(c - vec3(grey));
    float boost = v * (1.0 - sat * 2.0);
    return mix(vec3(grey), c, 1.0 + boost);
}
vec3 applyTemperature(vec3 c, float t) {
    c.r = clamp(c.r + t * 0.1, 0.0, 1.0);
    c.b = clamp(c.b - t * 0.1, 0.0, 1.0);
    return c;
}
vec3 applyTint(vec3 c, float t) {
    c.g = clamp(c.g - t * 0.1, 0.0, 1.0);
    c.r = clamp(c.r + t * 0.05, 0.0, 1.0);
    c.b = clamp(c.b + t * 0.05, 0.0, 1.0);
    return c;
}
vec3 applySharpness(sampler2D tex, vec2 uv, vec3 c, float s, vec2 ts) {
    if (s <= 0.0) return c;
    vec3 blur =
        texture(tex, uv + vec2(-ts.x,-ts.y)).rgb +
        texture(tex, uv + vec2( 0.0, -ts.y)).rgb +
        texture(tex, uv + vec2( ts.x,-ts.y)).rgb +
        texture(tex, uv + vec2(-ts.x,  0.0)).rgb +
        texture(tex, uv + vec2( ts.x,  0.0)).rgb +
        texture(tex, uv + vec2(-ts.x, ts.y)).rgb +
        texture(tex, uv + vec2( 0.0,  ts.y)).rgb +
        texture(tex, uv + vec2( ts.x, ts.y)).rgb;
    blur /= 8.0;
    return clamp(c + (c - blur) * s * 3.0, 0.0, 1.0);
}

void main(){
    vec4 t = texture(uTex, vUV);
    vec3 c = t.rgb;

    c *= pow(2.0, uExposure);
    float cs = 1.0 + uContrast;
    c = clamp((c - 0.5) * cs + 0.5, 0.0, 1.0);
    c = applyHighlights(c, uHighlights);
    c = applyShadows   (c, uShadows);
    c = applyWhites    (c, uWhites);
    c = applyBlacks    (c, uBlacks);
    c = applyTemperature(c, uTemperature);
    c = applyTint       (c, uTint);
    c = applyVibrance   (c, uVibrance);
    c = applySaturation (c, uSaturation);

    if (uCurveEnabled)
        c = applyCurveLUT(clamp(c, 0.0, 1.0));

    if (uHSLEnabled)
        c = applyHSL(clamp(c, 0.0, 1.0));

    c = applySharpness(uTex, vUV, c, uSharpness, uTexelSize);

    fragColor = vec4(clamp(c, 0.0, 1.0) * uAlpha, t.a * uAlpha);
}
)GLSL";

// ── Shader linker
// ─────────────────────────────────────────────────────────────
static GLuint linkProgram() {
  auto compile = [](GLenum type, const char *src) -> GLuint {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
  };
  GLuint vs = compile(GL_VERTEX_SHADER, kExposureVert);
  GLuint fs = compile(GL_FRAGMENT_SHADER, kExposureFrag);
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return p;
}

static void u1f(GLuint prog, const char *name, float v) {
  glUniform1f(glGetUniformLocation(prog, name), v);
}
static void u2f(GLuint prog, const char *name, float x, float y) {
  glUniform2f(glGetUniformLocation(prog, name), x, y);
}
static void u1i(GLuint prog, const char *name, int v) {
  glUniform1i(glGetUniformLocation(prog, name), v);
}

// ─────────────────────────────────────────────────────────────────────────────
//  ImageSurface
// ─────────────────────────────────────────────────────────────────────────────

class ImageSurface : public RenderSurface {
public:
  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void(bool, const std::string &)> onExportDone;
  // Called on the render thread after a new image is loaded.
  // The app uses this to resize the canvas and fit-to-view.
  std::function<void(int w, int h)> onImageLoaded;

  // ── Called from UI thread ─────────────────────────────────────────────────

  void requestLoad(const std::string &path) {
    {
      std::lock_guard<std::mutex> lk(mx_);
      pending_ = path;
    }
    pendingLoad_.store(true);
  }

  void requestExport(const std::string &path) {
    {
      std::lock_guard<std::mutex> lk(exportMx_);
      exportPath_ = path;
    }
    pendingExport_.store(true);
  }

  // Basic adjustment setters
  void setExposure(float v) { exposure_.store(v, std::memory_order_relaxed); }
  void setContrast(float v) { contrast_.store(v, std::memory_order_relaxed); }
  void setHighlights(float v) {
    highlights_.store(v, std::memory_order_relaxed);
  }
  void setShadows(float v) { shadows_.store(v, std::memory_order_relaxed); }
  void setWhites(float v) { whites_.store(v, std::memory_order_relaxed); }
  void setBlacks(float v) { blacks_.store(v, std::memory_order_relaxed); }
  void setSaturation(float v) {
    saturation_.store(v, std::memory_order_relaxed);
  }
  void setVibrance(float v) { vibrance_.store(v, std::memory_order_relaxed); }
  void setTemperature(float v) {
    temperature_.store(v, std::memory_order_relaxed);
  }
  void setTint(float v) { tint_.store(v, std::memory_order_relaxed); }
  void setSharpness(float v) { sharpness_.store(v, std::memory_order_relaxed); }

  // ── Curve / HSL LUT uploads ───────────────────────────────────────────────

  void uploadCurveLUTs(const std::array<uint8_t, 256> &lutRGB,
                       const std::array<uint8_t, 256> &lutR,
                       const std::array<uint8_t, 256> &lutG,
                       const std::array<uint8_t, 256> &lutB) {
    std::array<uint8_t, 256 * 4> packed{};
    std::copy(lutRGB.begin(), lutRGB.end(), packed.begin() + 0 * 256);
    std::copy(lutR.begin(), lutR.end(), packed.begin() + 1 * 256);
    std::copy(lutG.begin(), lutG.end(), packed.begin() + 2 * 256);
    std::copy(lutB.begin(), lutB.end(), packed.begin() + 3 * 256);
    {
      std::lock_guard<std::mutex> lk(curveMx_);
      pendingCurveLUT_ = packed;
      curveLUTDirty_ = true;
    }
    bool identity = (lutRGB == kIdentityLUT && lutR == kIdentityLUT &&
                     lutG == kIdentityLUT && lutB == kIdentityLUT);
    curveEnabled_.store(!identity, std::memory_order_relaxed);
  }

  void uploadHSLLUTs(const std::array<uint8_t, 360> &hue,
                     const std::array<uint8_t, 360> &sat,
                     const std::array<uint8_t, 360> &lum) {
    {
      std::lock_guard<std::mutex> lk(hslMx_);
      pendingHSLHue_ = hue;
      pendingHSLSat_ = sat;
      pendingHSLLum_ = lum;
      hslLUTDirty_ = true;
    }
    bool identity = true;
    for (int i = 0; i < 360 && identity; ++i)
      if (hue[i] != 128 || sat[i] != 128 || lum[i] != 128)
        identity = false;
    hslEnabled_.store(!identity, std::memory_order_relaxed);
  }

  // ── Histogram readback ────────────────────────────────────────────────────
  void requestHistogramReadback(std::function<void(HistogramData)> cb) {
    std::lock_guard<std::mutex> lk(histCbMx_);
    histCallback_ = std::move(cb);
    histReadbackRequested_.store(true, std::memory_order_relaxed);
  }

  bool needsContinuousRedraw() const override { return false; }

  void initialize(int w, int h) override {
    viewW_ = w;
    viewH_ = h;
    buildShaderAndQuad();
    buildCurveLUTTexture();
    buildHSLLUTTextures();
  }

  void resize(int w, int h) override {
    viewW_ = w;
    viewH_ = h;
  }

  void update(double) override {}

  void render(Canvas2D &ctx) override {
    ctx_ = &ctx;

    // ── Load pending image ──────────────────────────────────────────────
    if (pendingLoad_.load()) {
      pendingLoad_.store(false);
      std::string path;
      {
        std::lock_guard<std::mutex> lk(mx_);
        path = pending_;
      }
      if (img_) {
        ctx_->freeImage(img_);
        img_ = nullptr;
      }
      img_ = ctx_->loadImage(path);
      if (img_) {
        imgW_ = img_->width;
        imgH_ = img_->height;
        rebuildExportFBO(imgW_, imgH_);

        // Notify the app so it can resize the canvas and fit-to-view.
        // This callback is safe to call from the render thread because
        // setCanvasSize / fitToView just write state that tickAndRender
        // will pick up on the next frame (same thread).
        if (onImageLoaded)
          onImageLoaded(imgW_, imgH_);
      }
      histReadbackRequested_.store(true, std::memory_order_relaxed);
    }

    // ── Upload pending curve LUT ────────────────────────────────────────
    {
      std::lock_guard<std::mutex> lk(curveMx_);
      if (curveLUTDirty_ && curveTex_) {
        glBindTexture(GL_TEXTURE_2D, curveTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 4, GL_RED,
                        GL_UNSIGNED_BYTE, pendingCurveLUT_.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        curveLUTDirty_ = false;
      }
    }

    // ── Upload pending HSL LUTs ─────────────────────────────────────────
    {
      std::lock_guard<std::mutex> lk(hslMx_);
      if (hslLUTDirty_) {
        if (hslHueTex_) {
          glBindTexture(GL_TEXTURE_2D, hslHueTex_);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 360, 1, GL_RED,
                          GL_UNSIGNED_BYTE, pendingHSLHue_.data());
        }
        if (hslSatTex_) {
          glBindTexture(GL_TEXTURE_2D, hslSatTex_);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 360, 1, GL_RED,
                          GL_UNSIGNED_BYTE, pendingHSLSat_.data());
        }
        if (hslLumTex_) {
          glBindTexture(GL_TEXTURE_2D, hslLumTex_);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 360, 1, GL_RED,
                          GL_UNSIGNED_BYTE, pendingHSLLum_.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        hslLUTDirty_ = false;
      }
    }

    // ── Export ──────────────────────────────────────────────────────────
    if (pendingExport_.load()) {
      pendingExport_.store(false);
      std::string path;
      {
        std::lock_guard<std::mutex> lk(exportMx_);
        path = exportPath_;
      }
      doExport(path);
    }

    // ── Background — draw in canvas space, large enough to fill any pan ─
    // Because the viewport MVP is already applied through Canvas2D, we draw
    // a huge rect that always covers the visible area regardless of pan/zoom.
    ctx.setFillColor(img_ ? Color::fromRGB(18, 18, 24)
                          : Color::fromRGB(30, 30, 40));
    ctx.fillRect(-32000.f, -32000.f, 64000.f, 64000.f);

    if (!img_ || !img_->texId || !prog_)
      return;

    float iw = float(img_->width);
    float ih = float(img_->height);

    // ── Draw image at canvas origin at its natural pixel size ───────────
    // The CanvasWidget viewport MVP already encodes zoom + pan, so we never
    // need to manually fit-to-screen. The image lives at (0,0)-(imgW,imgH)
    // in canvas space; the viewport transform scales and offsets it.
    renderDx_ = 0.f;
    renderDy_ = 0.f;
    renderDw_ = iw;
    renderDh_ = ih;

    // Retrieve the MVP that tickAndRender built from vp_.zoom/offset.
    float mvp[16];
    {
      GLint curProg = 0;
      glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
      glGetUniformfv(curProg, glGetUniformLocation(curProg, "uMVP"), mvp);
    }

    float verts[] = {
        0.f, 0.f, 0.f, 0.f, iw,  0.f, 1.f, 0.f, iw,  ih,  1.f, 1.f,
        iw,  ih,  1.f, 1.f, 0.f, ih,  0.f, 1.f, 0.f, 0.f, 0.f, 0.f,
    };
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    bindAndDraw(prog_, mvp, img_->texId, iw, ih);

    // Restore texture / VAO state
    for (int unit = 4; unit >= 0; --unit) {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindVertexArray(0);
    glUseProgram(0);

    // ── Histogram readback ──────────────────────────────────────────────
    if (histReadbackRequested_.load(std::memory_order_relaxed)) {
      histReadbackRequested_.store(false, std::memory_order_relaxed);
      doHistogramReadback();
    }
  }

  void destroy() override {
    if (img_ && ctx_) {
      ctx_->freeImage(img_);
      img_ = nullptr;
    }
    ctx_ = nullptr;
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
    if (curveTex_) {
      glDeleteTextures(1, &curveTex_);
      curveTex_ = 0;
    }
    if (hslHueTex_) {
      glDeleteTextures(1, &hslHueTex_);
      hslHueTex_ = 0;
    }
    if (hslSatTex_) {
      glDeleteTextures(1, &hslSatTex_);
      hslSatTex_ = 0;
    }
    if (hslLumTex_) {
      glDeleteTextures(1, &hslLumTex_);
      hslLumTex_ = 0;
    }
    destroyExportFBO();
  }

private:
  Canvas2D *ctx_ = nullptr;
  Canvas2DImage *img_ = nullptr;
  GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
  int viewW_ = 1, viewH_ = 1;
  int imgW_ = 0, imgH_ = 0;

  // Rendered canvas rect (for histogram readback — canvas space)
  float renderDx_ = 0, renderDy_ = 0, renderDw_ = 0, renderDh_ = 0;

  // ── Image load ────────────────────────────────────────────────────────────
  std::mutex mx_;
  std::string pending_;
  std::atomic<bool> pendingLoad_{false};

  // ── Export ────────────────────────────────────────────────────────────────
  std::mutex exportMx_;
  std::string exportPath_;
  std::atomic<bool> pendingExport_{false};

  GLuint exportFBO_ = 0, exportTex_ = 0;
  int exportFBOW_ = 0, exportFBOH_ = 0;

  // ── Basic adjustment atomics ──────────────────────────────────────────────
  std::atomic<float> exposure_{0.f};
  std::atomic<float> contrast_{0.f};
  std::atomic<float> highlights_{0.f};
  std::atomic<float> shadows_{0.f};
  std::atomic<float> whites_{0.f};
  std::atomic<float> blacks_{0.f};
  std::atomic<float> saturation_{0.f};
  std::atomic<float> vibrance_{0.f};
  std::atomic<float> temperature_{0.f};
  std::atomic<float> tint_{0.f};
  std::atomic<float> sharpness_{0.f};

  // ── Curve LUT ─────────────────────────────────────────────────────────────
  GLuint curveTex_ = 0;
  std::mutex curveMx_;
  std::array<uint8_t, 256 * 4> pendingCurveLUT_{};
  bool curveLUTDirty_ = false;
  std::atomic<bool> curveEnabled_{false};

  // ── HSL LUTs ──────────────────────────────────────────────────────────────
  GLuint hslHueTex_ = 0, hslSatTex_ = 0, hslLumTex_ = 0;
  std::mutex hslMx_;
  std::array<uint8_t, 360> pendingHSLHue_{};
  std::array<uint8_t, 360> pendingHSLSat_{};
  std::array<uint8_t, 360> pendingHSLLum_{};
  bool hslLUTDirty_ = false;
  std::atomic<bool> hslEnabled_{false};

  // ── Histogram readback ────────────────────────────────────────────────────
  std::mutex histCbMx_;
  std::function<void(HistogramData)> histCallback_;
  std::atomic<bool> histReadbackRequested_{false};

  static const std::array<uint8_t, 256> kIdentityLUT;

  // ── GL setup ──────────────────────────────────────────────────────────────
  void buildShaderAndQuad() {
    prog_ = linkProgram();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glBindVertexArray(0);
  }

  void buildCurveLUTTexture() {
    glGenTextures(1, &curveTex_);
    glBindTexture(GL_TEXTURE_2D, curveTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::array<uint8_t, 256 * 4> data{};
    for (int row = 0; row < 4; ++row)
      for (int i = 0; i < 256; ++i)
        data[row * 256 + i] = (uint8_t)i;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 4, 0, GL_RED, GL_UNSIGNED_BYTE,
                 data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void buildHSLLUTTextures() {
    auto make360 = [](GLuint &tex, uint8_t fill) {
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      std::array<uint8_t, 360> data{};
      data.fill(fill);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 360, 1, 0, GL_RED, GL_UNSIGNED_BYTE,
                   data.data());
      glBindTexture(GL_TEXTURE_2D, 0);
    };
    make360(hslHueTex_, 128);
    make360(hslSatTex_, 128);
    make360(hslLumTex_, 128);
  }

  void rebuildExportFBO(int w, int h) {
    destroyExportFBO();

    glGenTextures(1, &exportTex_);
    glBindTexture(GL_TEXTURE_2D, exportTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &exportFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, exportFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           exportTex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    exportFBOW_ = w;
    exportFBOH_ = h;
  }

  void destroyExportFBO() {
    if (exportFBO_) {
      glDeleteFramebuffers(1, &exportFBO_);
      exportFBO_ = 0;
    }
    if (exportTex_) {
      glDeleteTextures(1, &exportTex_);
      exportTex_ = 0;
    }
    exportFBOW_ = exportFBOH_ = 0;
  }

  void bindAndDraw(GLuint prog, const float mvp[16], GLuint imgTex, float iw,
                   float ih) {
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uMVP"), 1, GL_FALSE, mvp);

    u1f(prog, "uExposure", exposure_.load(std::memory_order_relaxed));
    u1f(prog, "uContrast", contrast_.load(std::memory_order_relaxed));
    u1f(prog, "uHighlights", highlights_.load(std::memory_order_relaxed));
    u1f(prog, "uShadows", shadows_.load(std::memory_order_relaxed));
    u1f(prog, "uWhites", whites_.load(std::memory_order_relaxed));
    u1f(prog, "uBlacks", blacks_.load(std::memory_order_relaxed));
    u1f(prog, "uSaturation", saturation_.load(std::memory_order_relaxed));
    u1f(prog, "uVibrance", vibrance_.load(std::memory_order_relaxed));
    u1f(prog, "uTemperature", temperature_.load(std::memory_order_relaxed));
    u1f(prog, "uTint", tint_.load(std::memory_order_relaxed));
    u1f(prog, "uSharpness", sharpness_.load(std::memory_order_relaxed));
    u2f(prog, "uTexelSize", 1.f / iw, 1.f / ih);
    u1f(prog, "uAlpha", 1.f);

    u1i(prog, "uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, imgTex);

    u1i(prog, "uCurveLUT", 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, curveTex_);
    u1i(prog, "uCurveEnabled",
        curveEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    u1i(prog, "uHSLHue", 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, hslHueTex_);

    u1i(prog, "uHSLSat", 3);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, hslSatTex_);

    u1i(prog, "uHSLLum", 4);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, hslLumTex_);

    u1i(prog, "uHSLEnabled",
        hslEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }

  // ── Export: render at full image resolution → readback → write file ───────
  void doExport(const std::string &path) {
    if (!img_ || !img_->texId || !exportFBO_ || imgW_ <= 0 || imgH_ <= 0) {
      if (onExportDone)
        onExportDone(false, "No image loaded.");
      return;
    }

    GLint prevFBO = 0, prevViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    float iw = float(imgW_), ih = float(imgH_);

    // Same convention as tickAndRender: top-left origin, Y down.
    // ortho(left, right, bottom, top) → here bottom=ih, top=0 so Y goes down.
    float mvp[16];
    glutil::ortho(0.f, iw, ih, 0.f, mvp);

    // Normal UVs — no flip needed because MVP and glReadPixels
    // both agree on the same Y convention now.
    float verts[] = {
        0.f, 0.f, 0.f, 0.f, iw,  0.f, 1.f, 0.f, iw,  ih,  1.f, 1.f,
        iw,  ih,  1.f, 1.f, 0.f, ih,  0.f, 1.f, 0.f, 0.f, 0.f, 0.f,
    };
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glBindFramebuffer(GL_FRAMEBUFFER, exportFBO_);
    glViewport(0, 0, imgW_, imgH_);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    bindAndDraw(prog_, mvp, img_->texId, iw, ih);

    // glReadPixels reads bottom-to-top; our MVP renders top-to-bottom,
    // so we must flip rows to get the correct top-left order for file writers.
    std::vector<uint8_t> raw(size_t(imgW_) * imgH_ * 4);
    glReadPixels(0, 0, imgW_, imgH_, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());

    size_t rowBytes = size_t(imgW_) * 4;
    std::vector<uint8_t> rowTmp(rowBytes);
    for (int top = 0, bot = imgH_ - 1; top < bot; ++top, --bot) {
      uint8_t *rowTop = raw.data() + top * rowBytes;
      uint8_t *rowBot = raw.data() + bot * rowBytes;
      std::memcpy(rowTmp.data(), rowTop, rowBytes);
      std::memcpy(rowTop, rowBot, rowBytes);
      std::memcpy(rowBot, rowTmp.data(), rowBytes);
    }

    // Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2],
               prevViewport[3]);
    for (int unit = 4; unit >= 0; --unit) {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindVertexArray(0);
    glUseProgram(0);

    // Write file
    bool ok = false;
    std::string ext;
    auto dotPos = path.rfind('.');
    if (dotPos != std::string::npos)
      ext = path.substr(dotPos);
    for (auto &c : ext)
      c = char(std::tolower(c));

    if (ext == ".jpg" || ext == ".jpeg")
      ok = stbi_write_jpg(path.c_str(), imgW_, imgH_, 4, raw.data(), 95) != 0;
    else if (ext == ".bmp")
      ok = stbi_write_bmp(path.c_str(), imgW_, imgH_, 4, raw.data()) != 0;
    else
      ok = stbi_write_png(path.c_str(), imgW_, imgH_, 4, raw.data(),
                          imgW_ * 4) != 0;

    if (onExportDone)
      onExportDone(ok, ok ? "Export saved." : "Export failed.");
  }

  void doHistogramReadback() {
    std::function<void(HistogramData)> cb;
    {
      std::lock_guard<std::mutex> lk(histCbMx_);
      cb = histCallback_;
    }
    if (!cb)
      return;

    // Sample the whole canvas area — the rendered image fills it after zoom.
    // viewW_/viewH_ are the GL surface dimensions set by initialize/resize.
    int fbW = std::max(1, viewW_);
    int fbH = std::max(1, viewH_);

    // Stride so we sample at most 256×256 worth of pixels for perf.
    int strideX = std::max(1, fbW / 256);
    int strideY = std::max(1, fbH / 256);
    int readW = (fbW + strideX - 1) / strideX;
    int readH = (fbH + strideY - 1) / strideY;

    std::vector<uint8_t> pixels(readW * readH * 4);
    std::vector<uint8_t> rowBuf(fbW * 4);
    int dstRow = 0;
    for (int srcRow = 0; srcRow < fbH && dstRow < readH; srcRow += strideY) {
      int glY = fbH - srcRow - 1; // GL origin is bottom-left
      glReadPixels(0, glY, fbW, 1, GL_RGBA, GL_UNSIGNED_BYTE, rowBuf.data());
      uint8_t *dst = pixels.data() + dstRow * readW * 4;
      for (int col = 0, dstCol = 0; col < fbW && dstCol < readW;
           col += strideX, ++dstCol) {
        dst[dstCol * 4 + 0] = rowBuf[col * 4 + 0];
        dst[dstCol * 4 + 1] = rowBuf[col * 4 + 1];
        dst[dstCol * 4 + 2] = rowBuf[col * 4 + 2];
        dst[dstCol * 4 + 3] = rowBuf[col * 4 + 3];
      }
      ++dstRow;
    }

    HistogramData hist =
        HistogramData::fromPixels(pixels.data(), readW * dstRow, 4);
    cb(std::move(hist));
  }
};

const std::array<uint8_t, 256> ImageSurface::kIdentityLUT = []() {
  std::array<uint8_t, 256> a{};
  for (int i = 0; i < 256; ++i)
    a[i] = (uint8_t)i;
  return a;
}();

// ─────────────────────────────────────────────────────────────────────────────
//  LightRoomApp
// ─────────────────────────────────────────────────────────────────────────────

class LightRoomApp : public Widget {
  State<std::string> filePath{""};
  State<std::string> statusMsg{"Open an image to begin."};

  State<double> sExposure{0.0};
  State<double> sContrast{0.0};
  State<double> sHighlights{0.0};
  State<double> sShadows{0.0};
  State<double> sWhites{0.0};
  State<double> sBlacks{0.0};
  State<double> sSaturation{0.0};
  State<double> sVibrance{0.0};
  State<double> sTemperature{0.0};
  State<double> sTint{0.0};
  State<double> sSharpness{0.0};

  State<HistogramData> histState{HistogramData{}};
  State<ToneCurveData> curveState{ToneCurveData{}};
  State<HSLData> hslState{HSLData{}};

  // Zoom level display (e.g. "100%"), updated via onViewportChanged callback.
  State<std::string> zoomLabel{"100%"};

  static constexpr Color kColorOk = Color::fromRGB(148, 226, 213);
  static constexpr Color kColorErr = Color::fromRGB(243, 139, 168);
  State<Color> statusColor{kColorOk};

public:
  WidgetPtr build() override {
    auto canvas = std::make_shared<CanvasWidget>();

    // ── Enable viewport so MMB pan and scroll-zoom work natively ─────────
    // The CanvasWidget already handles:
    //   • MMB hold + drag  → pan
    //   • Scroll wheel     → pan vertically
    //   • Ctrl + scroll    → zoom toward cursor  (zoom limits applied below)
    //   • Ctrl + +/-/0     → zoom in / zoom out / reset
    canvas->setViewportEnabled(true);
    canvas->setScrollbarsEnabled(false); // clean look, no scrollbars

    auto surface = canvas->setSurface<ImageSurface>();
    std::weak_ptr<ImageSurface> ws = surface;
    std::weak_ptr<CanvasWidget> wc = canvas;

    // ── Zoom label ───────────────────────────────────────────────────────
    // onViewportChanged fires every time the zoom level changes.
    canvas->onViewportChanged = [this](float zoom) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.0f%%", zoom * 100.f);
      zoomLabel.set(buf);
    };

    // ── Wire export result back to status bar ────────────────────────────
    surface->onExportDone = [this](bool ok, const std::string &msg) {
      statusMsg.set(msg);
      statusColor.set(ok ? kColorOk : kColorErr);
    };

    // ── Wire image-loaded callback: resize canvas + fit to view ──────────
    // This gives the Lightroom-style "fit image to window" on open.
    surface->onImageLoaded = [wc](int w, int h) {
      if (auto c = wc.lock()) {
        // Tell the viewport the canvas extent matches the image pixels.
        c->setCanvasSize(w, h);
        // Fit the image to the current view (zoom to fit, centered).
        c->viewport().fitToView();
      }
    };

    // ── Histogram refresh ────────────────────────────────────────────────
    surface->requestHistogramReadback(
        [this](HistogramData hd) { histState.set(std::move(hd)); });

    auto refreshHistogram = [ws, this]() {
      if (auto s = ws.lock())
        s->requestHistogramReadback(
            [this](HistogramData hd) { histState.set(std::move(hd)); });
    };

    // ── Curve LUTs ───────────────────────────────────────────────────────
    auto pushCurveLUTs = [ws, this, refreshHistogram](const ToneCurveData &d) {
      curveState.set(d);
      if (auto s = ws.lock())
        s->uploadCurveLUTs(d.rgb.buildLUT(), d.r.buildLUT(), d.g.buildLUT(),
                           d.b.buildLUT());
      refreshHistogram();
    };

    // ── HSL LUTs ─────────────────────────────────────────────────────────
    auto pushHSLLUTs = [ws, this, refreshHistogram](const HSLData &d) {
      hslState.set(d);
      if (auto s = ws.lock()) {
        auto gpu = d.buildGPULUTs();
        s->uploadHSLLUTs(gpu.hue, gpu.sat, gpu.lum);
      }
      refreshHistogram();
    };

    // ── Slider builder ────────────────────────────────────────────────────
    auto sliderCol = [](const char *label, double lo, double hi, double step,
                        State<double> &st,
                        std::function<void(double)> onChange) -> WidgetPtr {
      return Column({
                        Text(label)->setFontSize(11)->setTextColor(
                            Color::fromRGB(180, 180, 200)),
                        Slider(lo, hi, step)
                            ->setValue(st)
                            ->setOnValueChanged([onChange, &st](double v) {
                              st.set(v);
                              onChange(v);
                            }),
                    })
          ->setSpacing(4)
          ->setCrossAxisAlignment(CrossAxisAlignment::Start)
          ->setMainAxisSize(MainAxisSize::Min);
    };

    auto sectionLabel = [](const char *title) -> WidgetPtr {
      return Text(title)->setFontSize(10)->setTextColor(
          Color::fromRGB(110, 110, 140));
    };

#define SLIDER_CB(setter)                                                      \
  [ws, refreshHistogram](double v) {                                           \
    if (auto s = ws.lock())                                                    \
      s->setter(float(v));                                                     \
    refreshHistogram();                                                        \
  }

    // ── Zoom controls (fit / 1:1 / reset) ────────────────────────────────
    auto zoomFitBtn = Button("Fit")->setOnClick([wc]() {
      if (auto c = wc.lock()) {
        c->viewport().fitToView();
      }
    });

    auto zoom1x1Btn = Button("1:1")->setOnClick([wc]() {
      if (auto c = wc.lock()) {
        c->viewport().resetZoom(); // zoom = 1.0, centered
      }
    });

    return Scaffold(
        nullptr,
        Expanded(Center(
            Container(
                Column(
                    {
                        // ── File picker + zoom controls row
                        // ───────────────────
                        Row({
                                // Open
                                FilePicker()
                                    ->setMode(FilePickerMode::Open)
                                    ->addFilter("Images", {"*.png", "*.jpg",
                                                           "*.jpeg", "*.bmp"})
                                    ->addFilter("All Files", {"*.*"})
                                    ->setShowPath(true)
                                    ->bindPath(filePath)
                                    ->setOnChanged(
                                        [ws, refreshHistogram,
                                         this](const std::string &path) {
                                          statusMsg.set("Loading…");
                                          statusColor.set(kColorOk);
                                          if (auto s = ws.lock())
                                            s->requestLoad(path);
                                          refreshHistogram();
                                        }),

                                // Export
                                FilePicker("💾 Export")
                                    ->setMode(FilePickerMode::Save)
                                    ->setTitle("Export edited image")
                                    ->setDefaultFilename("edited.png")
                                    ->addFilter("PNG", {"*.png"})
                                    ->addFilter("JPEG", {"*.jpg", "*.jpeg"})
                                    ->addFilter("BMP", {"*.bmp"})
                                    ->setDefaultExtension("png")
                                    ->setShowPath(false)
                                    ->setOnChanged(
                                        [ws, this](const std::string &path) {
                                          statusMsg.set("Exporting…");
                                          statusColor.set(kColorOk);
                                          if (auto s = ws.lock())
                                            s->requestExport(path);
                                        }),

                                // Fit-to-view button
                                zoomFitBtn,

                                // 1:1 pixel button
                                zoom1x1Btn,

                                // Live zoom percentage label
                                Text(zoomLabel,
                                     [](const std::string &s) { return s; })
                                    ->setFontSize(10)
                                    ->setTextColor(
                                        Color::fromRGB(140, 140, 160)),

                                // Status indicator
                                Text(statusMsg,
                                     [](const std::string &s) { return s; })
                                    ->setFontSize(10)
                                    ->setTextColor(statusColor.get()),
                            })
                            ->setCrossAxisAlignment(CrossAxisAlignment::Center)
                            ->setSpacing(12),

                        // ── Main row: canvas + sidebar
                        // ────────────────────────
                        Expanded(Container(Row({
                            Expanded(canvas),
                            // ── Sidebar
                            // ───────────────────────────────────────
                            Container(
                                ScrollView({
                                    HSLPanel(258)
                                        ->setData(hslState)
                                        ->setActiveTab(HSLTab::All)
                                        ->setOnHSLChanged(pushHSLLUTs),

                                    ToneCurve(258, 220)
                                        ->setShowHistogram(true)
                                        ->setShowRegions(true)
                                        ->setShowGrid(true)
                                        ->setCurveData(curveState)
                                        ->setOnCurveChanged(pushCurveLUTs),

                                    Histogram(258, 90)
                                        ->setData(histState)
                                        ->setMode(HistogramMode::RGB)
                                        ->setShowGrid(true)
                                        ->setShowClip(true)
                                        ->setShowChannelToggles(true)
                                        ->setLogScale(false)
                                        ->setBgColor(Color::fromRGB(14, 14, 22))
                                        ->setOnZoneClicked([this](float pos) {
                                          sExposure.set(std::max(
                                              -5.0, std::min(5.0, (pos - 0.5) *
                                                                      4.0)));
                                        }),

                                    // LIGHT
                                    sectionLabel("LIGHT"),
                                    sliderCol("Exposure", -3.0, 3.0, 0.1,
                                              sExposure,
                                              SLIDER_CB(setExposure)),
                                    sliderCol("Contrast", -1.0, 1.0, 0.05,
                                              sContrast,
                                              SLIDER_CB(setContrast)),
                                    sliderCol("Highlights", -1.0, 1.0, 0.05,
                                              sHighlights,
                                              SLIDER_CB(setHighlights)),
                                    sliderCol("Shadows", -1.0, 1.0, 0.05,
                                              sShadows, SLIDER_CB(setShadows)),
                                    sliderCol("Whites", -1.0, 1.0, 0.05,
                                              sWhites, SLIDER_CB(setWhites)),
                                    sliderCol("Blacks", -1.0, 1.0, 0.05,
                                              sBlacks, SLIDER_CB(setBlacks)),

                                    // COLOR
                                    sectionLabel("COLOR"),
                                    sliderCol("Temperature", -1.0, 1.0, 0.05,
                                              sTemperature,
                                              SLIDER_CB(setTemperature)),
                                    sliderCol("Tint", -1.0, 1.0, 0.05, sTint,
                                              SLIDER_CB(setTint)),
                                    sliderCol("Vibrance", -1.0, 1.0, 0.05,
                                              sVibrance,
                                              SLIDER_CB(setVibrance)),
                                    sliderCol("Saturation", -1.0, 1.0, 0.05,
                                              sSaturation,
                                              SLIDER_CB(setSaturation)),

                                    // DETAIL
                                    sectionLabel("DETAIL"),
                                    sliderCol("Sharpness", 0.0, 1.0, 0.05,
                                              sSharpness,
                                              SLIDER_CB(setSharpness)),
                                }))
                                ->setPadding(10)
                                ->setWidth(250),
                        }))),
                    })
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                ->setBorderRadius(10))),
        nullptr, nullptr);

#undef SLIDER_CB
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Light Room App", std::make_shared<LightRoomApp>(),
                 AppTheme::dark(), false, 1000, 700, false, true);
}