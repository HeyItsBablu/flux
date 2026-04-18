#pragma once

#include "flux/flux.hpp"
#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <glad/glad.h>
#include <memory>
#include <nanovg.h>
#include <nanovg_gl.h>
#include <string>
#include <vector>

struct EditParams {
  // ── Tone ──────────────────────────────────────────────────────────────────
  float exposure = 0.f;
  float contrast = 0.f;
  float highlights = 0.f;
  float shadows = 0.f;
  float whites = 0.f;
  float blacks = 0.f;
  // ── Color ─────────────────────────────────────────────────────────────────
  float temperature = 0.f;
  float tint = 0.f;
  float saturation = 0.f;
  float vibrance = 0.f;
  // ── Detail ────────────────────────────────────────────────────────────────
  float sharpness = 0.f;
  float noiseReduce = 0.f;
  // ── Effects ───────────────────────────────────────────────────────────────
  float vignette = 0.f;
  float grain = 0.f;

  // ── Tone curve LUTs (256-entry) ───────────────────────────────────────────
  std::array<uint8_t, 256> lutRGB{};
  std::array<uint8_t, 256> lutR{};
  std::array<uint8_t, 256> lutG{};
  std::array<uint8_t, 256> lutB{};

  // ── HSL LUTs (360-entry) ──────────────────────────────────────────────────
  std::array<uint8_t, 360> hslHue{}; // encoded: 0.5=neutral, shift ±30°
  std::array<uint8_t, 360> hslSat{}; // encoded: 0.5=1× multiplier
  std::array<uint8_t, 360> hslLum{}; // encoded: 0.5=neutral, shift ±1

  EditParams() {
    resetCurves();
    resetHSL();
  }

  void resetCurves() {
    for (int i = 0; i < 256; ++i)
      lutRGB[i] = lutR[i] = lutG[i] = lutB[i] = uint8_t(i);
  }
  void resetHSL() {
    for (int i = 0; i < 360; ++i) {
      hslHue[i] = 128;
      hslSat[i] = 128;
      hslLum[i] = 128;
    }
  }
  void reset() { *this = EditParams{}; }
};

