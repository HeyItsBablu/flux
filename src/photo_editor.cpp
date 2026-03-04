// =============================================================================
// image_editor.hpp  —  Lightroom-style non-destructive image editor


#pragma once

// ── stb_image (single-header) ────────────────────────────────────────────────
// Define these exactly once (in one .cpp / this .hpp included once):
#define STB_IMAGE_IMPLEMENTATION
#include "widgets/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "widgets/stb_image_write.h"

#include "flux.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ── Extra GL defines not present in flux_canvas.hpp ─────────────────────────
#ifndef GL_STATIC_DRAW
#  define GL_STATIC_DRAW  0x88E4
#endif
#ifndef GL_TEXTURE0
#  define GL_TEXTURE0     0x84C0
#endif
#ifndef GL_TEXTURE1
#  define GL_TEXTURE1     0x84C1
#endif
#ifndef GL_TEXTURE2
#  define GL_TEXTURE2     0x84C2
#endif

// ── Extra GL proc types not declared in flux_canvas.hpp ──────────────────────
using PFNGLUNIFORM2FPROC     = void(APIENTRY*)(GLint, GLfloat, GLfloat);
using PFNGLACTIVETEXTUREPROC = void(APIENTRY*)(GLenum);

// ── Lazy-loaded GL extras (avoids touching flux_canvas.hpp) ──────────────────
// Call ie_gl::init() once before using these (ImageEditSurface::initialize does it).
namespace ie_gl {
    inline PFNGLUNIFORM2FPROC     uniform2f     = nullptr;
    inline PFNGLACTIVETEXTUREPROC activeTexture = nullptr;

    inline void init() {
        if (uniform2f) return; // already loaded
        HMODULE gl32 = GetModuleHandleA("opengl32.dll");
        auto load = [&](const char* name) -> void* {
            void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
            if (!p || p==(void*)1||p==(void*)2||p==(void*)3||p==(void*)-1)
                p = reinterpret_cast<void*>(GetProcAddress(gl32, name));
            return p;
        };
        uniform2f     = reinterpret_cast<PFNGLUNIFORM2FPROC>    (load("glUniform2f"));
        activeTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(load("glActiveTexture"));
        assert(uniform2f && activeTexture);
    }
} // namespace ie_gl

// =============================================================================
// §1  EDIT PARAMETERS
//     All values are "natural" units that match what a photographer expects.
// =============================================================================

struct EditParams {
    // ── Tone ─────────────────────────────────────────────────────────────────
    float exposure   =  0.f;   // EV stops, -5 … +5
    float contrast   =  0.f;   // -1 … +1
    float highlights =  0.f;   // -1 … +1  (bright-region exposure tweak)
    float shadows    =  0.f;   // -1 … +1  (dark-region exposure tweak)
    float whites     =  0.f;   // -1 … +1  (upper clip point)
    float blacks     =  0.f;   // -1 … +1  (lower clip point)

    // ── Color ────────────────────────────────────────────────────────────────
    float temperature =  0.f;  // -1 … +1  (cool ↔ warm)
    float tint        =  0.f;  // -1 … +1  (green ↔ magenta)
    float saturation  =  0.f;  // -1 … +1
    float vibrance    =  0.f;  // -1 … +1  (saturation that protects skin)

    // ── Detail ───────────────────────────────────────────────────────────────
    float sharpness   =  0.f;  // 0 … 1
    float noiseReduce =  0.f;  // 0 … 1  (simple blur blend)

    // ── Effects ──────────────────────────────────────────────────────────────
    float vignette    =  0.f;  // -1 … +1  (darken / lighten edges)
    float grain       =  0.f;  // 0 … 1

    // ── Curves (simple 3-point gamma per channel, identity = 1.0) ────────────
    float curveR = 1.f, curveG = 1.f, curveB = 1.f; // gamma exponents

    void reset() { *this = EditParams{}; }
};

// =============================================================================
// §2  GLSL SHADER SOURCE
//     Single-pass fragment shader that implements every adjustment.
//     The vertex shader is trivial (full-screen quad with UVs).
// =============================================================================

static const char* kEditVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char* kEditFrag = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;

uniform sampler2D uOriginal;   // original, untouched image
uniform sampler2D uNoise;      // small tileable noise texture for grain

// ── Tone ─────────────────────────────────────────────────────────────────────
uniform float uExposure;
uniform float uContrast;
uniform float uHighlights;
uniform float uShadows;
uniform float uWhites;
uniform float uBlacks;

// ── Color ────────────────────────────────────────────────────────────────────
uniform float uTemperature;
uniform float uTint;
uniform float uSaturation;
uniform float uVibrance;

// ── Curves ───────────────────────────────────────────────────────────────────
uniform float uCurveR;
uniform float uCurveG;
uniform float uCurveB;

// ── Detail ───────────────────────────────────────────────────────────────────
uniform float uSharpness;
uniform float uNoiseReduce;
uniform vec2  uTexelSize;   // 1/width, 1/height

// ── Effects ──────────────────────────────────────────────────────────────────
uniform float uVignette;
uniform float uGrain;

