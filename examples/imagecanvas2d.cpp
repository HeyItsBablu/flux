#include "flux/flux.hpp"
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
uniform float uExposure;    // EV stops: pow(2, uExposure)
uniform float uContrast;    // -1..1
uniform float uHighlights;  // -1..1
uniform float uShadows;     // -1..1
uniform float uWhites;      // -1..1
uniform float uBlacks;      // -1..1
uniform float uSaturation;  // -1..1
uniform float uVibrance;    // -1..1
uniform float uTemperature; // -1..1
uniform float uTint;        // -1..1
uniform float uSharpness;   // 0..1
uniform vec2  uTexelSize;
uniform float uAlpha;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

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

// ─────────────────────────────────────────────────────────────────────────────
class ImageSurface : public RenderSurface {
public:
  void requestLoad(const std::string &path) {
    {
      std::lock_guard<std::mutex> lk(mx_);
      pending_ = path;
    }
    pendingLoad_.store(true);
  }

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

  bool needsContinuousRedraw() const override { return false; }

  void initialize(int w, int h) override {
    viewW_ = w;
    viewH_ = h;
    buildShaderAndQuad();
  }
  void resize(int w, int h) override {
    viewW_ = w;
    viewH_ = h;
  }
  void update(double) override {}

  void render(Canvas2D &ctx) override {
    ctx_ = &ctx;

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
    }

    float cw = float(ctx.width());
    float ch = float(ctx.height());

    ctx.setFillColor(img_ ? Color::fromRGB(18, 18, 24)
                          : Color::fromRGB(30, 30, 40));
    ctx.fillRect(0, 0, cw, ch);

    if (!img_ || !img_->texId || !prog_)
      return;

    float iw = float(img_->width);
    float ih = float(img_->height);
    float scale = std::min(cw / iw, ch / ih);
    float dw = iw * scale, dh = ih * scale;
    float dx = (cw - dw) * 0.5f, dy = (ch - dh) * 0.5f;

    float mvp[16];
    {
      GLint curProg = 0;
      glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
      glGetUniformfv(curProg, glGetUniformLocation(curProg, "uMVP"), mvp);
    }

    float verts[] = {
        dx,      dy,      0.f, 0.f, dx + dw, dy,      1.f, 0.f,
        dx + dw, dy + dh, 1.f, 1.f, dx + dw, dy + dh, 1.f, 1.f,
        dx,      dy + dh, 0.f, 1.f, dx,      dy,      0.f, 0.f,
    };
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glUseProgram(prog_);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "uMVP"), 1, GL_FALSE, mvp);
    u1f(prog_, "uExposure", exposure_.load(std::memory_order_relaxed));
    u1f(prog_, "uContrast", contrast_.load(std::memory_order_relaxed));
    u1f(prog_, "uHighlights", highlights_.load(std::memory_order_relaxed));
    u1f(prog_, "uShadows", shadows_.load(std::memory_order_relaxed));
    u1f(prog_, "uWhites", whites_.load(std::memory_order_relaxed));
    u1f(prog_, "uBlacks", blacks_.load(std::memory_order_relaxed));
    u1f(prog_, "uSaturation", saturation_.load(std::memory_order_relaxed));
    u1f(prog_, "uVibrance", vibrance_.load(std::memory_order_relaxed));
    u1f(prog_, "uTemperature", temperature_.load(std::memory_order_relaxed));
    u1f(prog_, "uTint", tint_.load(std::memory_order_relaxed));
    u1f(prog_, "uSharpness", sharpness_.load(std::memory_order_relaxed));
    u2f(prog_, "uTexelSize", 1.f / iw, 1.f / ih);
    u1f(prog_, "uAlpha", 1.f);
    glUniform1i(glGetUniformLocation(prog_, "uTex"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, img_->texId);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
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
  }

private:
  Canvas2D *ctx_ = nullptr;
  Canvas2DImage *img_ = nullptr;
  GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
  int viewW_ = 1, viewH_ = 1;

  std::mutex mx_;
  std::string pending_;
  std::atomic<bool> pendingLoad_{false};

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
};

