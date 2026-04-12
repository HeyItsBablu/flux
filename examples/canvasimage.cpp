#include "flux/flux.hpp"
#include "stb_image.h"
#include "stb_image_write.h"

// ============================================================================
// ImageSurface
// ============================================================================

static const char* kVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main(){
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* kFrag = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform float uExposure;
uniform float uContrast;

vec3 toLinear(vec3 c){ return pow(clamp(c,0.0,1.0), vec3(2.2)); }
vec3 toSRGB  (vec3 c){ return pow(clamp(c,0.0,1.0), vec3(1.0/2.2)); }

void main(){
    vec3 c = texture(uTex, vUV).rgb;
    c = toLinear(c);
    c *= pow(2.0, uExposure);
    float pivot = 0.18;
    c = (c - pivot) * (1.0 + uContrast) + pivot;
    c = clamp(c, 0.0, 1.0);
    c = toSRGB(c);
    fragColor = vec4(c, 1.0);
}
)GLSL";

class ImageSurface : public RenderSurface
{
public:
    std::function<void()>           onLoaded;
    std::function<void(const char*)> onStatus;

    void loadImage(const std::string& path) { pendingPath_ = path; }
    void setExposure(float v) { exposure_ = v; }
    void setContrast(float v) { contrast_ = v; }
    int  imgW() const { return imgW_; }
    int  imgH() const { return imgH_; }
    bool hasImage() const { return tex_ != 0; }

