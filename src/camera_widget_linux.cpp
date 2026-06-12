// camera_widget_linux.cpp
// Linux platform implementation for CameraWidget.
//
// Preview: V4L2 RGB24 → BGRX → cairo_image_surface → cairo blit.
// Thumbnail: libjpeg → BGRX → cairo_image_surface → cairo blit.
// Flash: Painter::fillRectAlpha (semi-transparent white rect).
//
// Link: libjpeg  libv4l2  SDL2  cairo  pangocairo

#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/widgets/camera_widget.hpp"

// ── LinuxState deleter ────────────────────────────────────────────────────────

void CameraWidget::LinuxStateDeleter::operator()(LinuxState *p) const
{
    if (p)
    {
        if (p->previewSurf)
            cairo_surface_destroy(p->previewSurf);
        if (p->thumbSurf)
            cairo_surface_destroy(p->thumbSurf);
    }
    delete p;
}

// ── Lazy initialiser ──────────────────────────────────────────────────────────

static CameraWidget::LinuxState &getLinux(
    std::unique_ptr<CameraWidget::LinuxState,
                    CameraWidget::LinuxStateDeleter> &ptr)
{
    if (!ptr)
        ptr.reset(new CameraWidget::LinuxState());
    return *ptr;
}

// ── _platformScheduleOpen ─────────────────────────────────────────────────────

void CameraWidget::_platformScheduleOpen()
{
    _shouldOpen = true;
}

// ── _platformOnFlip ───────────────────────────────────────────────────────────

void CameraWidget::_platformOnFlip()
{
    if (_linux)
        _linux->bgrxCache.clear();
}

// ── _platformRenderPreview ────────────────────────────────────────────────────

bool CameraWidget::_platformRenderPreview(GraphicsContext &ctx, Painter &p,
                                          FontCache & /*fontCache*/, int viewH)
{
    auto &cam = FluxCamera::get();
    auto &s = getLinux(_linux);

    // Pull latest frame — convert RGB24 → BGRX for Cairo
    if (cam.updateFrame() && cam.getPreviewWidth() > 0)
    {
        auto frame = cam.lockFrame();
        if (frame.data && frame.width > 0 && frame.height > 0)
        {
            int n = frame.width * frame.height;
            s.bgrxCache.resize((size_t)(n * 4));
            const uint8_t *src = frame.data;
            uint8_t *dst = s.bgrxCache.data();
            for (int i = 0; i < n; i++)
            {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = 0xFF;
                src += 3;
                dst += 4;
            }
            s.cachedSrcW = frame.width;
            s.cachedSrcH = frame.height;

            // Rebuild Cairo surface when dimensions change
            if (s.cachedSrcW != s.cairoSurfW || s.cachedSrcH != s.cairoSurfH)
            {
                if (s.previewSurf)
                {
                    cairo_surface_destroy(s.previewSurf);
                    s.previewSurf = nullptr;
                }
                s.previewSurf = cairo_image_surface_create_for_data(
                    s.bgrxCache.data(), CAIRO_FORMAT_RGB24,
                    s.cachedSrcW, s.cachedSrcH, s.cachedSrcW * 4);
                s.cairoSurfW = s.cachedSrcW;
                s.cairoSurfH = s.cachedSrcH;
            }
        }
    }

    if (!s.previewSurf || s.bgrxCache.empty() || s.cachedSrcW <= 0 || !ctx.cr)
        return false;

    cairo_surface_mark_dirty(s.previewSurf);

    // Letterbox / pillarbox
    float camAR = (float)s.cachedSrcW / (float)s.cachedSrcH;
    float widAR = (float)width / (float)viewH;
    int dstX, dstY, dstW, dstH;
    if (camAR > widAR)
    {
        dstW = width;
        dstH = (int)((float)dstW / camAR);
        dstX = 0;
        dstY = (viewH - dstH) / 2;
    }
    else
    {
        dstH = viewH;
        dstW = (int)((float)dstH * camAR);
        dstX = (width - dstW) / 2;
        dstY = 0;
    }

    // Fill letterbox bars
    if (dstX > 0)
        p.fillRect(x, y, dstX, viewH, colPlaceholder);
    if (dstX + dstW < width)
        p.fillRect(x + dstX + dstW, y, width - dstX - dstW, viewH, colPlaceholder);
    if (dstY > 0)
        p.fillRect(x, y, width, dstY, colPlaceholder);
    if (dstY + dstH < viewH)
        p.fillRect(x, y + dstY + dstH, width, viewH - dstY - dstH, colPlaceholder);

    // Scale and blit via Cairo
    cairo_save(ctx.cr);
    cairo_translate(ctx.cr, x + dstX, y + dstY);
    cairo_scale(ctx.cr,
                (double)dstW / (double)s.cachedSrcW,
                (double)dstH / (double)s.cachedSrcH);
    cairo_set_source_surface(ctx.cr, s.previewSurf, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(ctx.cr), CAIRO_FILTER_BILINEAR);
    cairo_rectangle(ctx.cr, 0, 0, s.cachedSrcW, s.cachedSrcH);
    cairo_fill(ctx.cr);
    cairo_restore(ctx.cr);
    return true;
}

