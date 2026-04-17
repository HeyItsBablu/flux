// image_editor.hpp
#pragma once
#include "flux/flux.hpp"
#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <execution>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <glad/glad.h>
#include <nanovg.h>
#include <nanovg_gl.h>

struct EditParams {
  float exposure = 0.f;
  float contrast = 0.f;
  float highlights = 0.f;
  float shadows = 0.f;
  float whites = 0.f;
  float blacks = 0.f;
  float temperature = 0.f;
  float tint = 0.f;
  float saturation = 0.f;
  float vibrance = 0.f;
  float sharpness = 0.f;
  float noiseReduce = 0.f;
  float vignette = 0.f;
  float grain = 0.f;

  void reset() { *this = EditParams{}; }

  // Field-by-field — avoids memcmp on floats / padding bytes.
  bool operator==(const EditParams &o) const {
    return exposure == o.exposure && contrast == o.contrast &&
           highlights == o.highlights && shadows == o.shadows &&
           whites == o.whites && blacks == o.blacks &&
           temperature == o.temperature && tint == o.tint &&
           saturation == o.saturation && vibrance == o.vibrance &&
           sharpness == o.sharpness && noiseReduce == o.noiseReduce &&
           vignette == o.vignette && grain == o.grain;
  }
  bool operator!=(const EditParams &o) const { return !(*this == o); }
};