namespace cpu_edit {

// ── Scalar helpers
// ────────────────────────────────────────────────────────────

static inline float clamp01(float v) {
  return v < 0.f ? 0.f : v > 1.f ? 1.f : v;
}

static inline float toLinear(float c) {
  c = clamp01(c);
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
static inline float toSRGB(float c) {
  c = clamp01(c);
  return c <= 0.0031308f ? c * 12.92f
                         : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}
static inline float luma(float r, float g, float b) {
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ── HSL conversion
// ────────────────────────────────────────────────────────────

struct HSL {
  float h, s, l;
};

static HSL rgb2hsl(float r, float g, float b) {
  float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  float l = (mx + mn) * 0.5f;
  if (d < 1e-5f)
    return {0.f, 0.f, l};
  float s = d / (1.f - std::abs(2.f * l - 1.f));
  float h;
  if (mx == r)
    h = std::fmod((g - b) / d, 6.f) / 6.f;
  else if (mx == g)
    h = ((b - r) / d + 2.f) / 6.f;
  else
    h = ((r - g) / d + 4.f) / 6.f;
  if (h < 0.f)
    h += 1.f;
  return {h, s, l};
}

static inline float hue2rgb(float p, float q, float t) {
  if (t < 0.f)
    t += 1.f;
  if (t > 1.f)
    t -= 1.f;
  if (t < 1.f / 6.f)
    return p + (q - p) * 6.f * t;
  if (t < 0.5f)
    return q;
  if (t < 2.f / 3.f)
    return p + (q - p) * (2.f / 3.f - t) * 6.f;
  return p;
}

static void hsl2rgb(HSL hsl, float &r, float &g, float &b) {
  if (hsl.s < 1e-5f) {
    r = g = b = hsl.l;
    return;
  }
  float q =
      hsl.l < 0.5f ? hsl.l * (1.f + hsl.s) : hsl.l + hsl.s - hsl.l * hsl.s;
  float p = 2.f * hsl.l - q;
  r = hue2rgb(p, q, hsl.h + 1.f / 3.f);
  g = hue2rgb(p, q, hsl.h);
  b = hue2rgb(p, q, hsl.h - 1.f / 3.f);
}

// ── Apply one LUT256 sample ────────────────────────────────────────────────

static inline float applyLUT256(const uint8_t *lut, float v) {
  int idx = std::max(0, std::min(255, int(v * 255.f + 0.5f)));
  return lut[idx] / 255.f;
}

// ── Apply one HSL LUT360 sample ───────────────────────────────────────────

static inline float sampleHSL360(const uint8_t *lut, float hueNorm) {
  int idx = std::max(0, std::min(359, int(hueNorm * 360.f)));
  return lut[idx] / 255.f;
}

// ── Shadow / highlight masks ───────────────────────────────────────────────

static inline float shadowMask(float l) { return (1.f - l) * (1.f - l); }
static inline float highlightMask(float l) { return l * l; }

// ── Simple 3×3 blur (for sharpness / noise-reduce) ────────────────────────

static void blurRow(const uint8_t *src, uint8_t *dst, int w, int ch, int y,
                    int h) {
  // Horizontal pass (clamp borders)
  for (int x = 0; x < w; ++x) {
    for (int c = 0; c < ch; ++c) {
      int x0 = std::max(0, x - 1), x1 = x, x2 = std::min(w - 1, x + 1);
      int v =
          (int(src[(y * w + x0) * ch + c]) + int(src[(y * w + x1) * ch + c]) +
           int(src[(y * w + x2) * ch + c])) /
          3;
      dst[(y * w + x) * ch + c] = uint8_t(v);
    }
  }
}

static std::vector<uint8_t> boxBlur3(const uint8_t *src, int w, int h) {
  // Horizontal pass
  std::vector<uint8_t> tmp(size_t(w) * h * 4);
  for (int y = 0; y < h; ++y)
    blurRow(src, tmp.data(), w, 4, y, h);
  // Vertical pass
  std::vector<uint8_t> out(size_t(w) * h * 4);
  for (int x = 0; x < w; ++x) {
    for (int y = 0; y < h; ++y) {
      int y0 = std::max(0, y - 1), y1 = y, y2 = std::min(h - 1, y + 1);
      for (int c = 0; c < 3; ++c) {
        int v =
            (int(tmp[(y0 * w + x) * 4 + c]) + int(tmp[(y1 * w + x) * 4 + c]) +
             int(tmp[(y2 * w + x) * 4 + c])) /
            3;
        out[(y * w + x) * 4 + c] = uint8_t(v);
      }
      out[(y * w + x) * 4 + 3] = src[(y * w + x) * 4 + 3]; // preserve alpha
    }
  }
  return out;
}

// ── Main per-pixel pipeline ────────────────────────────────────────────────

static void processPixels(const uint8_t *orig, uint8_t *out, int w, int h,
                          const EditParams &p, unsigned noiseSeed = 42) {
  const int N = w * h;

  // Pre-compute blurred version if sharpness or noise-reduce is active
  std::vector<uint8_t> blurred;
  bool needBlur = (p.sharpness > 0.001f || p.noiseReduce > 0.001f);
  if (needBlur)
    blurred = boxBlur3(orig, w, h);

  static uint8_t noiseCache[64 * 64] = {};
  static bool noiseReady = false;
  if (!noiseReady) {
    unsigned s = noiseSeed;
    for (auto &v : noiseCache) {
      s = s * 1664525u + 1013904223u;
      v = uint8_t(s >> 24);
    }
    noiseReady = true;
  }
  auto noiseAt = [&](int x, int y) -> float {
    return noiseCache[((y & 63) * 64 + (x & 63))] / 255.f - 0.5f;
  };

  for (int i = 0; i < N; ++i) {
    int x = i % w, y = i / w;
    float r, g, b;

    if (needBlur) {
      float blurR = blurred[i * 4 + 0] / 255.f;
      float blurG = blurred[i * 4 + 1] / 255.f;
      float blurB = blurred[i * 4 + 2] / 255.f;
      float origR = orig[i * 4 + 0] / 255.f;
      float origG = orig[i * 4 + 1] / 255.f;
      float origB = orig[i * 4 + 2] / 255.f;
      float nr = clamp01(p.noiseReduce);
      // Blend toward blur then add unsharp mask
      r = clamp01((origR * (1.f - nr) + blurR * nr) +
                  p.sharpness * 0.6f * (origR - blurR));
      g = clamp01((origG * (1.f - nr) + blurG * nr) +
                  p.sharpness * 0.6f * (origG - blurG));
      b = clamp01((origB * (1.f - nr) + blurB * nr) +
                  p.sharpness * 0.6f * (origB - blurB));
    } else {
      r = orig[i * 4 + 0] / 255.f;
      g = orig[i * 4 + 1] / 255.f;
      b = orig[i * 4 + 2] / 255.f;
    }

    // ── Linear space ──────────────────────────────────────────────────────
    r = toLinear(r);
    g = toLinear(g);
    b = toLinear(b);

    // Exposure
    float expMul = std::pow(2.f, p.exposure);
    r *= expMul;
    g *= expMul;
    b *= expMul;

    // Contrast (pivot at 0.18 grey)
    constexpr float pv = 0.18f;
    r = clamp01((r - pv) * (1.f + p.contrast) + pv);
    g = clamp01((g - pv) * (1.f + p.contrast) + pv);
    b = clamp01((b - pv) * (1.f + p.contrast) + pv);

    // Highlights / Shadows
    {
      float l = luma(r, g, b);
      float hm = highlightMask(l) * 0.7f;
      float sm = shadowMask(l) * 0.7f;
      r = clamp01(r * (1.f + p.highlights * hm) * (1.f + p.shadows * sm));
      g = clamp01(g * (1.f + p.highlights * hm) * (1.f + p.shadows * sm));
      b = clamp01(b * (1.f + p.highlights * hm) * (1.f + p.shadows * sm));
    }

    // Whites / Blacks
    r = clamp01(r * (1.f + p.whites * 0.3f) - p.blacks * 0.15f);
    g = clamp01(g * (1.f + p.whites * 0.3f) - p.blacks * 0.15f);
    b = clamp01(b * (1.f + p.whites * 0.3f) - p.blacks * 0.15f);

    // RGB curve LUT
    r = applyLUT256(p.lutRGB.data(), r);
    g = applyLUT256(p.lutRGB.data(), g);
    b = applyLUT256(p.lutRGB.data(), b);
    r = applyLUT256(p.lutR.data(), r);
    g = applyLUT256(p.lutG.data(), g);
    b = applyLUT256(p.lutB.data(), b);
    r = clamp01(r);
    g = clamp01(g);
    b = clamp01(b);

    // Temperature / Tint
    {
      float t = p.temperature * 0.15f, gt = p.tint * 0.10f;
      r = clamp01(r + t + gt);
      g = clamp01(g - gt);
      b = clamp01(b - t + gt);
    }

    // Back to sRGB for saturation / HSL operations
    r = toSRGB(r);
    g = toSRGB(g);
    b = toSRGB(b);

    // Saturation
    {
      HSL hsl = rgb2hsl(r, g, b);
      hsl.s = clamp01(hsl.s * (1.f + p.saturation));
      hsl2rgb(hsl, r, g, b);
    }

    // Vibrance (targets unsaturated pixels more)
    {
      HSL hsl = rgb2hsl(r, g, b);
      float mask = (1.f - hsl.s) * (1.f - hsl.s);
      hsl.s = clamp01(hsl.s + p.vibrance * mask * 0.8f);
      hsl2rgb(hsl, r, g, b);
    }
    r = clamp01(r);
    g = clamp01(g);
    b = clamp01(b);

    // HSL panel (per-hue adjustments)
    {
      HSL hsl = rgb2hsl(r, g, b);
      float hueNorm = hsl.h; // 0..1

      float hShift =
          (sampleHSL360(p.hslHue.data(), hueNorm) - 0.5f) * (60.f / 360.f);
      hsl.h = std::fmod(hsl.h + hShift + 1.f, 1.f);

      float sScale = sampleHSL360(p.hslSat.data(), hueNorm) * 2.f;
      hsl.s = clamp01(hsl.s * sScale);

      float lShift = (sampleHSL360(p.hslLum.data(), hueNorm) - 0.5f) * 2.f;
      hsl.l = clamp01(hsl.l + lShift * 0.5f);

      hsl2rgb(hsl, r, g, b);
      r = clamp01(r);
      g = clamp01(g);
      b = clamp01(b);
    }

    // Vignette
    {
      float uvx = float(x) / float(w) * 2.f - 1.f;
      float uvy = float(y) / float(h) * 2.f - 1.f;
      float dist = uvx * uvx + uvy * uvy;
      float mask = std::min(1.f, std::max(0.f, (dist - 0.5f) / (1.6f - 0.5f)));
      // smooth step approximation
      mask = mask * mask * (3.f - 2.f * mask);
      float vig = 1.f - mask * (-p.vignette) * 0.8f;
      r = clamp01(r * vig);
      g = clamp01(g * vig);
      b = clamp01(b * vig);
    }

    // Grain
    if (p.grain > 0.001f) {
      float n = noiseAt(x * 5, y * 5);
      r = clamp01(r + n * p.grain * 0.12f);
      g = clamp01(g + n * p.grain * 0.12f);
      b = clamp01(b + n * p.grain * 0.12f);
    }

    out[i * 4 + 0] = uint8_t(r * 255.f + 0.5f);
    out[i * 4 + 1] = uint8_t(g * 255.f + 0.5f);
    out[i * 4 + 2] = uint8_t(b * 255.f + 0.5f);
    out[i * 4 + 3] = orig[i * 4 + 3]; // preserve alpha
  }
}

} // namespace cpu_edit

static const char *kEditVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=uMVP*vec4(aPos,0.0,1.0); }
)GLSL";

static const char *kEditFrag = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;

uniform sampler2D uOriginal;
uniform sampler2D uNoise;
uniform sampler2D uLutRGB;
uniform sampler2D uLutR;
uniform sampler2D uLutG;
uniform sampler2D uLutB;
uniform sampler2D uHSLHue;
uniform sampler2D uHSLSat;
uniform sampler2D uHSLLum;

uniform float uExposure, uContrast, uHighlights, uShadows, uWhites, uBlacks;
uniform float uTemperature, uTint, uSaturation, uVibrance;
uniform float uSharpness, uNoiseReduce;
uniform vec2  uTexelSize;
uniform float uVignette, uGrain;