    // ── Export ───────────────────────────────────────────────────────────────
    bool exportImage(const std::string& path)
    {
        if (!tex_) return false;
        //FluxGLLinux::GLContext::instance().makeCurrent();

        // 1. Build an ortho MVP that maps [0,imgW_]x[0,imgH_] → NDC
        float mvp[16];
        buildExportMVP(mvp);

        // 2. Render into export FBO at full image resolution
        glBindFramebuffer(GL_FRAMEBUFFER, exportFBO_);
        glViewport(0, 0, imgW_, imgH_);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        drawScene(mvp);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 3. Read pixels
        std::vector<uint8_t> buf(size_t(imgW_) * imgH_ * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, exportFBO_);
        glReadPixels(0, 0, imgW_, imgH_, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        // 4. Flip vertically (GL origin is bottom-left)
        int stride = imgW_ * 4;
        std::vector<uint8_t> flipped(buf.size());
        for (int r = 0; r < imgH_; ++r)
            memcpy(flipped.data() + r * stride,
                   buf.data() + (imgH_ - 1 - r) * stride, stride);

        // 5. Write file
        bool ok = false;
        std::string ext = path.substr(path.rfind('.'));
        // lowercase ext
        for (auto& c : ext) c = char(tolower(c));

        if (ext == ".jpg" || ext == ".jpeg")
        {
            ok = stbi_write_jpg(path.c_str(), imgW_, imgH_, 4,
                                flipped.data(), 95) != 0;
        }
        else
        {
            ok = stbi_write_png(path.c_str(), imgW_, imgH_, 4,
                                flipped.data(), stride) != 0;
        }

        if (onStatus) onStatus(ok ? "Export saved." : "Export failed.");
        return ok;
    }

    // ── RenderSurface ─────────────────────────────────────────────────────────
    void initialize(int, int) override
    {
        prog_ = glutil::linkProgram(kVert, kFrag);
        buildQuad();
    }

    void resize(int, int) override {}

    void update(double) override
    {
        if (!pendingPath_.empty())
        {
            uploadFromFile(pendingPath_);
            pendingPath_.clear();
        }
    }

    void render(const float mvp[16]) override
    {
        glClearColor(0.08f, 0.08f, 0.10f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!tex_) return;
        drawScene(mvp);
    }

    void destroy() override
    {
        if (prog_)      { glDeleteProgram(prog_);          prog_      = 0; }
        if (vao_)       { glDeleteVertexArrays(1, &vao_);  vao_       = 0; }
        if (vbo_)       { glDeleteBuffers(1, &vbo_);       vbo_       = 0; }
        if (tex_)       { glDeleteTextures(1, &tex_);      tex_       = 0; }
        if (exportTex_) { glDeleteTextures(1, &exportTex_); exportTex_ = 0; }
        if (exportFBO_) { glDeleteFramebuffers(1, &exportFBO_); exportFBO_ = 0; }
    }

private:
    std::string pendingPath_;
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0, tex_ = 0;
    GLuint exportFBO_ = 0, exportTex_ = 0;
    int    imgW_ = 0, imgH_ = 0;
    float  exposure_ = 0.f, contrast_ = 0.f;

    // ── Draw ──────────────────────────────────────────────────────────────────
    void drawScene(const float mvp[16])
    {
        glUseProgram(prog_);
        glUniformMatrix4fv(glGetUniformLocation(prog_, "uMVP"), 1, GL_FALSE, mvp);
        glUniform1f(glGetUniformLocation(prog_, "uExposure"), exposure_);
        glUniform1f(glGetUniformLocation(prog_, "uContrast"),  contrast_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glUniform1i(glGetUniformLocation(prog_, "uTex"), 0);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    // ── Quad ──────────────────────────────────────────────────────────────────
    void buildQuad()
    {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float)*6*4, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
        glBindVertexArray(0);
    }

    void uploadQuad()
    {
        float w = float(imgW_), h = float(imgH_);
        float verts[] = {
            0,0,  0.f,0.f,   w,0.f, 1.f,0.f,   w,h,  1.f,1.f,
            w,h,  1.f,1.f,   0.f,h, 0.f,1.f,   0,0,  0.f,0.f,
        };
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // ── Texture + FBO ─────────────────────────────────────────────────────────
    void uploadFromFile(const std::string& path)
    {
        stbi_set_flip_vertically_on_load(1);
        int w, h, ch;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!data)
        {
            fprintf(stderr, "[ImageSurface] load failed: %s\n", path.c_str());
            if (onStatus) onStatus("Failed to load image.");
            return;
        }
        imgW_ = w; imgH_ = h;

        // Upload source texture
        if (tex_) glDeleteTextures(1, &tex_);
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(data);

        // Rebuild export FBO at image resolution
        rebuildExportFBO();

        uploadQuad();

        if (onLoaded) onLoaded();
    }

    void rebuildExportFBO()
    {
        if (exportFBO_) { glDeleteFramebuffers(1, &exportFBO_); exportFBO_ = 0; }
        if (exportTex_) { glDeleteTextures(1, &exportTex_);     exportTex_ = 0; }

        glGenTextures(1, &exportTex_);
        glBindTexture(GL_TEXTURE_2D, exportTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, imgW_, imgH_, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &exportFBO_);
        glBindFramebuffer(GL_FRAMEBUFFER, exportFBO_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, exportTex_, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            fprintf(stderr, "[ImageSurface] export FBO incomplete: 0x%x\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ── Export MVP ────────────────────────────────────────────────────────────
    // Ortho: maps canvas coords [0,imgW_]x[0,imgH_] → NDC, y-up (matches stbi flip)
    void buildExportMVP(float out[16]) const
    {
        float l=0, r=float(imgW_), b=0, t=float(imgH_);
        memset(out, 0, 64);
        out[0]  =  2.f/(r-l);
        out[5]  =  2.f/(t-b);
        out[10] = -1.f;
        out[12] = -(r+l)/(r-l);
        out[13] = -(t+b)/(t-b);
        out[15] =  1.f;
    }
};

// ============================================================================
// App
// ============================================================================

class ImageApp : public Widget
{
    std::shared_ptr<ImageSurface> surface_;
    CanvasWidget* canvasPtr_ = nullptr;
    State<std::string> status_{"Open an image."};
    State<double> sExposure{0.0};
    State<double> sContrast{0.0};

public:
    WidgetPtr build() override
    {
        auto canvas = std::make_shared<CanvasWidget>();
        canvas->setSize(800, 600);
        canvas->setCanvasSize(800, 600);
        canvas->setScrollbarsEnabled(true);
        canvasPtr_ = canvas.get();

        surface_ = canvas->setSurface<ImageSurface>();

        surface_->onLoaded = [this]()
        {
            canvasPtr_->setCanvasSize(surface_->imgW(), surface_->imgH());
            canvasPtr_->viewport().fitToView();
            canvasPtr_->redraw();
            char buf[64];
            snprintf(buf, sizeof(buf), "Loaded %dx%d",
                     surface_->imgW(), surface_->imgH());
            status_.set(buf);
        };
        surface_->onStatus = [this](const char* m){ status_.set(m); };

        sExposure.listen([this](double v){
            surface_->setExposure(float(v));
            canvasPtr_->redraw();
        });
        sContrast.listen([this](double v){
            surface_->setContrast(float(v));
            canvasPtr_->redraw();
        });

        // ── Toolbar ──────────────────────────────────────────────────────────
        auto toolbar = Container(
            Row({
                FilePicker("📂 Open")
                    ->setMode(FilePickerMode::Open)
                    ->addFilter("Images", {"*.jpg","*.jpeg","*.png","*.bmp"})
                    ->addFilter("All Files", {"*.*"})
                    ->setOnChanged([this](const std::string& path){
                        status_.set("Loading...");
                        surface_->loadImage(path);
                        canvasPtr_->redraw();
                    })
                    ->setShowPath(false)
                    ->setHeight(28)->setWidth(100),

                SizedBox(8, 0),

                FilePicker("💾 Export")
                    ->setMode(FilePickerMode::Save)
                    ->setTitle("Export Image")
                    ->setDefaultFilename("edited.png")
                    ->addFilter("PNG",  {"*.png"})
                    ->addFilter("JPEG", {"*.jpg","*.jpeg"})
                    ->setDefaultExtension("png")
                    ->setOnChanged([this](const std::string& path){
                        surface_->exportImage(path);
                    })
                    ->setShowPath(false)
                    ->setHeight(28)->setWidth(100),

                SizedBox(16, 0),
                Text("Exposure")->setFontSize(10)
                    ->setTextColor(Color::fromRGB(160,160,180)),
                SizedBox(6, 0),
                Slider(-5.0, 5.0, 0.05)
                    ->setValue(sExposure)
                    ->setTrackFillColor(Color::fromRGB(250,220,100))
                    ->setWidth(140),
                SizedBox(6, 0),
                Text(sExposure, [](double v){
                    char b[16]; snprintf(b,sizeof(b),"%.2f",v);
                    return std::string(b);
                })->setFontSize(10)
                  ->setTextColor(Color::fromRGB(200,200,220))
                  ->setMinWidth(36),

                SizedBox(16, 0),
                Text("Contrast")->setFontSize(10)
                    ->setTextColor(Color::fromRGB(160,160,180)),
                SizedBox(6, 0),
                Slider(-1.0, 1.0, 0.01)
                    ->setValue(sContrast)
                    ->setTrackFillColor(Color::fromRGB(174,129,255))
                    ->setWidth(140),
                SizedBox(6, 0),
                Text(sContrast, [](double v){
                    char b[16]; snprintf(b,sizeof(b),"%.2f",v);
                    return std::string(b);
                })->setFontSize(10)
                  ->setTextColor(Color::fromRGB(200,200,220))
                  ->setMinWidth(36),

                SizedBox(16, 0),
                Text(status_, [](const std::string& s){ return s; })
                    ->setFontSize(10)
                    ->setTextColor(Color::fromRGB(148,226,213)),
            })
            ->setSpacing(0)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(Color::fromRGB(17,17,27))
            ->setPaddingAll(10, 8, 10, 8);

        return Scaffold(nullptr,
            Column({ toolbar, canvas })->setSpacing(0)
        );
    }
};

// ============================================================================
// Entry point
// ============================================================================

WidgetPtr createApp(FluxUI* app)
{
    return FluxApp("Image Editor",
                   std::make_shared<ImageApp>(),
                   AppTheme::dark(),
                   false,
                   900, 680,
                   false, false);
}