// ── _platformRenderFlash ──────────────────────────────────────────────────────

void CameraWidget::_platformRenderFlash(GraphicsContext & /*ctx*/, Painter &p,
                                        int viewH)
{
    Color fc = Color::fromRGBA(255, 255, 255, (uint8_t)(_flashAlpha * 255));
    p.fillRectAlpha(x, y, width, viewH, fc);
}

// ── _platformRenderThumb ──────────────────────────────────────────────────────

bool CameraWidget::_platformRenderThumb(GraphicsContext &ctx,
                                        int thumbX, int thumbY,
                                        int thumbW, int thumbH)
{
    if (!_linux || !_linux->thumbSurf || _linux->thumbSrcW <= 0 || !ctx.cr)
        return false;
    auto &s = *_linux;
    cairo_save(ctx.cr);
    cairo_translate(ctx.cr, thumbX, thumbY);
    cairo_scale(ctx.cr,
                (double)thumbW / (double)s.thumbSrcW,
                (double)thumbH / (double)s.thumbSrcH);
    cairo_set_source_surface(ctx.cr, s.thumbSurf, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(ctx.cr), CAIRO_FILTER_BILINEAR);
    cairo_rectangle(ctx.cr, 0, 0, s.thumbSrcW, s.thumbSrcH);
    cairo_fill(ctx.cr);
    cairo_restore(ctx.cr);
    return true;
}

// ── _platformLoadThumb ────────────────────────────────────────────────────────
// Decode JPEG → RGB24 → BGRX → Cairo surface.

void CameraWidget::_platformLoadThumb(const std::string &path)
{
    auto &s = getLinux(_linux);

    if (s.thumbSurf)
    {
        cairo_surface_destroy(s.thumbSurf);
        s.thumbSurf = nullptr;
    }
    s.thumbBgrx.clear();
    s.thumbSrcW = s.thumbSrcH = 0;

    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp)
        return;

    // libjpeg error handler that longjmps instead of calling exit()
    struct JpegErr
    {
        jpeg_error_mgr pub;
        jmp_buf jmpBuf;
    } jerr{};

    jpeg_decompress_struct cinfo{};
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = [](j_common_ptr c)
    {
        longjmp(reinterpret_cast<JpegErr *>(c->err)->jmpBuf, 1);
    };

    if (setjmp(jerr.jmpBuf))
    {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int w = (int)cinfo.output_width;
    int h = (int)cinfo.output_height;

    // Decode to RGB24
    std::vector<uint8_t> rgb((size_t)(w * h * 3));
    JSAMPROW rowPtr[1];
    while ((int)cinfo.output_scanline < h)
    {
        rowPtr[0] = rgb.data() + cinfo.output_scanline * w * 3;
        jpeg_read_scanlines(&cinfo, rowPtr, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);

    // RGB24 → BGRX
    s.thumbBgrx.resize((size_t)(w * h * 4));
    const uint8_t *src = rgb.data();
    uint8_t *dst = s.thumbBgrx.data();
    for (int i = 0; i < w * h; i++)
    {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = 0xFF;
        src += 3;
        dst += 4;
    }

    s.thumbSurf = cairo_image_surface_create_for_data(
        s.thumbBgrx.data(), CAIRO_FORMAT_RGB24, w, h, w * 4);
    s.thumbSrcW = w;
    s.thumbSrcH = h;
}

// ── _platformDestroy ──────────────────────────────────────────────────────────

void CameraWidget::_platformDestroy()
{
    _linux.reset(); // LinuxStateDeleter destroys both Cairo surfaces
}

#endif // __linux__ && !__ANDROID__