vec3 toLinear(vec3 c){ return pow(clamp(c,0.0,1.0),vec3(2.2)); }
vec3 toSRGB  (vec3 c){ return pow(clamp(c,0.0,1.0),vec3(1.0/2.2)); }
float luma(vec3 c){ return dot(c,vec3(0.2126,0.7152,0.0722)); }

vec3 rgb2hsl(vec3 c){
    float mx=max(c.r,max(c.g,c.b)), mn=min(c.r,min(c.g,c.b)), d=mx-mn;
    float l=(mx+mn)*0.5;
    if(d<0.0001) return vec3(0,0,l);
    float s=d/(1.0-abs(2.0*l-1.0)), h;
    if(mx==c.r)      h=mod((c.g-c.b)/d,6.0)/6.0;
    else if(mx==c.g) h=((c.b-c.r)/d+2.0)/6.0;
    else             h=((c.r-c.g)/d+4.0)/6.0;
    return vec3(h,s,l);
}
float hue2rgb(float p,float q,float t){
    if(t<0.0)t+=1.0; if(t>1.0)t-=1.0;
    if(t<1.0/6.0) return p+(q-p)*6.0*t;
    if(t<0.5)     return q;
    if(t<2.0/3.0) return p+(q-p)*(2.0/3.0-t)*6.0;
    return p;
}
vec3 hsl2rgb(vec3 h){
    if(h.y<0.0001) return vec3(h.z);
    float q=h.z<0.5?h.z*(1.0+h.y):h.z+h.y-h.z*h.y, p=2.0*h.z-q;
    return vec3(hue2rgb(p,q,h.x+1.0/3.0),hue2rgb(p,q,h.x),hue2rgb(p,q,h.x-1.0/3.0));
}
float applyLUT256(sampler2D lut, float v){
    return texture(lut, vec2(clamp(v,0.0,1.0), 0.5)).r;
}
float sampleHSLLut(sampler2D lut, float hueNorm){
    return texture(lut, vec2(clamp(hueNorm,0.0,1.0), 0.5)).r;
}
float shadowMask   (float l){ return pow(1.0-l,2.0); }
float highlightMask(float l){ return pow(l,    2.0); }

void main(){
    vec3 c;
    if(uSharpness>0.001||uNoiseReduce>0.001){
        vec3 blur=vec3(0);
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++)
            blur+=texture(uOriginal,vUV+vec2(dx,dy)*uTexelSize).rgb;
        blur/=9.0;
        vec3 orig=texture(uOriginal,vUV).rgb;
        vec3 blended=mix(orig,blur,clamp(uNoiseReduce,0.0,1.0));
        c=clamp(blended+uSharpness*0.6*(orig-blur),0.0,1.0);
    } else {
        c=texture(uOriginal,vUV).rgb;
    }
    c=toLinear(c);
    c*=pow(2.0,uExposure);
    { float pv=0.18; c=(c-pv)*(1.0+uContrast)+pv; c=clamp(c,0.0,1.0); }
    { float l=luma(c);
      c*=1.0+uHighlights*highlightMask(l)*0.7;
      c*=1.0+uShadows*shadowMask(l)*0.7;
      c=clamp(c,0.0,1.0); }
    c=clamp(c*(1.0+uWhites*0.3)-uBlacks*0.15,0.0,1.0);
    c.r=applyLUT256(uLutRGB,c.r); c.g=applyLUT256(uLutRGB,c.g); c.b=applyLUT256(uLutRGB,c.b);
    c.r=applyLUT256(uLutR,  c.r); c.g=applyLUT256(uLutG,  c.g); c.b=applyLUT256(uLutB,  c.b);
    c=clamp(c,0.0,1.0);
    { float t=uTemperature*0.15, g=uTint*0.10;
      c.r=clamp(c.r+t,0.0,1.0); c.b=clamp(c.b-t,0.0,1.0);
      c.r=clamp(c.r+g,0.0,1.0); c.g=clamp(c.g-g,0.0,1.0); c.b=clamp(c.b+g,0.0,1.0); }
    c=toSRGB(c);
    { vec3 hsl=rgb2hsl(c); hsl.y=clamp(hsl.y*(1.0+uSaturation),0.0,1.0); c=hsl2rgb(hsl); }
    { vec3 hsl=rgb2hsl(c); float sat=hsl.y, mask=(1.0-sat)*(1.0-sat);
      hsl.y=clamp(hsl.y+uVibrance*mask*0.8,0.0,1.0); c=hsl2rgb(hsl); }
    c=clamp(c,0.0,1.0);
    { vec3 hsl=rgb2hsl(c); float hueNorm=hsl.x;
      float hShift=(sampleHSLLut(uHSLHue,hueNorm)-0.5)*(60.0/360.0);
      hsl.x=fract(hsl.x+hShift);
      float sScale=sampleHSLLut(uHSLSat,hueNorm)*2.0;
      hsl.y=clamp(hsl.y*sScale,0.0,1.0);
      float lShift=(sampleHSLLut(uHSLLum,hueNorm)-0.5)*2.0;
      hsl.z=clamp(hsl.z+lShift*0.5,0.0,1.0);
      c=hsl2rgb(hsl); c=clamp(c,0.0,1.0); }
    { vec2 uv2=vUV*2.0-1.0; float dist=dot(uv2,uv2);
      float mask=smoothstep(0.5,1.6,dist);
      c=clamp(c*(1.0-mask*(-uVignette)*0.8),0.0,1.0); }
    if(uGrain>0.001){
        vec3 n=texture(uNoise,vUV*5.0).rgb;
        c=clamp(c+(n.r-0.5)*uGrain*0.12,0.0,1.0); }
    fragColor=vec4(c,1.0);
}
)GLSL";

class ImageEditSurface : public RenderSurface {
public:
  std::function<void()> onImageLoaded;
  std::function<void(const char *)> onStatusMessage;
  std::function<void(const HistogramData &)> onHistogramUpdated;

  bool loadImage(const std::string &path) {
    FILE *f = FluxFile::open(path, "rb");
    if (!f) {
      status("Failed to open image.");
      return false;
    }
    int w, h, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char *data = stbi_load_from_file(f, &w, &h, &ch, 4);
    fclose(f);
    if (!data) {
      status("Failed to decode image.");
      return false;
    }

    imgW_ = w;
    imgH_ = h;
    originalPixels_.assign(data, data + size_t(w) * h * 4);
    stbi_image_free(data);

    uploadOriginal();
    params_.reset();
    buildIdentityLUTs();
    buildIdentityHSLLUTs();
    dirty_ = true;

    computeHistogram();
    if (onImageLoaded)
      onImageLoaded();
    char msg[64];
    snprintf(msg, sizeof(msg), "Loaded %dx%d", w, h);
    status(msg);
    return true;
  }