// ─────────────────────────────────────────────────────────────────────────────
class ImageApp : public Widget {
  State<std::string> filePath{""};

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

public:
  WidgetPtr build() override {
    auto canvas = std::make_shared<CanvasWidget>();
    canvas->setViewportEnabled(false);
    canvas->setScrollbarsEnabled(false);
    auto surface = canvas->setSurface<ImageSurface>();
    std::weak_ptr<ImageSurface> ws = surface;

    // ── One labelled slider, matching the example Column pattern ──────
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

    // ── Dimmed section label ──────────────────────────────────────────
    auto sectionLabel = [](const char *title) -> WidgetPtr {
      return Text(title)->setFontSize(10)->setTextColor(
          Color::fromRGB(110, 110, 140));
    };

    return Scaffold(
        nullptr,
        Expanded(Center(
            Container(
                Column({
                           // ── File picker row ───────────────────────────────
                           Row({
                                   FilePicker()
                                       ->setMode(FilePickerMode::Open)
                                       ->addFilter("Images",
                                                   {"*.png", "*.jpg", "*.jpeg",
                                                    "*.bmp"})
                                       ->addFilter("All Files", {"*.*"})
                                       ->setShowPath(true)
                                       ->bindPath(filePath)
                                       ->setOnChanged(
                                           [ws](const std::string &path) {
                                             if (auto s = ws.lock())
                                               s->requestLoad(path);
                                           }),
                               })
                               ->setCrossAxisAlignment(
                                   CrossAxisAlignment::Center)
                               ->setSpacing(12),

                           // ── Main row: sidebar + canvas ────────────────────
                           Expanded(Container(Row({

                               // ── Sidebar ───────────────────────────────────
                               Container(ListView({

                                             // LIGHT
                                             sectionLabel("LIGHT"),
                                             sliderCol("Exposure", -3.0, 3.0,
                                                       0.1, sExposure,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setExposure(
                                                               float(v));
                                                       }),
                                             sliderCol("Contrast", -1.0, 1.0,
                                                       0.05, sContrast,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setContrast(
                                                               float(v));
                                                       }),
                                             sliderCol("Highlights", -1.0, 1.0,
                                                       0.05, sHighlights,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setHighlights(
                                                               float(v));
                                                       }),
                                             sliderCol("Shadows", -1.0, 1.0,
                                                       0.05, sShadows,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setShadows(
                                                               float(v));
                                                       }),
                                             sliderCol("Whites", -1.0, 1.0,
                                                       0.05, sWhites,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setWhites(
                                                               float(v));
                                                       }),
                                             sliderCol("Blacks", -1.0, 1.0,
                                                       0.05, sBlacks,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setBlacks(
                                                               float(v));
                                                       }),

                                             // COLOR
                                             sectionLabel("COLOR"),
                                             sliderCol("Temperature", -1.0, 1.0,
                                                       0.05, sTemperature,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setTemperature(
                                                               float(v));
                                                       }),
                                             sliderCol(
                                                 "Tint", -1.0, 1.0, 0.05, sTint,
                                                 [ws](double v) {
                                                   if (auto s = ws.lock())
                                                     s->setTint(float(v));
                                                 }),
                                             sliderCol("Vibrance", -1.0, 1.0,
                                                       0.05, sVibrance,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setVibrance(
                                                               float(v));
                                                       }),
                                             sliderCol("Saturation", -1.0, 1.0,
                                                       0.05, sSaturation,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setSaturation(
                                                               float(v));
                                                       }),

                                             // DETAIL
                                             sectionLabel("DETAIL"),
                                             sliderCol("Sharpness", 0.0, 1.0,
                                                       0.05, sSharpness,
                                                       [ws](double v) {
                                                         if (auto s = ws.lock())
                                                           s->setSharpness(
                                                               float(v));
                                                       }),

                                         }))
                                         ->setPadding(10)
                                   ->setWidth(250),

     
                               Expanded(canvas),
                           }))),
                       })
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                ->setBorderRadius(10))),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Image Viewer", std::make_shared<ImageApp>(), AppTheme::dark(),
                 false, 1000, 700, false, true);
}