namespace ImageProcessor {

struct GammaLUT {
  float toLinear[256];
  GammaLUT() {
    for (int i = 0; i < 256; ++i) {
      float c = i / 255.f;
      toLinear[i] =
          c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
  }
};
static const GammaLUT kGamma;

static inline float toSRGB(float c) {
  c = std::clamp(c, 0.f, 1.f);
  return c <= 0.0031308f ? c * 12.92f
                         : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}
static inline float luma(float r, float g, float b) {
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

struct HSL {
  float h, s, l;
};

static HSL rgb2hsl(float r, float g, float b) {
  float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  float l = (mx + mn) * 0.5f;
  if (d < 0.0001f)
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
static float hue2rgb(float p, float q, float t) {
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
static void hsl2rgb(float h, float s, float l, float &r, float &g, float &b) {
  if (s < 0.0001f) {
    r = g = b = l;
    return;
  }
  float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
  float p = 2.f * l - q;
  r = hue2rgb(p, q, h + 1.f / 3.f);
  g = hue2rgb(p, q, h);
  b = hue2rgb(p, q, h - 1.f / 3.f);
}

// ── Separable 3-tap box blur
// ──────────────────────────────────────────────────

static void boxBlurH(const uint8_t *src, float *tmpR, float *tmpG, float *tmpB,
                     int w, int h) {
  for (int py = 0; py < h; py++) {
    const uint8_t *row = src + py * w * 4;
    float *dR = tmpR + py * w;
    float *dG = tmpG + py * w;
    float *dB = tmpB + py * w;

    float sr = row[0] * 2.f + row[4];
    float sg = row[1] * 2.f + row[5];
    float sb = row[2] * 2.f + row[6];
    dR[0] = sr / 3.f / 255.f;
    dG[0] = sg / 3.f / 255.f;
    dB[0] = sb / 3.f / 255.f;

    for (int px = 1; px < w; px++) {
      int lx = std::max(px - 2, 0) * 4;
      int rx = std::min(px + 1, w - 1) * 4;
      sr += row[rx] - row[lx];
      sg += row[rx + 1] - row[lx + 1];
      sb += row[rx + 2] - row[lx + 2];
      dR[px] = sr / 3.f / 255.f;
      dG[px] = sg / 3.f / 255.f;
      dB[px] = sb / 3.f / 255.f;
    }
  }
}

static void boxBlurV(const float *tmpR, const float *tmpG, const float *tmpB,
                     float *outR, float *outG, float *outB, int w, int h) {
  for (int px = 0; px < w; px++) {
    float sr = tmpR[px] * 2.f + tmpR[w + px];
    float sg = tmpG[px] * 2.f + tmpG[w + px];
    float sb = tmpB[px] * 2.f + tmpB[w + px];
    outR[px] = sr / 3.f;
    outG[px] = sg / 3.f;
    outB[px] = sb / 3.f;

    for (int py = 1; py < h; py++) {
      int ty = py * w + px;
      int ly = std::max(py - 2, 0) * w + px;
      int ry = std::min(py + 1, h - 1) * w + px;
      sr += tmpR[ry] - tmpR[ly];
      sg += tmpG[ry] - tmpG[ly];
      sb += tmpB[ry] - tmpB[ly];
      outR[ty] = sr / 3.f;
      outG[ty] = sg / 3.f;
      outB[ty] = sb / 3.f;
    }
  }
}

// ── Main process
// ──────────────────────────────────────────────────────────────

static void process(const std::vector<uint8_t> &src, uint8_t *dst, int w, int h,
                    const EditParams &p) {

  const bool needsBlur = p.sharpness > 0.001f || p.noiseReduce > 0.001f;
  std::vector<float> blurR, blurG, blurB;

  if (needsBlur) {
    const int N = w * h;
    std::vector<float> tmpR(N), tmpG(N), tmpB(N);
    blurR.resize(N);
    blurG.resize(N);
    blurB.resize(N);
    boxBlurH(src.data(), tmpR.data(), tmpG.data(), tmpB.data(), w, h);
    boxBlurV(tmpR.data(), tmpG.data(), tmpB.data(), blurR.data(), blurG.data(),
             blurB.data(), w, h);
  }

  const float ev = std::pow(2.f, p.exposure);
  const float pv = 0.18f;
  const float tmp = p.temperature * 0.15f;
  const float tnt = p.tint * 0.10f;
  const float noBlend = std::clamp(p.noiseReduce, 0.f, 1.f);
  const float sharp = p.sharpness * 0.6f;
  const float wM = 1.f + p.whites * 0.3f;
  const float bkOff = p.blacks * 0.15f;
  const bool doVig = std::abs(p.vignette) > 0.001f;
  const bool doGrain = p.grain > 0.001f;
  const float grainAmp = p.grain * 0.12f;

  std::vector<int> rows(h);
  std::iota(rows.begin(), rows.end(), 0);

  std::for_each(
      std::execution::par_unseq, rows.begin(), rows.end(), [&](int py) {
        for (int px = 0; px < w; px++) {
          const uint8_t *s = src.data() + (py * w + px) * 4;

          float r = kGamma.toLinear[s[0]];
          float g = kGamma.toLinear[s[1]];
          float b = kGamma.toLinear[s[2]];
          float alpha = s[3] / 255.f;

          if (needsBlur) {
            const int idx = py * w + px;
            float br = blurR[idx], bg = blurG[idx], bb = blurB[idx];
            r += noBlend * (br - r);
            g += noBlend * (bg - g);
            b += noBlend * (bb - b);
            r = std::clamp(r + sharp * (r - br), 0.f, 1.f);
            g = std::clamp(g + sharp * (g - bg), 0.f, 1.f);
            b = std::clamp(b + sharp * (b - bb), 0.f, 1.f);
          }

          r *= ev;
          g *= ev;
          b *= ev;

          r = std::clamp((r - pv) * (1.f + p.contrast) + pv, 0.f, 1.f);
          g = std::clamp((g - pv) * (1.f + p.contrast) + pv, 0.f, 1.f);
          b = std::clamp((b - pv) * (1.f + p.contrast) + pv, 0.f, 1.f);

          {
            float l = luma(r, g, b);
            float hM = l * l;
            float sM = (1.f - l) * (1.f - l);
            float hf = 1.f + p.highlights * hM * 0.7f;
            float sf = 1.f + p.shadows * sM * 0.7f;
            r = std::clamp(r * hf * sf, 0.f, 1.f);
            g = std::clamp(g * hf * sf, 0.f, 1.f);
            b = std::clamp(b * hf * sf, 0.f, 1.f);
          }

          r = std::clamp(r * wM - bkOff, 0.f, 1.f);
          g = std::clamp(g * wM - bkOff, 0.f, 1.f);
          b = std::clamp(b * wM - bkOff, 0.f, 1.f);

          r = std::clamp(r + tmp + tnt, 0.f, 1.f);
          g = std::clamp(g - tnt, 0.f, 1.f);
          b = std::clamp(b - tmp + tnt, 0.f, 1.f);

          r = toSRGB(r);
          g = toSRGB(g);
          b = toSRGB(b);

          if (std::abs(p.saturation) > 0.001f) {
            HSL hsl = rgb2hsl(r, g, b);
            hsl.s = std::clamp(hsl.s * (1.f + p.saturation), 0.f, 1.f);
            hsl2rgb(hsl.h, hsl.s, hsl.l, r, g, b);
          }

          if (std::abs(p.vibrance) > 0.001f) {
            HSL hsl = rgb2hsl(r, g, b);
            float mask = (1.f - hsl.s) * (1.f - hsl.s);
            hsl.s = std::clamp(hsl.s + p.vibrance * mask * 0.8f, 0.f, 1.f);
            hsl2rgb(hsl.h, hsl.s, hsl.l, r, g, b);
          }

          if (doVig) {
            float uvx = (float(px) / w) * 2.f - 1.f;
            float uvy = (float(py) / h) * 2.f - 1.f;
            float dist = uvx * uvx + uvy * uvy;
            float t =
                dist > 0.5f ? std::clamp((dist - 0.5f) / 1.1f, 0.f, 1.f) : 0.f;
            float factor = 1.f - t * (-p.vignette) * 0.8f;
            r = std::clamp(r * factor, 0.f, 1.f);
            g = std::clamp(g * factor, 0.f, 1.f);
            b = std::clamp(b * factor, 0.f, 1.f);
          }

          if (doGrain) {
            unsigned seed =
                (unsigned(px) * 1973u + unsigned(py) * 9277u + 13u) *
                2654435761u;
            float noise = (seed >> 16) / 65535.f;
            float offset = (noise - 0.5f) * grainAmp;
            r = std::clamp(r + offset, 0.f, 1.f);
            g = std::clamp(g + offset, 0.f, 1.f);
            b = std::clamp(b + offset, 0.f, 1.f);
          }

          uint8_t *d = dst + (py * w + px) * 4;
          d[0] = uint8_t(std::clamp(r * 255.f + 0.5f, 0.f, 255.f));
          d[1] = uint8_t(std::clamp(g * 255.f + 0.5f, 0.f, 255.f));
          d[2] = uint8_t(std::clamp(b * 255.f + 0.5f, 0.f, 255.f));
          d[3] = uint8_t(std::clamp(alpha * 255.f + 0.5f, 0.f, 255.f));
        }
      });
}

} // namespace ImageProcessor

class ImageEditorSurface : public RenderSurface {
public:
  std::function<void()> onImageLoaded;
  std::function<void(const char *)> onStatusMessage;

  // ── Public API ─────────────────────────────────────────────────────────────

  bool loadImage(const std::string &path) {
    pendingPath_ = path;
    return true;
  }

  bool exportImage(const std::string &path) {
    if (!originalPixels_)
      return false;

    // Wait for any running job so we export the very latest result.
    {
      std::unique_lock<std::mutex> lk(jobMutex_);
      jobDoneCV_.wait(lk, [this] { return !workerBusy_; });
    }

    std::vector<uint8_t> buf;
    {
      std::lock_guard<std::mutex> lk(resultMutex_);
      buf = resultPixels_.empty() ? *originalPixels_ : resultPixels_;
    }

    std::string ext = path.substr(path.find_last_of('.'));
    for (auto &c : ext)
      c = char(std::tolower(c));

    bool ok = false;
    if (ext == ".jpg" || ext == ".jpeg")
      ok = stbi_write_jpg(path.c_str(), imgW_, imgH_, 4, buf.data(), 95) != 0;
    else
      ok = stbi_write_png(path.c_str(), imgW_, imgH_, 4, buf.data(),
                          imgW_ * 4) != 0;

    if (onStatusMessage)
      onStatusMessage(ok ? "Export saved." : "Export failed.");
    return ok;
  }

  bool hasImage() const { return originalPixels_ != nullptr; }
  int imageWidth() const { return imgW_; }
  int imageHeight() const { return imgH_; }
  EditParams &params() { return params_; }
  const EditParams &params() const { return params_; }

  void markDirty() {
    if (!originalPixels_)
      return;
    if (params_ == lastRequested_)
      return;
    lastRequested_ = params_;
    debounceRemainMs_ = kDebounceMs;
  }

  // ── RenderSurface lifecycle ────────────────────────────────────────────────

  void initialize(int /*w*/, int /*h*/) override { startWorker(); }

  void resize(int /*w*/, int /*h*/) override {}

  void update(double dt) override {
    if (debounceRemainMs_ <= 0.0)
      return;
    debounceRemainMs_ -= dt * 1000.0;
    if (debounceRemainMs_ <= 0.0) {
      debounceRemainMs_ = 0.0;
      tryLaunchJob(lastRequested_);
    }
  }

  void destroy() override {
    stopWorker();
    destroyGLResources();
  }

  void render(Canvas2D &ctx) override {
    const float W = float(ctx.width());
    const float H = float(ctx.height());

    ctx.setFillColor({20, 20, 28, 255});
    ctx.fillRect(0.f, 0.f, W, H);

    // ── Lazy file load (render thread, GL context is current) ─────────────
    if (!pendingPath_.empty()) {
      doLoad(ctx, pendingPath_);
      pendingPath_.clear();
    }

    if (!originalPixels_) {
      ctx.setFont("16px sans");
      ctx.setTextAlign(TextAlign::Center);
      ctx.setTextBaseline(TextBaseline::Middle);
      ctx.setFillColor({70, 70, 95, 255});
      ctx.fillText("Open an image to begin", W * 0.5f, H * 0.5f);
      return;
    }

    // ── Commit PBO → texture when the worker signals new data ─────────────

    if (pboReady_.load(std::memory_order_acquire)) {
      commitPBO();
      pboReady_.store(false, std::memory_order_release);
      wakeupNeeded_.store(false, std::memory_order_release);

      if (jobPending_.load(std::memory_order_acquire)) {
        jobPending_.store(false, std::memory_order_release);
        tryLaunchJob(lastRequested_);
      }
    }

    // ── Draw via persistent NVG texture (no re-upload) ────────────────────
    if (nvgImageHandle_ >= 0) {
      NVGpaint paint = nvgImagePattern(ctx.nvg(), 0.f, 0.f, float(imgW_),
                                       float(imgH_), 0.f, nvgImageHandle_, 1.f);
      nvgBeginPath(ctx.nvg());
      nvgRect(ctx.nvg(), 0.f, 0.f, float(imgW_), float(imgH_));
      nvgFillPaint(ctx.nvg(), paint);
      nvgFill(ctx.nvg());
    }
  }

  bool needsContinuousRedraw() const override {
    return wakeupNeeded_.load(std::memory_order_acquire);
  }

private:
  static constexpr double kDebounceMs = 80.0;

  // ── Image state ───────────────────────────────────────────────────────────
  std::string pendingPath_;
  int imgW_ = 0, imgH_ = 0;
  EditParams params_;
  EditParams lastRequested_;
  EditParams lastLaunched_;
  double debounceRemainMs_ = 0.0;

  std::shared_ptr<const std::vector<uint8_t>> originalPixels_;

  // ── Worker thread ─────────────────────────────────────────────────────────

  struct Job {
    EditParams params;
    std::shared_ptr<const std::vector<uint8_t>> src;
    int w, h;
  };

  std::thread workerThread_;
  std::mutex jobMutex_;
  std::condition_variable jobCV_;
  std::condition_variable jobDoneCV_;
  std::optional<Job> pendingJob_;
  bool workerBusy_ = false;
  bool workerShouldExit_ = false;

  std::atomic<bool> jobPending_{false};

  std::mutex resultMutex_;
  std::vector<uint8_t> resultPixels_;

  // ── GL / NVG resources ────────────────────────────────────────────────────

  GLuint persistentTex_ = 0;
  GLuint pbo_ = 0;
  int nvgImageHandle_ = -1;

  std::atomic<bool> pboReady_{false};
  std::atomic<bool> wakeupNeeded_{false};

  // ── Worker ────────────────────────────────────────────────────────────────

  void startWorker() {
    workerShouldExit_ = false;
    workerThread_ = std::thread([this] { workerLoop(); });
  }

  void stopWorker() {
    {
      std::lock_guard<std::mutex> lk(jobMutex_);
      workerShouldExit_ = true;
      pendingJob_.reset();
    }
    jobCV_.notify_one();
    if (workerThread_.joinable())
      workerThread_.join();
  }

  void workerLoop() {
    while (true) {
      Job job;
      {
        std::unique_lock<std::mutex> lk(jobMutex_);
        jobCV_.wait(lk, [this] {
          return pendingJob_.has_value() || workerShouldExit_;
        });
        if (workerShouldExit_)
          break;
        job = std::move(*pendingJob_);
        pendingJob_.reset();
        workerBusy_ = true;
      }

      // Process into a plain heap buffer.
      const size_t N = size_t(job.w) * job.h * 4;
      std::vector<uint8_t> tmp(N);
      ImageProcessor::process(*job.src, tmp.data(), job.w, job.h, job.params);

      {
        std::lock_guard<std::mutex> lk(resultMutex_);
        resultPixels_ = std::move(tmp);
      }
      {
        std::lock_guard<std::mutex> lk(jobMutex_);
        workerBusy_ = false;
      }
      jobDoneCV_.notify_all();

      pboReady_.store(true, std::memory_order_release);
      wakeupNeeded_.store(true, std::memory_order_release);
    }
  }

  // ── Job dispatch ──────────────────────────────────────────────────────────

  void tryLaunchJob(const EditParams &p) {
    if (!originalPixels_)
      return;
    {
      std::lock_guard<std::mutex> lk(jobMutex_);
      if (workerBusy_) {

        jobPending_.store(true, std::memory_order_release);
        lastRequested_ = p;
        return;
      }
      lastLaunched_ = p;
      pendingJob_ = Job{p, originalPixels_, imgW_, imgH_};
    }
    jobCV_.notify_one();
  }

  // ── GL resource helpers ───────────────────────────────────────────────────

  void allocateGLResources(NVGcontext *nvg) {
    destroyGLResources();

    const GLsizeiptr sz = GLsizeiptr(imgW_) * imgH_ * 4;

    // ── Persistent texture ────────────────────────────────────────────────
    glGenTextures(1, &persistentTex_);
    glBindTexture(GL_TEXTURE_2D, persistentTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Initial content = original pixels (shown instantly, before first job).
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imgW_, imgH_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, originalPixels_->data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── PBO (write-combined staging buffer for CPU→GPU streaming) ─────────
    glGenBuffers(1, &pbo_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, sz, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    // ── NVG wrapper ───────────────────────────────────────────────────────

    nvgImageHandle_ =
        nvglCreateImageFromHandleGL3(nvg, persistentTex_, imgW_, imgH_, 0);
  }

  void destroyGLResources() {
    nvgImageHandle_ = -1;

    if (pbo_) {
      glDeleteBuffers(1, &pbo_);
      pbo_ = 0;
    }
    if (persistentTex_) {
      glDeleteTextures(1, &persistentTex_);
      persistentTex_ = 0;
    }
  }

  void commitPBO() {
    if (!persistentTex_ || !pbo_)
      return;

    std::vector<uint8_t> pixels;
    {
      std::lock_guard<std::mutex> lk(resultMutex_);
      pixels = resultPixels_;
    }
    if (pixels.empty())
      return;

    const GLsizeiptr sz = GLsizeiptr(imgW_) * imgH_ * 4;

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_);

    glBufferData(GL_PIXEL_UNPACK_BUFFER, sz, nullptr, GL_STREAM_DRAW);

    void *mapped =
        glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, sz,
                         GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                             GL_MAP_UNSYNCHRONIZED_BIT);
    if (mapped) {
      std::memcpy(mapped, pixels.data(), size_t(sz));
      glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    } else {
      // Rare fallback if mapping fails.
      glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, sz, pixels.data());
    }

    glBindTexture(GL_TEXTURE_2D, persistentTex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, imgW_, imgH_, GL_RGBA,
                    GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  }

  // ── Load / export ─────────────────────────────────────────────────────────

  void doLoad(Canvas2D &ctx, const std::string &path) {

    stopWorker();

    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) {
      if (onStatusMessage)
        onStatusMessage("Failed to load image.");
      startWorker();
      return;
    }

    imgW_ = w;
    imgH_ = h;

    originalPixels_ =
        std::make_shared<std::vector<uint8_t>>(data, data + size_t(w) * h * 4);
    stbi_image_free(data);

    {
      std::lock_guard<std::mutex> lk(resultMutex_);
      resultPixels_.clear();
    }

    params_.reset();
    lastRequested_ = params_;
    lastLaunched_ = params_;
    debounceRemainMs_ = 0.0;

    pboReady_.store(false, std::memory_order_relaxed);
    wakeupNeeded_.store(false, std::memory_order_relaxed);
    jobPending_.store(false, std::memory_order_relaxed);

    allocateGLResources(ctx.nvg());

    startWorker();

    if (onImageLoaded)
      onImageLoaded();
    if (onStatusMessage) {
      char msg[128];
      snprintf(msg, sizeof(msg), "Loaded: %dx%d", w, h);
      onStatusMessage(msg);
    }
  }
};

class ImageEditorApp : public Widget {

  // ── Slider states ──────────────────────────────────────────────────────────
  State<double> sExposure{0.0}, sContrast{0.0}, sHighlights{0.0}, sShadows{0.0},
      sWhites{0.0}, sBlacks{0.0}, sTemperature{0.0}, sTint{0.0},
      sSaturation{0.0}, sVibrance{0.0}, sSharpness{0.0}, sNoiseReduce{0.0},
      sVignette{0.0}, sGrain{0.0};

  State<std::string> statusMsg{"Open an image to begin."};
  State<bool> hasImage{false};
  State<double> zoomLevel{100.0};

  std::shared_ptr<ImageEditorSurface> surface_;
  CanvasWidget *canvasPtr_ = nullptr;
  bool syncingZoom_ = false;

  static constexpr int kPanelW = 270;
  static constexpr int kToolbarH = 46;

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

  void resetAll() {
    if (surface_) {
      surface_->params().reset();
      surface_->markDirty();
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
  }

public:
  WidgetPtr build() override {
    int viewW = 960 - kPanelW;
    int viewH = 700 - kToolbarH;

    auto canvas = std::make_shared<CanvasWidget>();
    canvas->setSize(viewW, viewH);
    canvas->setCanvasSize(1, 1);
    canvas->setScrollbarsEnabled(true);
    surface_ = canvas->setSurface<ImageEditorSurface>();
    canvasPtr_ = canvas.get();

    surface_->onImageLoaded = [this]() {
      hasImage.set(true);
      canvasPtr_->setCanvasSize(surface_->imageWidth(),
                                surface_->imageHeight());
      canvasPtr_->viewport().fitToView();
      zoomLevel.set(double(canvasPtr_->viewport().zoom()) * 100.0);
      canvasPtr_->redraw();
    };

    surface_->onStatusMessage = [this](const char *m) { statusMsg.set(m); };

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

    // ── Colors ────────────────────────────────────────────────────────────
    Color kBg1 = Color::fromRGB(17, 17, 27);
    Color kBg2 = Color::fromRGB(24, 24, 37);
    Color kBorder = Color::fromRGB(42, 44, 60);
    Color kText = Color::fromRGB(200, 210, 230);
    Color kDim = Color::fromRGB(90, 95, 120);
    Color kAccent = Color::fromRGB(140, 100, 240);
    Color kGreen = Color::fromRGB(100, 210, 180);
    Color kOrange = Color::fromRGB(240, 160, 80);
    Color kWarm = Color::fromRGB(240, 180, 80);
    Color kCool = Color::fromRGB(80, 160, 240);

    // ── Helpers ───────────────────────────────────────────────────────────

    auto sectionLabel = [&](const std::string &t) -> WidgetPtr {
      return Text(t)
          ->setFontSize(9)
          ->setTextColor(Color::fromRGB(80, 85, 115))
          ->setFontWeight(FontWeight::Bold);
    };

    auto makeSlider = [&](const std::string &label, double lo, double hi,
                          double step, State<double> &st,
                          Color col) -> WidgetPtr {
      return Column({
                        Row({
                                Text(label)
                                    ->setFontSize(9)
                                    ->setTextColor(kDim)
                                    ->setMinWidth(90),
                                Text(st,
                                     [](double v) {
                                       char b[12];
                                       snprintf(b, sizeof(b), "%+.2f", v);
                                       return std::string(b);
                                     })
                                    ->setFontSize(9)
                                    ->setTextColor(kText)
                                    ->setMinWidth(40),
                            })
                            ->setSpacing(0)
                            ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                        SizedBox(0, 3),
                        Slider(lo, hi, step)
                            ->setValue(st)
                            ->setTrackFillColor(col)
                            ->setWidth(kPanelW - 24),
                    })
          ->setSpacing(0);
    };

    auto card = [&](WidgetPtr child) -> WidgetPtr {
      return Container(child)
          ->setBackgroundColor(kBg2)
          ->setBorderRadius(7)
          ->setBorderWidth(1)
          ->setBorderColor(kBorder)
          ->setPaddingAll(10, 8, 10, 8);
    };

    // ── Toolbar ───────────────────────────────────────────────────────────

    auto toolbarBtn = [&](const std::string &lbl, Color c,
                          std::function<void()> fn) -> WidgetPtr {
      return Button(lbl, fn)
          ->setBackgroundColor(Color::fromRGB(28, 28, 42))
          ->setTextColor(c)
          ->setBorderRadius(5)
          ->setHeight(28)
          ->setPadding(4);
    };

    auto toolbar =
        Container(
            Row({
                    FilePicker("📂  Open")
                        ->setMode(FilePickerMode::Open)
                        ->addFilter("Images", {"*.jpg", "*.jpeg", "*.png",
                                               "*.bmp", "*.tga"})
                        ->addFilter("All Files", {"*.*"})
                        ->setShowPath(false)
                        ->setHeight(28)
                        ->setWidth(110)
                        ->setOnChanged([this](const std::string &path) {
                          if (!path.empty())
                            surface_->loadImage(path);
                        }),

                    SizedBox(6, 0),

                    FilePicker("💾  Export")
                        ->setMode(FilePickerMode::Save)
                        ->setTitle("Export Image")
                        ->setDefaultFilename("edited.png")
                        ->addFilter("PNG", {"*.png"})
                        ->addFilter("JPEG", {"*.jpg", "*.jpeg"})
                        ->setDefaultExtension("png")
                        ->setShowPath(false)
                        ->setHeight(28)
                        ->setWidth(110)
                        ->setOnChanged([this](const std::string &path) {
                          if (!path.empty())
                            surface_->exportImage(path);
                        }),

                    SizedBox(6, 0),
                    toolbarBtn("↺ Reset", kOrange, [this]() { resetAll(); }),
                    SizedBox(16, 0),

                    Text("Zoom")->setFontSize(10)->setTextColor(kDim),
                    SizedBox(4, 0),
                    Slider(6.25, 400.0, 0.25)
                        ->setValue(zoomLevel)
                        ->setTrackFillColor(kAccent)
                        ->setWidth(100),
                    SizedBox(4, 0),
                    Text(zoomLevel,
                         [](double v) {
                           char b[12];
                           snprintf(b, sizeof(b), "%d%%", int(std::round(v)));
                           return std::string(b);
                         })
                        ->setFontSize(10)
                        ->setTextColor(kText)
                        ->setMinWidth(36),

                    toolbarBtn("Fit", kAccent,
                               [this]() {
                                 if (!canvasPtr_)
                                   return;
                                 canvasPtr_->viewport().fitToView();
                                 canvasPtr_->redraw();
                                 zoomLevel.set(
                                     double(canvasPtr_->viewport().zoom()) *
                                     100.0);
                               }),
                    toolbarBtn("1:1", kAccent,
                               [this]() {
                                 if (!canvasPtr_)
                                   return;
                                 canvasPtr_->viewport().resetZoom();
                                 canvasPtr_->redraw();
                                 zoomLevel.set(100.0);
                               }),

                    SizedBox(16, 0),
                    Text(statusMsg, [](const std::string &s) { return s; })
                        ->setFontSize(10)
                        ->setTextColor(kGreen),
                })
                ->setSpacing(4)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setBackgroundColor(kBg1)
            ->setPaddingAll(10, 8, 10, 8)
            ->setHeight(kToolbarH);

    // ── Right panel ───────────────────────────────────────────────────────

    auto panel =
        Container(
            ListView(
                {
                    card(
                        Column(
                            {
                                sectionLabel("TONE"),
                                SizedBox(0, 7),
                                makeSlider("Exposure", -5.0, 5.0, 0.05,
                                           sExposure, kWarm),
                                SizedBox(0, 5),
                                makeSlider("Contrast", -1.0, 1.0, 0.01,
                                           sContrast, kAccent),
                                SizedBox(0, 5),
                                makeSlider("Highlights", -1.0, 1.0, 0.01,
                                           sHighlights,
                                           Color::fromRGB(210, 210, 230)),
                                SizedBox(0, 5),
                                makeSlider("Shadows", -1.0, 1.0, 0.01, sShadows,
                                           Color::fromRGB(70, 90, 160)),
                                SizedBox(0, 5),
                                makeSlider("Whites", -1.0, 1.0, 0.01, sWhites,
                                           Color::fromRGB(220, 220, 220)),
                                SizedBox(0, 5),
                                makeSlider("Blacks", -1.0, 1.0, 0.01, sBlacks,
                                           Color::fromRGB(55, 55, 80)),
                            })
                            ->setSpacing(0)),

                    SizedBox(0, 6),

                    card(Column({
                                    sectionLabel("COLOR"),
                                    SizedBox(0, 7),
                                    makeSlider("Temperature", -1.0, 1.0, 0.01,
                                               sTemperature, kCool),
                                    SizedBox(0, 5),
                                    makeSlider("Tint", -1.0, 1.0, 0.01, sTint,
                                               Color::fromRGB(200, 110, 200)),
                                    SizedBox(0, 5),
                                    makeSlider("Saturation", -1.0, 1.0, 0.01,
                                               sSaturation,
                                               Color::fromRGB(220, 100, 100)),
                                    SizedBox(0, 5),
                                    makeSlider("Vibrance", -1.0, 1.0, 0.01,
                                               sVibrance,
                                               Color::fromRGB(160, 220, 100)),
                                })
                             ->setSpacing(0)),

                    SizedBox(0, 6),

                    card(Column({
                                    sectionLabel("DETAIL"),
                                    SizedBox(0, 7),
                                    makeSlider("Sharpness", 0.0, 1.0, 0.01,
                                               sSharpness, kAccent),
                                    SizedBox(0, 5),
                                    makeSlider("Noise Reduce", 0.0, 1.0, 0.01,
                                               sNoiseReduce, kGreen),
                                })
                             ->setSpacing(0)),

                    SizedBox(0, 6),

                    card(Column({
                                    sectionLabel("EFFECTS"),
                                    SizedBox(0, 7),
                                    makeSlider("Vignette", -1.0, 1.0, 0.01,
                                               sVignette,
                                               Color::fromRGB(75, 75, 110)),
                                    SizedBox(0, 5),
                                    makeSlider("Grain", 0.0, 1.0, 0.01, sGrain,
                                               Color::fromRGB(170, 150, 120)),
                                })
                             ->setSpacing(0)),

                    SizedBox(0, 6),

                    Container(Column({
                                         Text("MMB / Space+drag — Pan")
                                             ->setFontSize(9)
                                             ->setTextColor(kDim),
                                         Text("Ctrl+Scroll — Zoom")
                                             ->setFontSize(9)
                                             ->setTextColor(kDim),
                                         Text("Shift+Scroll — Pan horizontal")
                                             ->setFontSize(9)
                                             ->setTextColor(kDim),
                                     })
                                  ->setSpacing(3))
                        ->setBackgroundColor(Color::fromRGB(14, 14, 22))
                        ->setBorderRadius(6)
                        ->setBorderWidth(1)
                        ->setBorderColor(kBorder)
                        ->setPaddingAll(8, 6, 8, 6),
                })
                ->setSpacing(0))
            ->setWidth(kPanelW)
            ->setBackgroundColor(kBg1)
            ->setPaddingAll(8, 8, 8, 8);

    return Scaffold(nullptr, Column({
                                        toolbar,
                                        Row({canvas, panel})->setSpacing(0),
                                    })
                                 ->setSpacing(0));
  }
};


WidgetPtr createApp(FluxUI * /*app*/) {
  return FluxApp("Image Editor", std::make_shared<ImageEditorApp>(),
                 AppTheme::dark(), false, 960, 700, false, true);
}