  bool exportImage(const std::string &path) {
    if (originalPixels_.empty() || !editFBO_)
      return false;
    renderEditPass(editFBO_);

    std::vector<uint8_t> buf(size_t(imgW_) * imgH_ * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, editFBO_);
    glReadPixels(0, 0, imgW_, imgH_, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    std::vector<uint8_t> flipped(buf.size());
    int stride = imgW_ * 4;
    for (int r = 0; r < imgH_; ++r)
      memcpy(flipped.data() + r * stride, buf.data() + (imgH_ - 1 - r) * stride,
             stride);

    bool ok = false;
    std::string ext = FluxFile::extension(path);
    if (ext == ".jpg" || ext == ".jpeg") {
      FILE *f = FluxFile::open(path, "wb");
      if (f) {
        ok = stbi_write_jpg_to_func(
                 [](void *c, void *d, int s) { fwrite(d, 1, s, (FILE *)c); }, f,
                 imgW_, imgH_, 4, flipped.data(), 95) != 0;
        fclose(f);
      }
    } else {
      FILE *f = FluxFile::open(path, "wb");
      if (f) {
        ok = stbi_write_png_to_func(
                 [](void *c, void *d, int s) { fwrite(d, 1, s, (FILE *)c); }, f,
                 imgW_, imgH_, 4, flipped.data(), stride) != 0;
        fclose(f);
      }
    }
    status(ok ? "Export saved." : "Export failed.");
    return ok;
  }

  bool hasImage() const { return !originalPixels_.empty(); }
  int imageWidth() const { return imgW_; }
  int imageHeight() const { return imgH_; }

  EditParams &params() { return params_; }
  const EditParams &params() const { return params_; }

  void markDirty() { dirty_ = true; }

  void uploadCurveLUTs(const std::array<uint8_t, 256> &rgb,
                       const std::array<uint8_t, 256> &r,
                       const std::array<uint8_t, 256> &g,
                       const std::array<uint8_t, 256> &b) {
    params_.lutRGB = rgb;
    params_.lutR = r;
    params_.lutG = g;
    params_.lutB = b;
    uploadOneLUT256(lutTexRGB_, rgb.data());
    uploadOneLUT256(lutTexR_, r.data());
    uploadOneLUT256(lutTexG_, g.data());
    uploadOneLUT256(lutTexB_, b.data());
    dirty_ = true;
  }

  void uploadHSLLUTs(const uint8_t *hue360, const uint8_t *sat360,
                     const uint8_t *lum360) {
    memcpy(params_.hslHue.data(), hue360, 360);
    memcpy(params_.hslSat.data(), sat360, 360);
    memcpy(params_.hslLum.data(), lum360, 360);
    uploadOneLUT360(hslTexHue_, hue360);
    uploadOneLUT360(hslTexSat_, sat360);
    uploadOneLUT360(hslTexLum_, lum360);
    dirty_ = true;
  }

  void computeHistogram() {
    if (originalPixels_.empty())
      return;
    histData_ =
        HistogramData::fromPixels(originalPixels_.data(), imgW_ * imgH_, 4);
    if (onHistogramUpdated)
      onHistogramUpdated(histData_);
  }

  const HistogramData &histogramData() const { return histData_; }

  // ── RenderSurface ─────────────────────────────────────────────────────────

  void initialize(int w, int h) override {
    viewW_ = w;
    viewH_ = h;
    buildShader();
    buildQuad();
    buildNoiseTexture();
    buildIdentityLUTs();
    buildIdentityHSLLUTs();
  }

  void resize(int w, int h) override {
    viewW_ = w;
    viewH_ = h;
  }
  void update(double) override {}

  void preRender() override {
    if (originalPixels_.empty() || !origTex_ || !editFBO_)
      return;

    if (!dirty_)
      return;
    dirty_ = false;

    renderEditPass(editFBO_);
    nvgImageDirty_ = true;
  }

  void render(Canvas2D &ctx) override {
    ctx.setFillColor(Color::fromRGB(20, 20, 22));
    ctx.fillRect(0.f, 0.f, float(ctx.width()), float(ctx.height()));
    if (originalPixels_.empty())
      return;

    NVGcontext *nvg = ctx.nvg();

    if (!nvgCtx_ && nvg) {
      nvgCtx_ = nvg;

      nvgImageHandle_ =
          nvgCreateImageRGBA(nvg, imgW_, imgH_, NVG_IMAGE_FLIPY, nullptr);
      nvgImageDirty_ = true;
    }

    if (nvgImageHandle_ < 0)
      return;

    if (nvgImageDirty_ && nvgCtx_) {
      nvgImageDirty_ = false;
      pullFBOIntoNVGImage();
    }

    NVGpaint paint = nvgImagePattern(nvg, 0.f, 0.f, float(imgW_), float(imgH_),
                                     0.f, nvgImageHandle_, 1.f);
    nvgBeginPath(nvg);
    nvgRect(nvg, 0.f, 0.f, float(imgW_), float(imgH_));
    nvgFillPaint(nvg, paint);
    nvgFill(nvg);
  }

  void destroy() override {

    if (nvgImageHandle_ >= 0 && nvgCtx_) {
      nvgDeleteImage(nvgCtx_, nvgImageHandle_);
      nvgImageHandle_ = -1;
    }
    nvgCtx_ = nullptr;

    auto del = [](GLuint &t) {
      if (t) {
        glDeleteTextures(1, &t);
        t = 0;
      }
    };
    del(origTex_);
    del(noiseTex_);
    del(lutTexRGB_);
    del(lutTexR_);
    del(lutTexG_);
    del(lutTexB_);
    del(hslTexHue_);
    del(hslTexSat_);
    del(hslTexLum_);
    del(editTex_);

    if (editFBO_) {
      glDeleteFramebuffers(1, &editFBO_);
      editFBO_ = 0;
    }
    if (editProg_) {
      glDeleteProgram(editProg_);
      editProg_ = 0;
    }
    if (quadVAO_) {
      glDeleteVertexArrays(1, &quadVAO_);
      quadVAO_ = 0;
    }
    if (quadVBO_) {
      glDeleteBuffers(1, &quadVBO_);
      quadVBO_ = 0;
    }
  }

  bool needsContinuousRedraw() const override { return false; }

private:
  // ── State ─────────────────────────────────────────────────────────────────
  int viewW_ = 1, viewH_ = 1, imgW_ = 0, imgH_ = 0;
  EditParams params_;
  HistogramData histData_;
  std::vector<uint8_t> originalPixels_;
  bool dirty_ = false;

  bool nvgImageDirty_ = false;
  std::vector<uint8_t> fboReadback_;

  GLuint editProg_ = 0, quadVAO_ = 0, quadVBO_ = 0;
  GLuint origTex_ = 0, noiseTex_ = 0;
  GLuint editFBO_ = 0, editTex_ = 0;
  GLuint lutTexRGB_ = 0, lutTexR_ = 0, lutTexG_ = 0, lutTexB_ = 0;
  GLuint hslTexHue_ = 0, hslTexSat_ = 0, hslTexLum_ = 0;

  // NanoVG wrapper around editTex_
  NVGcontext *nvgCtx_ = nullptr;
  int nvgImageHandle_ = -1;

  void pullFBOIntoNVGImage() {
    if (!editTex_ || nvgImageHandle_ < 0 || !nvgCtx_)
      return;

    size_t bytes = size_t(imgW_) * imgH_ * 4;
    if (fboReadback_.size() != bytes)
      fboReadback_.resize(bytes);

    // Read the rendered pixels back from the GPU texture.
    glBindTexture(GL_TEXTURE_2D, editTex_);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                  fboReadback_.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    nvgUpdateImage(nvgCtx_, nvgImageHandle_, fboReadback_.data());
  }

  void buildShader() {
    editProg_ = glutil::linkProgram(kEditVert, kEditFrag);
    assert(editProg_);
  }

  void buildQuad() {
    glGenVertexArrays(1, &quadVAO_);
    glGenBuffers(1, &quadVBO_);
    glBindVertexArray(quadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)8);
    glBindVertexArray(0);
  }