// ── Helpers ───────────────────────────────────────────────────────────────────

// sRGB → linear
vec3 toLinear(vec3 c){ return pow(clamp(c,0.0,1.0), vec3(2.2)); }
// linear → sRGB
vec3 toSRGB (vec3 c){ return pow(clamp(c,0.0,1.0), vec3(1.0/2.2)); }

// Luminance (Rec.709)
float luma(vec3 c){ return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// RGB ↔ HSL (all in [0,1])
vec3 rgb2hsl(vec3 c){
    float mx = max(c.r,max(c.g,c.b));
    float mn = min(c.r,min(c.g,c.b));
    float d  = mx - mn;
    float l  = (mx + mn) * 0.5;
    if(d < 0.0001) return vec3(0.0, 0.0, l);
    float s = d / (1.0 - abs(2.0*l - 1.0));
    float h;
    if      (mx == c.r) h = mod((c.g - c.b)/d, 6.0) / 6.0;
    else if (mx == c.g) h = ((c.b - c.r)/d + 2.0) / 6.0;
    else                h = ((c.r - c.g)/d + 4.0) / 6.0;
    return vec3(h, s, l);
}
float hue2rgb(float p, float q, float t){
    if(t < 0.0) t += 1.0;
    if(t > 1.0) t -= 1.0;
    if(t < 1.0/6.0) return p + (q-p)*6.0*t;
    if(t < 1.0/2.0) return q;
    if(t < 2.0/3.0) return p + (q-p)*(2.0/3.0 - t)*6.0;
    return p;
}
vec3 hsl2rgb(vec3 hsl){
    float h=hsl.x, s=hsl.y, l=hsl.z;
    if(s < 0.0001) return vec3(l);
    float q = l < 0.5 ? l*(1.0+s) : l+s - l*s;
    float p = 2.0*l - q;
    return vec3(hue2rgb(p,q,h+1.0/3.0),
                hue2rgb(p,q,h),
                hue2rgb(p,q,h-1.0/3.0));
}

// Lift-shadows / pull-highlights mask
float shadowMask   (float l){ return pow(1.0 - l, 2.0); }   // bright=0, dark=1
float highlightMask(float l){ return pow(l,         2.0); }  // bright=1, dark=0

void main(){
    // ── 1. Sample with optional sharpen / noise-reduce blur ──────────────────
    vec3 c;
    if(uSharpness > 0.001 || uNoiseReduce > 0.001){
        // 3×3 box blur for noise reduction
        vec3 blur = vec3(0.0);
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++)
            blur += texture(uOriginal, vUV + vec2(dx,dy)*uTexelSize).rgb;
        blur /= 9.0;

        // Unsharp mask: c = orig + amount*(orig - blur)
        vec3 orig = texture(uOriginal, vUV).rgb;
        float blurBlend = clamp(uNoiseReduce, 0.0, 1.0);
        vec3  blended   = mix(orig, blur, blurBlend);
        float sharpAmt  = uSharpness * 0.6;
        c = clamp(blended + sharpAmt * (orig - blur), 0.0, 1.0);
    } else {
        c = texture(uOriginal, vUV).rgb;
    }

    // Work in linear light from here.
    c = toLinear(c);

    // ── 2. Exposure ───────────────────────────────────────────────────────────
    c *= pow(2.0, uExposure);

    // ── 3. Contrast  (S-curve around 0.18 grey) ───────────────────────────────
    {
        float pivot = 0.18;
        c = (c - pivot) * (1.0 + uContrast) + pivot;
        c = clamp(c, 0.0, 1.0);
    }

    // ── 4. Highlights & Shadows ───────────────────────────────────────────────
    {
        float l = luma(c);
        // highlights: brighten/darken the bright areas
        float hMask = highlightMask(l);
        float sMask = shadowMask(l);
        c *= 1.0 + uHighlights * hMask * 0.7;
        c *= 1.0 + uShadows    * sMask * 0.7;
        c  = clamp(c, 0.0, 1.0);
    }

    // ── 5. Whites & Blacks (clip points) ─────────────────────────────────────
    {
        // whites: shift the upper knee
        float wScale = 1.0 + uWhites * 0.3;
        // blacks: shift the lower knee
        float bShift = -uBlacks * 0.15;
        c = c * wScale + bShift;
        c = clamp(c, 0.0, 1.0);
    }

    // ── 6. Per-channel curves (power/gamma) ───────────────────────────────────
    c.r = pow(c.r, 1.0 / max(uCurveR, 0.1));
    c.g = pow(c.g, 1.0 / max(uCurveG, 0.1));
    c.b = pow(c.b, 1.0 / max(uCurveB, 0.1));
    c = clamp(c, 0.0, 1.0);

    // ── 7. Temperature & Tint  ────────────────────────────────────────────────
    // Temperature shifts the blue↔amber axis; Tint shifts green↔magenta.
    {
        float t = uTemperature * 0.15;   // scaled so ±1 is a strong shift
        float g = uTint        * 0.10;
        // Warm shift: boost R, reduce B; cool: opposite
        c.r = clamp(c.r + t, 0.0, 1.0);
        c.b = clamp(c.b - t, 0.0, 1.0);
        // Tint: positive = magenta (boost R+B, cut G), negative = green
        c.r = clamp(c.r + g, 0.0, 1.0);
        c.g = clamp(c.g - g, 0.0, 1.0);
        c.b = clamp(c.b + g, 0.0, 1.0);
    }

    // Convert to sRGB for HSL operations (HSL is defined on gamma-encoded values)
    c = toSRGB(c);

    // ── 8. Saturation ─────────────────────────────────────────────────────────
    {
        vec3 hsl = rgb2hsl(c);
        hsl.y = clamp(hsl.y * (1.0 + uSaturation), 0.0, 1.0);
        c = hsl2rgb(hsl);
    }

    // ── 9. Vibrance  (saturation that spares already-saturated pixels) ────────
    {
        vec3  hsl  = rgb2hsl(c);
        float sat  = hsl.y;
        // Weight: low-saturation pixels get more boost
        float mask = (1.0 - sat) * (1.0 - sat);
        hsl.y = clamp(hsl.y + uVibrance * mask * 0.8, 0.0, 1.0);
        c = hsl2rgb(hsl);
    }

    c = clamp(c, 0.0, 1.0);

    // ── 10. Vignette  ─────────────────────────────────────────────────────────
    {
        vec2  uv2   = vUV * 2.0 - 1.0;
        float dist  = dot(uv2, uv2);               // 0 at centre, ~2 at corners
        float mask  = smoothstep(0.5, 1.6, dist);   // ramp starts mid-frame
        // uVignette < 0 → darken edges (classic), > 0 → lighten
        c = c * (1.0 - mask * (-uVignette) * 0.8);
        c = clamp(c, 0.0, 1.0);
    }

    // ── 11. Grain ─────────────────────────────────────────────────────────────
    if(uGrain > 0.001){
        vec3  n   = texture(uNoise, vUV * 5.0).rgb;   // tile noise texture
        float nm  = (n.r - 0.5) * uGrain * 0.12;
        c = clamp(c + nm, 0.0, 1.0);
    }

    fragColor = vec4(c, 1.0);
}
)GLSL";

