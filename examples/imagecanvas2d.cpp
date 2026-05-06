#include "flux/flux.hpp"
#include <atomic>
#include <mutex>
#include <glad/glad.h>

// ── Shader sources ────────────────────────────────────────────────────────────
// We own our own shader program so we never touch Canvas2D internals.

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
uniform float     uExposure; // EV stops: pow(2, uExposure) multiplier
uniform float     uAlpha;
void main(){
    vec4 t = texture(uTex, vUV);
    float ev = pow(2.0, uExposure);
    fragColor = vec4(clamp(t.rgb * ev, 0.0, 1.0) * uAlpha, t.a * uAlpha);
}
)GLSL";

// ── Minimal shader linker (avoids depending on flux_glutil.hpp internals) ─────
static GLuint linkExposureProgram() {
    auto compile = [](GLenum type, const char *src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER,   kExposureVert);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kExposureFrag);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
class ImageSurface : public RenderSurface {
public:
    // Called from UI thread — safe via atomic.
    void requestLoad(const std::string &path) {
        {
            std::lock_guard<std::mutex> lk(mx_);
            pending_ = path;
        }
        pendingLoad_.store(true);
    }

    // Called from UI thread — atomic float write is fine.
    void setExposure(float ev) {
        exposure_.store(ev, std::memory_order_relaxed);
    }

    bool needsContinuousRedraw() const override { return false; }

    // ── RenderSurface ─────────────────────────────────────────────────────────

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

        // ── Handle pending image load (GL thread) ─────────────────────────
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

        // ── Background ────────────────────────────────────────────────────
        ctx.setFillColor(img_ ? Color::fromRGB(18, 18, 24)
                               : Color::fromRGB(30, 30, 40));
        ctx.fillRect(0, 0, cw, ch);

        if (!img_ || !img_->texId || !prog_)
            return;

        // ── Compute aspect-fit rect ───────────────────────────────────────
        float iw    = float(img_->width);
        float ih    = float(img_->height);
        float scale = std::min(cw / iw, ch / ih);
        float dw    = iw * scale;
        float dh    = ih * scale;
        float dx    = (cw - dw) * 0.5f;
        float dy    = (ch - dh) * 0.5f;

        // ── Build the same MVP Canvas2D would use ─────────────────────────
        // Canvas2D draws with an ortho projection covering (0,0)..(canvasW,canvasH).
        // We need the same matrix so our quad lands in the right place.
        // Grab it from the currently bound program before we switch.
        float mvp[16];
        {
            // Canvas2D has already called glUseProgram(flatProg) for the
            // fillRect above, so we can query the uniform from that program.
            GLint curProg = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
            glGetUniformfv(curProg, glGetUniformLocation(curProg, "uMVP"), mvp);
        }

        // ── Upload quad covering the fit rect ─────────────────────────────
        // vertices: x, y, u, v  (2 triangles)
        float verts[] = {
            dx,      dy,      0.f, 0.f,
            dx + dw, dy,      1.f, 0.f,
            dx + dw, dy + dh, 1.f, 1.f,
            dx + dw, dy + dh, 1.f, 1.f,
            dx,      dy + dh, 0.f, 1.f,
            dx,      dy,      0.f, 0.f,
        };
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        // ── Draw with our exposure shader ─────────────────────────────────
        glUseProgram(prog_);
        glUniformMatrix4fv(glGetUniformLocation(prog_, "uMVP"),      1, GL_FALSE, mvp);
        glUniform1f(glGetUniformLocation(prog_, "uExposure"), exposure_.load(std::memory_order_relaxed));
        glUniform1f(glGetUniformLocation(prog_, "uAlpha"),    1.f);
        glUniform1i(glGetUniformLocation(prog_, "uTex"),      0);

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

        if (prog_)  { glDeleteProgram(prog_);            prog_  = 0; }
        if (vao_)   { glDeleteVertexArrays(1, &vao_);   vao_   = 0; }
        if (vbo_)   { glDeleteBuffers(1, &vbo_);         vbo_   = 0; }
    }

private:
    // ── GL resources (GL thread only) ─────────────────────────────────────
    Canvas2D       *ctx_  = nullptr;
    Canvas2DImage  *img_  = nullptr;
    GLuint          prog_ = 0;
    GLuint          vao_  = 0;
    GLuint          vbo_  = 0;
    int             viewW_ = 1, viewH_ = 1;

    // ── Cross-thread state ────────────────────────────────────────────────
    std::mutex           mx_;
    std::string          pending_;
    std::atomic<bool>    pendingLoad_{false};
    std::atomic<float>   exposure_{0.f};   // EV stops, 0 = no change

    void buildShaderAndQuad() {
        prog_ = linkExposureProgram();

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        // Pre-allocate for 6 vertices × 4 floats, filled each frame
        glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        // aPos  — layout location 0
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        // aUV   — layout location 1
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
class ImageApp : public Widget {
    State<std::string> filePath{""};
    State<double>      sliderState{0.0};   // 0 EV = no change

public:
    WidgetPtr build() override {
        auto canvas = std::make_shared<CanvasWidget>();
        canvas->setViewportEnabled(false);
        canvas->setScrollbarsEnabled(false);
        auto surface = canvas->setSurface<ImageSurface>();

        std::weak_ptr<ImageSurface> weakSurface = surface;

        return Scaffold(
            AppBar("Image Viewer"),
            Expanded(Center(
                Container(
                    Column({
                        // ── Top row: file picker ──────────────────────────
                        Row({
                            FilePicker()
                                ->setMode(FilePickerMode::Open)
                                ->addFilter("Images", {"*.png", "*.jpg", "*.jpeg", "*.bmp"})
                                ->addFilter("All Files", {"*.*"})
                                ->setShowPath(true)
                                ->bindPath(filePath)
                                ->setOnChanged([weakSurface](const std::string &path) {
                                    if (auto s = weakSurface.lock())
                                        s->requestLoad(path);
                                }),
                        })
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center)
                        ->setSpacing(12),

                        // ── Main row: exposure panel + canvas ─────────────
                        Expanded(Container(Row({
                            // Sidebar
                            Container(ListView({
                                Text("Exposure")
                                    ->setFontSize(11)
                                    ->setTextColor(Color::fromRGB(180, 180, 200)),
                                Slider(-3.0, 3.0, 0.1)
                                    ->setValue(sliderState)
                                    ->setOnValueChanged([weakSurface, this](double v) {
                                        sliderState.set(v);
                                        if (auto s = weakSurface.lock())
                                            s->setExposure(float(v));
                                    }),
                            }))
                            ->setWidth(100),

                            // Canvas fills remaining space
                            Expanded(canvas),
                        }))),
                    })
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center)
                )
                ->setBorderRadius(10)
            )),
            nullptr, nullptr
        );
    }
};

WidgetPtr createApp(FluxUI *app) {
    return FluxApp("Image Viewer", std::make_shared<ImageApp>(), AppTheme::dark(),
                   false, 900, 700, false, false);
}