  void uploadQuadForSize(float w, float h) {
    float v[] = {
        0.f, 0.f, 0.f, 0.f, w,   0.f, 1.f, 0.f, w,   h,   1.f, 1.f,
        w,   h,   1.f, 1.f, 0.f, h,   0.f, 1.f, 0.f, 0.f, 0.f, 0.f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  void buildNoiseTexture() {
    const int N = 64;
    std::vector<uint8_t> noise(N * N * 3);
    srand(42);
    for (auto &v : noise)
      v = uint8_t(rand() & 0xFF);
    glGenTextures(1, &noiseTex_);
    glBindTexture(GL_TEXTURE_2D, noiseTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, N, N, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 noise.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void uploadOneLUT256(GLuint &tex, const uint8_t *data) {
    if (!tex) {
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
      glBindTexture(GL_TEXTURE_2D, tex);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 1, 0, GL_RED, GL_UNSIGNED_BYTE,
                 data);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void uploadOneLUT360(GLuint &tex, const uint8_t *data) {
    if (!tex) {
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
      glBindTexture(GL_TEXTURE_2D, tex);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 360, 1, 0, GL_RED, GL_UNSIGNED_BYTE,
                 data);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void buildIdentityLUTs() {
    uint8_t id[256];
    for (int i = 0; i < 256; ++i)
      id[i] = uint8_t(i);
    uploadOneLUT256(lutTexRGB_, id);
    uploadOneLUT256(lutTexR_, id);
    uploadOneLUT256(lutTexG_, id);
    uploadOneLUT256(lutTexB_, id);
  }

  void buildIdentityHSLLUTs() {
    uint8_t id[360];
    memset(id, 128, 360);
    uploadOneLUT360(hslTexHue_, id);
    uploadOneLUT360(hslTexSat_, id);
    uploadOneLUT360(hslTexLum_, id);
  }

  void uploadOriginal() {
    if (origTex_) {
      glDeleteTextures(1, &origTex_);
      origTex_ = 0;
    }
    glGenTextures(1, &origTex_);
    glBindTexture(GL_TEXTURE_2D, origTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imgW_, imgH_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, originalPixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    rebuildEditFBO();
    computeHistogram();
  }

  void rebuildEditFBO() {
    // Invalidate NVG image — wrong size now
    if (nvgImageHandle_ >= 0 && nvgCtx_) {
      nvgDeleteImage(nvgCtx_, nvgImageHandle_);
      nvgImageHandle_ = -1;
    }
    nvgImageDirty_ = false;

    if (editFBO_) {
      glDeleteFramebuffers(1, &editFBO_);
      editFBO_ = 0;
    }
    if (editTex_) {
      glDeleteTextures(1, &editTex_);
      editTex_ = 0;
    }

    glGenTextures(1, &editTex_);
    glBindTexture(GL_TEXTURE_2D, editTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imgW_, imgH_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &editFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, editFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           editTex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  void setUniforms() {
    auto loc = [&](const char *n) {
      return glGetUniformLocation(editProg_, n);
    };
    const EditParams &p = params_;
    glUniform1f(loc("uExposure"), p.exposure);
    glUniform1f(loc("uContrast"), p.contrast);
    glUniform1f(loc("uHighlights"), p.highlights);
    glUniform1f(loc("uShadows"), p.shadows);
    glUniform1f(loc("uWhites"), p.whites);
    glUniform1f(loc("uBlacks"), p.blacks);
    glUniform1f(loc("uTemperature"), p.temperature);
    glUniform1f(loc("uTint"), p.tint);
    glUniform1f(loc("uSaturation"), p.saturation);
    glUniform1f(loc("uVibrance"), p.vibrance);
    glUniform1f(loc("uSharpness"), p.sharpness);
    glUniform1f(loc("uNoiseReduce"), p.noiseReduce);
    glUniform1f(loc("uVignette"), p.vignette);
    glUniform1f(loc("uGrain"), p.grain);
    glUniform2f(loc("uTexelSize"), 1.f / float(imgW_), 1.f / float(imgH_));
    glUniform1i(loc("uOriginal"), 0);
    glUniform1i(loc("uNoise"), 1);
    glUniform1i(loc("uLutRGB"), 2);
    glUniform1i(loc("uLutR"), 3);
    glUniform1i(loc("uLutG"), 4);
    glUniform1i(loc("uLutB"), 5);
    glUniform1i(loc("uHSLHue"), 6);
    glUniform1i(loc("uHSLSat"), 7);
    glUniform1i(loc("uHSLLum"), 8);
  }

  void buildImageMVP(float out[16]) const {
    float l = 0, r = float(imgW_), b = 0, t = float(imgH_);
    float rml = r - l, tmb = t - b;
    out[0] = 2.f / rml;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;
    out[5] = 2.f / tmb;
    out[6] = 0;
    out[7] = 0;
    out[8] = 0;
    out[9] = 0;
    out[10] = -1;
    out[11] = 0;
    out[12] = -(r + l) / rml;
    out[13] = -(t + b) / tmb;
    out[14] = 0;
    out[15] = 1;
  }

  void renderEditPass(GLuint fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, imgW_, imgH_);

    float mvp[16];
    buildImageMVP(mvp);

    glUseProgram(editProg_);
    glUniformMatrix4fv(glGetUniformLocation(editProg_, "uMVP"), 1, GL_FALSE,
                       mvp);
    setUniforms();

    auto bind = [](int unit, GLuint tex) {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, tex);
    };
    bind(0, origTex_);
    bind(1, noiseTex_);
    bind(2, lutTexRGB_);
    bind(3, lutTexR_);
    bind(4, lutTexG_);
    bind(5, lutTexB_);
    bind(6, hslTexHue_);
    bind(7, hslTexSat_);
    bind(8, hslTexLum_);

    uploadQuadForSize(float(imgW_), float(imgH_));
    glBindVertexArray(quadVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void status(const char *msg) {
    if (onStatusMessage)
      onStatusMessage(msg);
  }
};

class ImageEditorApp : public Widget {
  // ── UI state ──────────────────────────────────────────────────────────────
  State<std::string> statusMsg{"Open an image to begin."};
  State<bool> hasImage{false};
  State<bool> beforeMode{false};
  State<double> zoomLevel{100.0};
  State<HistogramData> histState{HistogramData{}};
  State<ToneCurveData> curveState{ToneCurveData{}};
  State<HSLData> hslState{HSLData{}};

  // ── Tone ──────────────────────────────────────────────────────────────────
  State<double> sExposure{0.0}, sContrast{0.0}, sHighlights{0.0}, sShadows{0.0},
      sWhites{0.0}, sBlacks{0.0};
  // ── Color ─────────────────────────────────────────────────────────────────
  State<double> sTemperature{0.0}, sTint{0.0}, sSaturation{0.0}, sVibrance{0.0};
  // ── Detail ────────────────────────────────────────────────────────────────
  State<double> sSharpness{0.0}, sNoiseReduce{0.0};
  // ── Effects ───────────────────────────────────────────────────────────────
  State<double> sVignette{0.0}, sGrain{0.0};

  std::shared_ptr<ImageEditSurface> surface_;
  ToneCurveWidget *curveWidgetPtr_ = nullptr;
  HSLPanelWidget *hslWidgetPtr_ = nullptr;
  CanvasWidget *canvasPtr_ = nullptr;

  static constexpr int kPanelW = 290;
  static constexpr int kToolbarH = 44;
  static constexpr int kHintsH = 22;

  template <typename F> void wire(State<double> &st, F EditParams::*field) {
    st.listen([this, field](double v) {
      if (!surface_)
        return;
      surface_->params().*field = float(v);

      surface_->markDirty();
      if (canvasPtr_)
        canvasPtr_->redraw();
    });
  }

public:
  WidgetPtr build() override {
    int screenW = 1920;
    int screenH = 1080;
    int viewW = screenW - kPanelW;
    int viewH = screenH - kToolbarH - kHintsH;

    auto canvas = std::make_shared<CanvasWidget>()->setSize(viewW, viewH);
    canvas->setCanvasSize(1, 1);
    canvas->setScrollbarsEnabled(false);
    surface_ = canvas->setSurface<ImageEditSurface>();
    canvasPtr_ = canvas.get();

    canvas->onViewportChanged = [this](float z) {
      if (syncingZoom_)
        return;
      double pct = double(z) * 100.0;
      if (std::abs(pct - zoomLevel.get()) > 0.5) {
        syncingZoom_ = true;
        zoomLevel.set(pct);
        syncingZoom_ = false;
      }
    };

    surface_->onImageLoaded = [this]() {
      hasImage.set(true);
      if (canvasPtr_) {
        canvasPtr_->setCanvasSize(surface_->imageWidth(),
                                  surface_->imageHeight());
        canvasPtr_->viewport().fitToView();
        canvasPtr_->redraw();
        zoomLevel.set(double(canvasPtr_->viewport().zoom()) * 100.0);
      }
    };
    surface_->onStatusMessage = [this](const char *m) { statusMsg.set(m); };
    surface_->onHistogramUpdated = [this](const HistogramData &d) {
      histState.set(d);
      if (curveWidgetPtr_)
        curveWidgetPtr_->setHistogram(d.r, d.g, d.b);
    };

    wire(sExposure, &EditParams::exposure);
    wire(sContrast, &EditParams::contrast);
    wire(sHighlights, &EditParams::highlights);
    wire(sShadows, &EditParams::shadows);
    wire(sWhites, &EditParams::whites);
    wire(sBlacks, &EditParams::blacks);
    wire(sTemperature, &EditParams::temperature);
    wire(sTint, &EditParams::tint);
    wire(sSaturation, &EditParams::saturation);
    wire(sVibrance, &EditParams::vibrance);
    wire(sSharpness, &EditParams::sharpness);
    wire(sNoiseReduce, &EditParams::noiseReduce);
    wire(sVignette, &EditParams::vignette);
    wire(sGrain, &EditParams::grain);

    zoomLevel.listen([this](double pct) {
      if (syncingZoom_ || !canvasPtr_)
        return;
      Viewport &vp = canvasPtr_->viewport();
      float target = float(pct / 100.0);
      float cur = vp.zoom();
      if (std::abs(target - cur) < 0.0001f)
        return;
      syncingZoom_ = true;
      vp.zoomToward(vp.viewW() * 0.5f, vp.viewH() * 0.5f, target / cur);
      canvasPtr_->redraw();
      syncingZoom_ = false;
    });

    // ── Colors
    // ────────────────────────────────────────────────────────────────
    Color kBg1 = Color::fromRGB(17, 17, 27);
    Color kBg2 = Color::fromRGB(24, 24, 37);
    Color kBorder = Color::fromRGB(49, 50, 68);
    Color kText = Color::fromRGB(205, 214, 244);
    Color kDim = Color::fromRGB(100, 105, 130);
    Color kAccent = Color::fromRGB(174, 129, 255);
    Color kGreen = Color::fromRGB(148, 226, 213);
    Color kOrange = Color::fromRGB(250, 179, 135);

    auto sectionLabel = [&](const std::string &t) -> WidgetPtr {
      return Text(t)
          ->setFontSize(9)
          ->setTextColor(Color::fromRGB(100, 105, 125))
          ->setFontWeight(FontWeight::Bold);
    };
    auto cardWrap = [&](WidgetPtr child) -> WidgetPtr {
      return Container(child)
          ->setBackgroundColor(kBg2)
          ->setBorderRadius(8)
          ->setBorderWidth(1)
          ->setBorderColor(kBorder)
          ->setPaddingAll(10, 10, 10, 10);
    };
    auto makeSlider = [&](const std::string &label, double lo, double hi,
                          double step, State<double> &st,
                          Color col =
                              Color::fromRGB(174, 129, 255)) -> WidgetPtr {
      return Column({
                        Row({
                                Text(label)
                                    ->setFontSize(9)
                                    ->setTextColor(kDim)
                                    ->setMinWidth(80),
                                SizedBox(4, 0),
                                Text(st,
                                     [](double v) {
                                       char b[16];
                                       snprintf(b, sizeof(b), "%.2f", v);
                                       return std::string(b);
                                     })
                                    ->setFontSize(9)
                                    ->setTextColor(kText)
                                    ->setMinWidth(36),
                            })
                            ->setSpacing(0)
                            ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                        SizedBox(0, 4),
                        Slider(lo, hi, step)
                            ->setValue(st)
                            ->setTrackFillColor(col)
                            ->setWidth(258),
                    })
          ->setSpacing(0);
    };

    // ──   HISTOGRAM
    // ─────────────────────────────────────────────────────────
    auto histSection = cardWrap(
        Column({
                   Row({
                           sectionLabel("HISTOGRAM"),
                           SizedBox(4, 0),
                           Text(hasImage,
                                [](bool b) -> std::string {
                                  return b ? "" : " (no image)";
                                })
                               ->setFontSize(9)
                               ->setTextColor(kDim),
                       })
                       ->setSpacing(0)
                       ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                   SizedBox(0, 6),
                   Histogram(258, 90)
                       ->setData(histState)
                       ->setMode(HistogramMode::RGB)
                       ->setShowGrid(true)
                       ->setShowClip(true)
                       ->setShowChannelToggles(true)
                       ->setLogScale(false)
                       ->setBgColor(Color::fromRGB(14, 14, 22))
                       ->setOnZoneClicked([this](float pos) {
                         sExposure.set(
                             std::max(-5.0, std::min(5.0, (pos - 0.5) * 4.0)));
                       }),
               })
            ->setSpacing(0));

    // ──   TONE CURVE
    // ────────────────────────────────────────────────────────
    auto curveWidget =
        ToneCurve(258, 220)
            ->setShowHistogram(true)
            ->setShowRegions(true)
            ->setShowGrid(true)
            ->setCurveData(curveState)
            ->setOnCurveChanged([this](const ToneCurveData &d) {
              curveState.set(d);
              if (!surface_)
                return;
              surface_->uploadCurveLUTs(d.rgb.buildLUT(), d.r.buildLUT(),
                                        d.g.buildLUT(), d.b.buildLUT());
              if (canvasPtr_)
                canvasPtr_->redraw();
            });
    curveWidgetPtr_ = curveWidget.get();

    auto modeBtn = [&](const std::string &lbl, Color col,
                       std::function<void()> fn) -> WidgetPtr {
      return GestureDetector(
                 Container(Text(lbl)->setFontSize(9)->setTextColor(col))
                     ->setBackgroundColor(Color::fromRGB(24, 24, 34))
                     ->setBorderRadius(4)
                     ->setPaddingAll(6, 3, 6, 3)
                     ->setBorderWidth(1)
                     ->setBorderColor(kBorder))
          ->setOnTap(fn);
    };

    auto curveModeRow =
        Row({
                modeBtn(
                    "Point Curve", kAccent,
                    [cw = curveWidget.get()] { cw->setParametricMode(false); }),
                SizedBox(5, 0),
                modeBtn(
                    "Parametric", kDim,
                    [cw = curveWidget.get()] { cw->setParametricMode(true); }),
                SizedBox(5, 0),
                modeBtn("Reset", Color::fromRGB(200, 100, 100),
                        [this, cw = curveWidget.get()] {
                          cw->resetCurve();
                          curveState.set(ToneCurveData{});
                          if (surface_) {
                            ToneCurveData id;
                            surface_->uploadCurveLUTs(
                                id.rgb.buildLUT(), id.r.buildLUT(),
                                id.g.buildLUT(), id.b.buildLUT());
                            if (canvasPtr_)
                              canvasPtr_->redraw();
                          }
                        }),
            })
            ->setSpacing(0)
            ->setCrossAxisAlignment(CrossAxisAlignment::Center);

    auto curveSection = cardWrap(
        Column(
            {
                Row({sectionLabel("TONE CURVE"), SizedBox(8, 0), curveModeRow})
                    ->setSpacing(0)
                    ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                SizedBox(0, 8),
                curveWidget,
            })
            ->setSpacing(0));

    // ──   TONE SLIDERS
    // ──────────────────────────────────────────────────────
    auto toneSection = cardWrap(
        Column({
                   sectionLabel("TONE"),
                   SizedBox(0, 8),
                   makeSlider("Exposure", -5.0, 5.0, 0.05, sExposure,
                              Color::fromRGB(250, 220, 100)),
                   SizedBox(0, 6),
                   makeSlider("Contrast", -1.0, 1.0, 0.01, sContrast, kAccent),
                   SizedBox(0, 6),
                   makeSlider("Highlights", -1.0, 1.0, 0.01, sHighlights,
                              Color::fromRGB(200, 200, 220)),
                   SizedBox(0, 6),
                   makeSlider("Shadows", -1.0, 1.0, 0.01, sShadows,
                              Color::fromRGB(80, 100, 160)),
                   SizedBox(0, 6),
                   makeSlider("Whites", -1.0, 1.0, 0.01, sWhites,
                              Color::fromRGB(230, 230, 230)),
                   SizedBox(0, 6),
                   makeSlider("Blacks", -1.0, 1.0, 0.01, sBlacks,
                              Color::fromRGB(60, 60, 80)),
               })
            ->setSpacing(0));

    // ──   COLOR
    // ─────────────────────────────────────────────────────────────
    auto colorSection = cardWrap(
        Column({
                   sectionLabel("COLOR"),
                   SizedBox(0, 8),
                   makeSlider("Temperature", -1.0, 1.0, 0.01, sTemperature,
                              Color::fromRGB(100, 180, 250)),
                   SizedBox(0, 6),
                   makeSlider("Tint", -1.0, 1.0, 0.01, sTint,
                              Color::fromRGB(200, 120, 200)),
                   SizedBox(0, 6),
                   makeSlider("Saturation", -1.0, 1.0, 0.01, sSaturation,
                              Color::fromRGB(220, 120, 100)),
                   SizedBox(0, 6),
                   makeSlider("Vibrance", -1.0, 1.0, 0.01, sVibrance,
                              Color::fromRGB(170, 220, 120)),
               })
            ->setSpacing(0));

    // ──   HSL / COLOR MIX
    // ───────────────────────────────────────────────────
    auto hslWidget =
        HSLPanel(258)
            ->setData(hslState)
            ->setActiveTab(HSLTab::All)
            ->setOnHSLChanged([this](const HSLData &d) {
              hslState.set(d);
              if (!surface_)
                return;
              auto luts = d.buildGPULUTs();
              surface_->uploadHSLLUTs(luts.hue.data(), luts.sat.data(),
                                      luts.lum.data());
              if (canvasPtr_)
                canvasPtr_->redraw();
            });
    hslWidgetPtr_ = hslWidget.get();

    auto hslResetBtn =
        modeBtn("Reset", Color::fromRGB(200, 100, 100), [this]() {
          if (hslWidgetPtr_)
            hslWidgetPtr_->resetAll();
          hslState.set(HSLData{});
          if (surface_) {
            HSLData id;
            auto luts = id.buildGPULUTs();
            surface_->uploadHSLLUTs(luts.hue.data(), luts.sat.data(),
                                    luts.lum.data());
            if (canvasPtr_)
              canvasPtr_->redraw();
          }
        });

    auto hslSection = cardWrap(
        Column({
                   Row({sectionLabel("HSL / COLOR MIX"), SizedBox(8, 0),
                        hslResetBtn})
                       ->setSpacing(0)
                       ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                   SizedBox(0, 8),
                   hslWidget,
               })
            ->setSpacing(0));

    // ──   DETAIL
    // ────────────────────────────────────────────────────────────
    auto detailSection = cardWrap(
        Column({
                   sectionLabel("DETAIL"),
                   SizedBox(0, 8),
                   makeSlider("Sharpness", 0.0, 1.0, 0.01, sSharpness, kAccent),
                   SizedBox(0, 6),
                   makeSlider("Noise Reduce", 0.0, 1.0, 0.01, sNoiseReduce,
                              kGreen),
               })
            ->setSpacing(0));

    // ──   EFFECTS
    // ───────────────────────────────────────────────────────────
    auto effectsSection =
        cardWrap(Column({
                            sectionLabel("EFFECTS"),
                            SizedBox(0, 8),
                            makeSlider("Vignette", -1.0, 1.0, 0.01, sVignette,
                                       Color::fromRGB(80, 80, 100)),
                            SizedBox(0, 6),
                            makeSlider("Grain", 0.0, 1.0, 0.01, sGrain,
                                       Color::fromRGB(180, 160, 130)),
                        })
                     ->setSpacing(0));

    // ──   PANEL
    // ─────────────────────────────────────────────────────────────
    auto panel = Container(ListView({
                                        histSection,
                                        SizedBox(0, 6),
                                        curveSection,
                                        SizedBox(0, 6),
                                        toneSection,
                                        SizedBox(0, 6),
                                        colorSection,
                                        SizedBox(0, 6),
                                        hslSection,
                                        SizedBox(0, 6),
                                        detailSection,
                                        SizedBox(0, 6),
                                        effectsSection,
                                    })
                               ->setSpacing(0))
                     ->setWidth(kPanelW)
                     ->setBackgroundColor(kBg1)
                     ->setPaddingAll(10, 10, 10, 10);

    // ──   TOOLBAR
    // ───────────────────────────────────────────────────────────
    auto mkBtn = [&](const std::string &lbl, Color c,
                     std::function<void()> fn) -> WidgetPtr {
      return Button(lbl, fn)
          ->setBackgroundColor(Color::fromRGB(30, 30, 46))
          ->setTextColor(c)
          ->setBorderRadius(5)
          ->setHeight(26)
          ->setPadding(4);
    };

    auto toolbar =
        Container(
            Row({
                    FilePicker("📂 Open")
                        ->setMode(FilePickerMode::Open)
                        ->addFilter("Images", {"*.jpg", "*.jpeg", "*.png",
                                               "*.bmp", "*.tga"})
                        ->addFilter("All Files", {"*.*"})
                        ->setOnChanged([this](const std::string &path) {
                          if (surface_)
                            surface_->loadImage(path);
                        })
                        ->setShowPath(false)
                        ->setHeight(26)
                        ->setWidth(100),
                    SizedBox(8, 0),
                    FilePicker("💾 Export")
                        ->setMode(FilePickerMode::Save)
                        ->setTitle("Export Image")
                        ->setDefaultFilename("edited.png")
                        ->addFilter("PNG", {"*.png"})
                        ->addFilter("JPEG", {"*.jpg", "*.jpeg"})
                        ->setDefaultExtension("png")
                        ->setOnChanged([this](const std::string &path) {
                          if (surface_)
                            surface_->exportImage(path);
                        })
                        ->setShowPath(false)
                        ->setHeight(26)
                        ->setWidth(100),
                    SizedBox(8, 0),
                    mkBtn("↺ Reset All", kOrange,
                          [this]() {
                            if (!surface_)
                              return;
                            surface_->params().reset();
                            surface_->markDirty();
                            if (curveWidgetPtr_)
                              curveWidgetPtr_->resetCurve();
                            curveState.set(ToneCurveData{});
                            {
                              ToneCurveData id;
                              surface_->uploadCurveLUTs(
                                  id.rgb.buildLUT(), id.r.buildLUT(),
                                  id.g.buildLUT(), id.b.buildLUT());
                            }
                            if (hslWidgetPtr_)
                              hslWidgetPtr_->resetAll();
                            hslState.set(HSLData{});
                            {
                              HSLData id;
                              auto luts = id.buildGPULUTs();
                              surface_->uploadHSLLUTs(luts.hue.data(),
                                                      luts.sat.data(),
                                                      luts.lum.data());
                            }
                            sExposure.set(0);
                            sContrast.set(0);
                            sHighlights.set(0);
                            sShadows.set(0);
                            sWhites.set(0);
                            sBlacks.set(0);
                            sTemperature.set(0);
                            sTint.set(0);
                            sSaturation.set(0);
                            sVibrance.set(0);
                            sSharpness.set(0);
                            sNoiseReduce.set(0);
                            sVignette.set(0);
                            sGrain.set(0);
                            if (canvasPtr_)
                              canvasPtr_->redraw();
                          }),
                    SizedBox(8, 0),
                    GestureDetector(
                        Container(Text(beforeMode,
                                       [](const bool &b) -> std::string {
                                         return b ? "After ▶" : "Before ◀";
                                       })
                                      ->setFontSize(11)
                                      ->setTextColor(kText))
                            ->setBackgroundColor(Color::fromRGB(30, 30, 46))
                            ->setBorderRadius(5)
                            ->setPaddingAll(6, 3, 6, 3)
                            ->setBorderWidth(1)
                            ->setBorderColor(kBorder))
                        ->setOnTap([this]() {
                          bool b = !beforeMode.get();
                          beforeMode.set(b);
                          if (surface_) {
                            if (b) {
                              savedParams_ = surface_->params();
                              surface_->params().reset();
                            } else {
                              surface_->params() = savedParams_;
                            }
                            surface_->markDirty();
                            if (canvasPtr_)
                              canvasPtr_->redraw();
                          }
                        }),
                    SizedBox(16, 0),
                    Text("Zoom")->setFontSize(10)->setTextColor(kDim),
                    Slider(6.25, 200.0, 0.25)
                        ->setValue(zoomLevel)
                        ->setTrackFillColor(kAccent)
                        ->setWidth(110),
                    Text(zoomLevel,
                         [](double v) {
                           char b[16];
                           snprintf(b, sizeof(b), "%d%%", int(std::round(v)));
                           return std::string(b);
                         })
                        ->setFontSize(10)
                        ->setTextColor(Color::fromRGB(180, 180, 200))
                        ->setMinWidth(38),
                    Button("1:1",
                           [this]() {
                             if (canvasPtr_) {
                               canvasPtr_->viewport().resetZoom();
                               canvasPtr_->redraw();
                               zoomLevel.set(100.0);
                             }
                           })
                        ->setBackgroundColor(Color::fromRGB(24, 24, 37))
                        ->setTextColor(kAccent)
                        ->setBorderRadius(5)
                        ->setWidth(30)
                        ->setHeight(26)
                        ->setPadding(4),
                    Button("Fit",
                           [this]() {
                             if (canvasPtr_) {
                               canvasPtr_->viewport().fitToView();
                               canvasPtr_->redraw();
                               zoomLevel.set(
                                   double(canvasPtr_->viewport().zoom()) *
                                   100.0);
                             }
                           })
                        ->setBackgroundColor(Color::fromRGB(24, 24, 37))
                        ->setTextColor(kAccent)
                        ->setBorderRadius(5)
                        ->setWidth(30)
                        ->setHeight(26)
                        ->setPadding(4),
                    SizedBox(16, 0),
                    Text(statusMsg, [](const std::string &s) { return s; })
                        ->setFontSize(10)
                        ->setTextColor(kGreen),
                })
                ->setSpacing(4)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(kBg1)
            ->setPaddingAll(10, 7, 10, 7)
            ->setHeight(kToolbarH);

    // ──   HINTS
    // ─────────────────────────────────────────────────────────────
    auto hints =
        Container(Row({
                          Text("MMB/Space: Pan")
                              ->setFontSize(10)
                              ->setTextColor(Color::fromRGB(60, 60, 80)),
                          SizedBox(10, 0),
                          Text("Ctrl+Scroll: Zoom")
                              ->setFontSize(10)
                              ->setTextColor(Color::fromRGB(60, 60, 80)),
                          SizedBox(10, 0),
                          Text("Curve: click=add  dbl-click=delete  arrow "
                               "keys=nudge  |  "
                               "HSL: dbl-click label=reset band")
                              ->setFontSize(10)
                              ->setTextColor(Color::fromRGB(60, 60, 80)),
                      })
                      ->setSpacing(0))
            ->setBackgroundColor(kBg1)
            ->setPaddingAll(10, 3, 10, 3)
            ->setHeight(kHintsH);

    return Scaffold(nullptr,
                    Row({
                            Column({toolbar, hints, canvas})->setSpacing(0),
                            panel,
                        })
                        ->setSpacing(0));
  }

private:
  EditParams savedParams_;
  bool syncingZoom_ = false;
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("FluxUI - Image Editor", std::make_shared<ImageEditorApp>(),
                 AppTheme::dark(),
                 false, // debugShowWidgetBounds
                 900,   // width
                 700,   // height
                 false, // maximize
                 true   // fullscreen
  );
}