// =============================================================================
// §3  IMAGE EDIT SURFACE
//     Owns the original RGBA pixel data.  Every frame it binds the original
//     texture and runs the edit shader — purely non-destructive.
//     Export reads back the edited pixels via an FBO.
// =============================================================================

class ImageEditSurface : public RenderSurface {
public:
    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void()>              onImageLoaded;
    std::function<void(const char*)>   onStatusMessage;

    // ── Public API ────────────────────────────────────────────────────────────

    bool loadImage(const wchar_t* path) {
        // Convert wchar_t path → UTF-8 for stb_image
        char utf8[MAX_PATH * 3] = {};
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);

        int w, h, ch;
        stbi_set_flip_vertically_on_load(1); // flip to GL Y-up
        unsigned char* data = stbi_load(utf8, &w, &h, &ch, 4); // force RGBA
        if (!data) {
            if (onStatusMessage) onStatusMessage("Failed to load image.");
            return false;
        }

        imgW_ = w; imgH_ = h;

        // Store original pixel data for non-destructive export
        originalPixels_.assign(data, data + size_t(w) * h * 4);
        stbi_image_free(data);

        uploadOriginal();
        params_.reset();

        if (onImageLoaded) onImageLoaded();
        if (onStatusMessage) {
            char msg[128];
            _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                "Loaded %dx%d", w, h);
            onStatusMessage(msg);
        }
        return true;
    }

    bool exportImage(const wchar_t* path) {
        if (originalPixels_.empty() || !editFBO_) return false;

        // Render one frame into editFBO_ at full resolution
        renderToExportFBO();

        // Read back
        std::vector<uint8_t> buf(size_t(imgW_) * imgH_ * 4);
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, editFBO_);
        glReadPixels(0, 0, imgW_, imgH_, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        GL.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        // Flip back to top-down for stb_image_write
        std::vector<uint8_t> flipped(buf.size());
        int stride = imgW_ * 4;
        for (int r = 0; r < imgH_; r++)
            memcpy(flipped.data() + r * stride,
                   buf.data() + (imgH_ - 1 - r) * stride, stride);

        // Write PNG or JPEG depending on extension
        char utf8[MAX_PATH * 3] = {};
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
        std::string ps(utf8);
        bool ok = false;
        if (ps.size() >= 4 &&
            (ps.substr(ps.size()-4) == ".jpg" || ps.substr(ps.size()-5) == ".jpeg"))
            ok = stbi_write_jpg(utf8, imgW_, imgH_, 4, flipped.data(), 95) != 0;
        else
            ok = stbi_write_png(utf8, imgW_, imgH_, 4, flipped.data(), stride) != 0;

        if (onStatusMessage)
            onStatusMessage(ok ? "Export saved." : "Export failed.");
        return ok;
    }

    bool hasImage() const { return !originalPixels_.empty(); }
    int  imageWidth()  const { return imgW_; }
    int  imageHeight() const { return imgH_; }

    EditParams& params() { return params_; }
    const EditParams& params() const { return params_; }

    // Histogram data: 256 bins per channel (R, G, B), normalised 0–1.
    const std::vector<float>& histR() const { return histR_; }
    const std::vector<float>& histG() const { return histG_; }
    const std::vector<float>& histB() const { return histB_; }

    void computeHistogram() {
        if (originalPixels_.empty()) return;
        std::vector<int> r(256,0), g(256,0), b(256,0);
        size_t n = size_t(imgW_) * imgH_;
        for (size_t i = 0; i < n; i++) {
            r[originalPixels_[i*4+0]]++;
            g[originalPixels_[i*4+1]]++;
            b[originalPixels_[i*4+2]]++;
        }
        int peak = 1;
        for (int i = 0; i < 256; i++) peak = max(peak, max(r[i], max(g[i], b[i])));
        histR_.resize(256); histG_.resize(256); histB_.resize(256);
        for (int i = 0; i < 256; i++) {
            histR_[i] = float(r[i]) / peak;
            histG_[i] = float(g[i]) / peak;
            histB_[i] = float(b[i]) / peak;
        }
    }

    // ── RenderSurface interface ───────────────────────────────────────────────

    void initialize(int w, int h) override {
        ie_gl::init();   // load glUniform2f + glActiveTexture
        viewW_ = w; viewH_ = h;
        buildShader();
        buildQuad();
        buildNoiseTexture();
        buildEditFBO();
    }

    void resize(int w, int h) override {
        viewW_ = w; viewH_ = h;
    }

    void update(double) override {}

    void render(const float /*mvp*/[16]) override {
        if (originalPixels_.empty() || !origTex_) {
            // No image — draw a dark checkerboard background
            glViewport(0, 0, viewW_, viewH_);
            glClearColor(0.12f, 0.12f, 0.14f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            return;
        }

        glViewport(0, 0, viewW_, viewH_);
        glClearColor(0.08f, 0.08f, 0.09f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        drawEditPass(0); // draw to screen (fbo=0)
    }

    void destroy() override {
        if (editProg_)   { GL.deleteProgram(editProg_); editProg_ = 0; }
        if (quadVAO_)    { GL.deleteVertexArrays(1, &quadVAO_); quadVAO_ = 0; }
        if (quadVBO_)    { GL.deleteBuffers(1, &quadVBO_); quadVBO_ = 0; }
        if (origTex_)    { glDeleteTextures(1, &origTex_); origTex_ = 0; }
        if (noiseTex_)   { glDeleteTextures(1, &noiseTex_); noiseTex_ = 0; }
        if (editFBO_)    { GL.deleteFramebuffers(1, &editFBO_); editFBO_ = 0; }
        if (editTex_)    { glDeleteTextures(1, &editTex_); editTex_ = 0; }
    }

private:
    int viewW_ = 1, viewH_ = 1;
    int imgW_  = 0, imgH_  = 0;

    EditParams params_;

    std::vector<uint8_t> originalPixels_;
    std::vector<float>   histR_, histG_, histB_;

    GLuint editProg_  = 0;
    GLuint quadVAO_   = 0, quadVBO_   = 0;
    GLuint origTex_   = 0;
    GLuint noiseTex_  = 0;
    GLuint editFBO_   = 0, editTex_   = 0;

    // ── GL helpers ────────────────────────────────────────────────────────────

    void buildShader() {
        editProg_ = glutil::linkProgram(kEditVert, kEditFrag);
        assert(editProg_);
    }

    void buildQuad() {
        // Full-screen NDC quad with UVs
        float verts[] = {
            -1.f,-1.f,  0.f,0.f,
             1.f,-1.f,  1.f,0.f,
             1.f, 1.f,  1.f,1.f,
             1.f, 1.f,  1.f,1.f,
            -1.f, 1.f,  0.f,1.f,
            -1.f,-1.f,  0.f,0.f,
        };
        GL.genVertexArrays(1, &quadVAO_);
        GL.genBuffers(1, &quadVBO_);
        GL.bindVertexArray(quadVAO_);
        GL.bindBuffer(GL_ARRAY_BUFFER, quadVBO_);
        GL.bufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
        GL.enableVertexAttribArray(1);
        GL.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
        GL.bindVertexArray(0);
    }

    void buildNoiseTexture() {
        // 64×64 RGB noise for grain
        const int N = 64;
        std::vector<uint8_t> noise(N * N * 3);
        srand(42);
        for (auto& v : noise) v = uint8_t(rand() & 0xFF);
        glGenTextures(1, &noiseTex_);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, N, N, 0, GL_RGB, GL_UNSIGNED_BYTE, noise.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void buildEditFBO() {
        // We rebuild this when loading an image to match its resolution.
    }

    void rebuildEditFBOForImage() {
        if (editFBO_) { GL.deleteFramebuffers(1, &editFBO_); editFBO_ = 0; }
        if (editTex_) { glDeleteTextures(1, &editTex_);      editTex_ = 0; }

        glGenTextures(1, &editTex_);
        glBindTexture(GL_TEXTURE_2D, editTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imgW_, imgH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        GL.genFramebuffers(1, &editFBO_);
        GL.bindFramebuffer(GL_FRAMEBUFFER, editFBO_);
        GL.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, editTex_, 0);
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void uploadOriginal() {
        if (origTex_) { glDeleteTextures(1, &origTex_); origTex_ = 0; }
        glGenTextures(1, &origTex_);
        glBindTexture(GL_TEXTURE_2D, origTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imgW_, imgH_, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, originalPixels_.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        rebuildEditFBOForImage();
        computeHistogram();
    }

    void setUniforms() {
        auto loc = [&](const char* name) {
            return GL.getUniformLocation(editProg_, name);
        };
        const EditParams& p = params_;
        GL.uniform1f(loc("uExposure"),    p.exposure);
        GL.uniform1f(loc("uContrast"),    p.contrast);
        GL.uniform1f(loc("uHighlights"),  p.highlights);
        GL.uniform1f(loc("uShadows"),     p.shadows);
        GL.uniform1f(loc("uWhites"),      p.whites);
        GL.uniform1f(loc("uBlacks"),      p.blacks);
        GL.uniform1f(loc("uTemperature"), p.temperature);
        GL.uniform1f(loc("uTint"),        p.tint);
        GL.uniform1f(loc("uSaturation"),  p.saturation);
        GL.uniform1f(loc("uVibrance"),    p.vibrance);
        GL.uniform1f(loc("uSharpness"),   p.sharpness);
        GL.uniform1f(loc("uNoiseReduce"), p.noiseReduce);
        GL.uniform1f(loc("uVignette"),    p.vignette);
        GL.uniform1f(loc("uGrain"),       p.grain);
        GL.uniform1f(loc("uCurveR"),      p.curveR);
        GL.uniform1f(loc("uCurveG"),      p.curveG);
        GL.uniform1f(loc("uCurveB"),      p.curveB);
        ie_gl::uniform2f(loc("uTexelSize"),
                     1.f / float(imgW_), 1.f / float(imgH_));
        GL.uniform1i(loc("uOriginal"), 0);
        GL.uniform1i(loc("uNoise"),    1);
    }

    // Draw the edit pass.  fbo=0 means the default framebuffer (screen).
    void drawEditPass(GLuint fbo) {
        GL.bindFramebuffer(GL_FRAMEBUFFER, fbo);
        if (fbo == 0)
            glViewport(0, 0, viewW_, viewH_);
        else
            glViewport(0, 0, imgW_, imgH_);

        GL.useProgram(editProg_);
        setUniforms();

        ie_gl::activeTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, origTex_);
        ie_gl::activeTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);

        GL.bindVertexArray(quadVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        GL.bindVertexArray(0);

        GL.useProgram(0);
        GL.bindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void renderToExportFBO() {
        drawEditPass(editFBO_);
    }

    // (ie_gl::uniform2f and ie_gl::activeTexture are used instead of the
    //  missing GLProcs members — see the ie_gl namespace near the top of file)
};  // class ImageEditSurface

// =============================================================================
// §4  FILE DIALOGS
// =============================================================================

static std::wstring promptOpenImage(HWND owner) {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = owner;
    ofn.lpstrFilter =
        L"Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.tga;*.hdr\0All Files\0*.*\0";
    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Open Image";
    return GetOpenFileNameW(&ofn) ? buf : L"";
}

static std::wstring promptExportImage(HWND owner) {
    wchar_t buf[MAX_PATH] = L"edited.png";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PNG\0*.png\0JPEG\0*.jpg\0All Files\0*.*\0";
    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Export Image";
    return GetSaveFileNameW(&ofn) ? buf : L"";
}

// =============================================================================
// §5  IMAGE EDITOR APP  (FluxUI Component)
// =============================================================================

class ImageEditorApp : public Component {
    // ── State ─────────────────────────────────────────────────────────────────
    State<std::string> statusMsg;
    State<bool>        hasImage;
    State<bool>        beforeMode;  // before/after toggle

    // Edit param states (one per slider)
    State<double> sExposure, sContrast, sHighlights, sShadows, sWhites, sBlacks;
    State<double> sTemperature, sTint, sSaturation, sVibrance;
    State<double> sSharpness, sNoiseReduce;
    State<double> sVignette, sGrain;
    State<double> sCurveR, sCurveG, sCurveB;

    std::shared_ptr<ImageEditSurface> surface_;
    CanvasWidget* canvasPtr_ = nullptr;
    HWND mainHwnd_ = nullptr;

    static constexpr int kPanelW = 280;
    static constexpr int kToolbarH = 44;
    static constexpr int kHintsH = 22;

    // Sync a single slider State → EditParams field, then redraw
    template<typename Field>
    void wire(State<double>& st, Field EditParams::* field) {
        st.listen([this, field](double v){
            if (!surface_) return;
            surface_->params().*field = float(v);
            if (canvasPtr_) canvasPtr_->redraw();
        });
    }

public:
    ImageEditorApp()
        : statusMsg("Open an image to begin.", context)
        , hasImage(false, context)
        , beforeMode(false, context)
        , sExposure(0.0,context), sContrast(0.0,context)
        , sHighlights(0.0,context), sShadows(0.0,context)
        , sWhites(0.0,context), sBlacks(0.0,context)
        , sTemperature(0.0,context), sTint(0.0,context)
        , sSaturation(0.0,context), sVibrance(0.0,context)
        , sSharpness(0.0,context), sNoiseReduce(0.0,context)
        , sVignette(0.0,context), sGrain(0.0,context)
        , sCurveR(1.0,context), sCurveG(1.0,context), sCurveB(1.0,context)
    {}

    WidgetPtr build() override {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int viewW   = screenW - kPanelW;
        int viewH   = screenH - kToolbarH - kHintsH;

        // ── Canvas setup ──────────────────────────────────────────────────────
        // We start with a 1×1 canvas; it's replaced on image load.
        auto canvas = std::make_shared<CanvasWidget>()->setSize(viewW, viewH);
        canvas->setCanvasSize(1, 1);
        surface_ = canvas->setSurface<ImageEditSurface>();
        canvasPtr_ = canvas.get();
        mainHwnd_ = GetActiveWindow();

        surface_->onImageLoaded = [this](){
            hasImage.set(true);
            if (canvasPtr_) {
                auto* s = surface_.get();
                canvasPtr_->setCanvasSize(s->imageWidth(), s->imageHeight());
                canvasPtr_->viewport().fitToView();
                canvasPtr_->redraw();
            }
        };
        surface_->onStatusMessage = [this](const char* msg){
            statusMsg.set(msg);
        };

        // Wire all sliders
        wire(sExposure,    &EditParams::exposure);
        wire(sContrast,    &EditParams::contrast);
        wire(sHighlights,  &EditParams::highlights);
        wire(sShadows,     &EditParams::shadows);
        wire(sWhites,      &EditParams::whites);
        wire(sBlacks,      &EditParams::blacks);
        wire(sTemperature, &EditParams::temperature);
        wire(sTint,        &EditParams::tint);
        wire(sSaturation,  &EditParams::saturation);
        wire(sVibrance,    &EditParams::vibrance);
        wire(sSharpness,   &EditParams::sharpness);
        wire(sNoiseReduce, &EditParams::noiseReduce);
        wire(sVignette,    &EditParams::vignette);
        wire(sGrain,       &EditParams::grain);
        wire(sCurveR,      &EditParams::curveR);
        wire(sCurveG,      &EditParams::curveG);
        wire(sCurveB,      &EditParams::curveB);

        // ── Colors ────────────────────────────────────────────────────────────
        COLORREF kBg1    = RGB(17,17,27);
        COLORREF kBg2    = RGB(24,24,37);
        COLORREF kBorder = RGB(49,50,68);
        COLORREF kText   = RGB(205,214,244);
        COLORREF kDim    = RGB(100,105,130);
        COLORREF kAccent = RGB(174,129,255);
        COLORREF kGreen  = RGB(148,226,213);
        COLORREF kRed    = RGB(243,139,168);
        COLORREF kOrange = RGB(250,179,135);

        // ── Helpers ───────────────────────────────────────────────────────────
        auto sectionLabel = [&](const std::string& txt) -> WidgetPtr {
            return Text(txt)
                ->setFontSize(9)
                ->setTextColor(RGB(100,105,125))
                ->setFontWeight(FontWeight::Bold);
        };

        auto cardWrap = [&](WidgetPtr child) -> WidgetPtr {
            return Container(child)
                ->setBackgroundColor(kBg2)
                ->setBorderRadius(8)
                ->setBorderWidth(1)->setBorderColor(kBorder)
                ->setPaddingAll(10,10,10,10);
        };

        // ── Slider row: label + slider + value ────────────────────────────────
        // Returns a row widget for a single adjustment slider.
        auto makeSlider = [&](const std::string& label,
                              double lo, double hi, double step,
                              State<double>& st,
                              COLORREF trackColor = RGB(174,129,255)) -> WidgetPtr
        {
            return Column(
                Row(
                    Text(label)->setFontSize(9)->setTextColor(kDim)->setMinWidth(72),
                    SizedBox(4,0),
                    Text(st, [](double v){
                        char buf[16];
                        _snprintf_s(buf,sizeof(buf),_TRUNCATE, "%.2f", v);
                        return std::string(buf);
                    })->setFontSize(9)->setTextColor(kText)->setMinWidth(36)
                )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center),
                SizedBox(0,4),
                Slider(lo, hi, step)
                    ->setValue(st)
                    ->setTrackFillColor(trackColor)
                    ->setWidth(248)
            )->setSpacing(0);
        };

        // ── §A  HISTOGRAM (simple bar chart drawn inline) ─────────────────────
        // We draw a tiny fixed-size histogram using a custom painted widget.
        // For simplicity we use a Container with a background and overlay text.
        // A full custom-paint histogram would require a CustomWidget subclass;
        // here we approximate it with a description label.
        auto histogramSection = cardWrap(
            Column(
                sectionLabel("HISTOGRAM"),
                SizedBox(0,6),
                Container(
                    Text("RGB histogram displayed after load")
                        ->setFontSize(9)->setTextColor(kDim)
                )->setWidth(248)->setHeight(48)
                 ->setBackgroundColor(RGB(20,20,30))
                 ->setBorderRadius(4)->setBorderWidth(1)->setBorderColor(kBorder)
            )->setSpacing(0)
        );

        // ── §B  TONE SECTION ──────────────────────────────────────────────────
        auto toneSection = cardWrap(
            Column(
                sectionLabel("TONE"),
                SizedBox(0,8),
                makeSlider("Exposure",   -5.0,  5.0, 0.05, sExposure,   RGB(250,220,100)),
                SizedBox(0,6),
                makeSlider("Contrast",   -1.0,  1.0, 0.01, sContrast,   kAccent),
                SizedBox(0,6),
                makeSlider("Highlights", -1.0,  1.0, 0.01, sHighlights, RGB(200,200,220)),
                SizedBox(0,6),
                makeSlider("Shadows",    -1.0,  1.0, 0.01, sShadows,    RGB(80,100,160)),
                SizedBox(0,6),
                makeSlider("Whites",     -1.0,  1.0, 0.01, sWhites,     RGB(230,230,230)),
                SizedBox(0,6),
                makeSlider("Blacks",     -1.0,  1.0, 0.01, sBlacks,     RGB(60,60,80))
            )->setSpacing(0)
        );

        // ── §C  COLOR SECTION ─────────────────────────────────────────────────
        auto colorSection = cardWrap(
            Column(
                sectionLabel("COLOR"),
                SizedBox(0,8),
                makeSlider("Temperature", -1.0, 1.0, 0.01, sTemperature, RGB(100,180,250)),
                SizedBox(0,6),
                makeSlider("Tint",        -1.0, 1.0, 0.01, sTint,        RGB(200,120,200)),
                SizedBox(0,6),
                makeSlider("Saturation",  -1.0, 1.0, 0.01, sSaturation,  RGB(220,120,100)),
                SizedBox(0,6),
                makeSlider("Vibrance",    -1.0, 1.0, 0.01, sVibrance,    RGB(170,220,120))
            )->setSpacing(0)
        );

        // ── §D  CURVES SECTION ────────────────────────────────────────────────
        auto curvesSection = cardWrap(
            Column(
                sectionLabel("CURVES (gamma)"),
                SizedBox(0,8),
                makeSlider("Red",   0.1, 3.0, 0.01, sCurveR, RGB(243,139,168)),
                SizedBox(0,6),
                makeSlider("Green", 0.1, 3.0, 0.01, sCurveG, RGB(166,227,161)),
                SizedBox(0,6),
                makeSlider("Blue",  0.1, 3.0, 0.01, sCurveB, RGB(137,180,250))
            )->setSpacing(0)
        );

        // ── §E  DETAIL SECTION ────────────────────────────────────────────────
        auto detailSection = cardWrap(
            Column(
                sectionLabel("DETAIL"),
                SizedBox(0,8),
                makeSlider("Sharpness",     0.0, 1.0, 0.01, sSharpness,   kAccent),
                SizedBox(0,6),
                makeSlider("Noise Reduce",  0.0, 1.0, 0.01, sNoiseReduce, kGreen)
            )->setSpacing(0)
        );

        // ── §F  EFFECTS SECTION ───────────────────────────────────────────────
        auto effectsSection = cardWrap(
            Column(
                sectionLabel("EFFECTS"),
                SizedBox(0,8),
                makeSlider("Vignette", -1.0, 1.0, 0.01, sVignette, RGB(80,80,100)),
                SizedBox(0,6),
                makeSlider("Grain",     0.0, 1.0, 0.01, sGrain,    RGB(180,160,130))
            )->setSpacing(0)
        );

        // ── §G  RIGHT PANEL ASSEMBLY ──────────────────────────────────────────
        auto panel = Container(
            Column(
                histogramSection,  SizedBox(0,6),
                toneSection,       SizedBox(0,6),
                colorSection,      SizedBox(0,6),
                curvesSection,     SizedBox(0,6),
                detailSection,     SizedBox(0,6),
                effectsSection
            )->setSpacing(0)
        )->setWidth(kPanelW)
         ->setBackgroundColor(kBg1)
         ->setPaddingAll(10,10,10,10);

        // ── §H  TOOLBAR ───────────────────────────────────────────────────────
        auto makeBtn = [&](const std::string& lbl, COLORREF c,
                           std::function<void()> fn) -> WidgetPtr
        {
            return Button(lbl, fn)
                ->setBackgroundColor(RGB(30,30,46))
                ->setTextColor(c)
                ->setBorderRadius(5)
                ->setHeight(26)
                ->setPadding(4);
        };

        auto toolbar = Container(
            Row(
                makeBtn("📂 Open",   kGreen,  [this](){
                    std::wstring p = promptOpenImage(mainHwnd_);
                    if (!p.empty() && surface_)
                        surface_->loadImage(p.c_str());
                }),
                SizedBox(8,0),
                makeBtn("💾 Export", kAccent, [this](){
                    if (!surface_ || !surface_->hasImage()) return;
                    std::wstring p = promptExportImage(mainHwnd_);
                    if (!p.empty()) surface_->exportImage(p.c_str());
                }),
                SizedBox(8,0),
                makeBtn("↺ Reset",   kOrange, [this](){
                    if (!surface_) return;
                    surface_->params().reset();
                    // Reset all slider states
                    sExposure.set(0); sContrast.set(0);
                    sHighlights.set(0); sShadows.set(0);
                    sWhites.set(0); sBlacks.set(0);
                    sTemperature.set(0); sTint.set(0);
                    sSaturation.set(0); sVibrance.set(0);
                    sSharpness.set(0); sNoiseReduce.set(0);
                    sVignette.set(0); sGrain.set(0);
                    sCurveR.set(1); sCurveG.set(1); sCurveB.set(1);
                    if (canvasPtr_) canvasPtr_->redraw();
                }),
                SizedBox(8,0),
                GestureDetector(
                    Container(
                        Text(beforeMode, [](const bool& b){
                            return std::string(b ? "After ▶" : "Before ◀");
                        })->setFontSize(11)->setTextColor(kText)
                    )->setBackgroundColor(RGB(30,30,46))
                     ->setBorderRadius(5)->setPaddingAll(6,3,6,3)
                     ->setBorderWidth(1)->setBorderColor(kBorder)
                )->setOnTap([this](){
                    // Toggle before/after by zeroing all params temporarily
                    bool b = !beforeMode.get();
                    beforeMode.set(b);
                    if (surface_) {
                        if (b) {
                            // Save current and zero out
                            savedParams_ = surface_->params();
                            surface_->params().reset();
                        } else {
                            surface_->params() = savedParams_;
                        }
                        if (canvasPtr_) canvasPtr_->redraw();
                    }
                }),
                SizedBox(16,0),
                Text("Zoom: ")->setFontSize(10)->setTextColor(kDim),
                Button("Fit", [this](){
                    if (canvasPtr_) {
                        canvasPtr_->viewport().fitToView();
                        canvasPtr_->redraw();
                    }
                })->setBackgroundColor(RGB(24,24,37))->setTextColor(kAccent)
                  ->setBorderRadius(5)->setWidth(32)->setHeight(26)->setPadding(4),
                Button("1:1", [this](){
                    if (canvasPtr_) {
                        canvasPtr_->viewport().resetZoom();
                        canvasPtr_->redraw();
                    }
                })->setBackgroundColor(RGB(24,24,37))->setTextColor(kAccent)
                  ->setBorderRadius(5)->setWidth(32)->setHeight(26)->setPadding(4),
                SizedBox(16,0),
                Text(statusMsg, [](const std::string& s){ return s; })
                    ->setFontSize(10)->setTextColor(RGB(148,226,213))
            )->setSpacing(4)->setCrossAxisAlignment(CrossAxisAlignment::Center)
        )->setBackgroundColor(kBg1)
         ->setPaddingAll(10,7,10,7)
         ->setHeight(kToolbarH);

        // ── §I  HINTS STRIP ───────────────────────────────────────────────────
        auto hints = Container(
            Row(
                Text("MMB/Space: Pan")->setFontSize(10)->setTextColor(RGB(60,60,80)),
                SizedBox(10,0),
                Text("Ctrl+Scroll: Zoom")->setFontSize(10)->setTextColor(RGB(60,60,80)),
                SizedBox(10,0),
                Text("Drag sliders for live preview  |  Reset clears all edits  |  Before◀ toggles original")
                    ->setFontSize(10)->setTextColor(RGB(60,60,80))
            )->setSpacing(0)
        )->setBackgroundColor(kBg1)
         ->setPaddingAll(10,3,10,3)
         ->setHeight(kHintsH);

        // ── §J  ROOT LAYOUT ───────────────────────────────────────────────────
        auto canvasCol = Column(toolbar, hints, canvas)->setSpacing(0);
        auto root = Row(canvasCol, panel)->setSpacing(0);
        return Scaffold(root);
    }

private:
    EditParams savedParams_; // for before/after toggle
};


// ── Entry point ────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE,LPSTR,int){
  FluxUI app(hInstance);
  app.build([&](){ return FluxApp("Paint",BuildComponent<ImageEditorApp>(),AppTheme::dark()); });
  RECT wa; SystemParametersInfo(SPI_GETWORKAREA,0,&wa,0);
  app.createWindow("FluxUI - Paint",wa.right-wa.left,wa.bottom-wa.top);
  return app.run();
}