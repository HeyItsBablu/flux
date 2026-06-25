#include "flux/flux_overlay_manager.hpp"
#include "flux/flux_core.hpp"
#include <algorithm>

#ifdef __ANDROID__
#include "nanovg.h"
extern NVGcontext *FluxAndroid_getVG();
extern float FluxAndroid_getDpiScale();
#endif

struct OverlayManager::Entry
{
    OverlayContent *content = nullptr;
    int zIndex = 0;
    bool pendingRemoval = false;

    int clientX = 0, clientY = 0, w = 0, h = 0;

    bool visible_ = false;
    bool visible() const { return visible_; }
    void destroyPopup() { visible_ = false; }
    ~Entry() = default;
};

OverlayManager::OverlayManager() = default;
OverlayManager::~OverlayManager() = default;

OverlayManager::Entry *OverlayManager::find(OverlayContent *content)
{
    for (auto &e : entries_)
        if (e->content == content && !e->pendingRemoval)
            return e.get();
    return nullptr;
}
const OverlayManager::Entry *OverlayManager::find(OverlayContent *content) const
{
    for (auto &e : entries_)
        if (e->content == content && !e->pendingRemoval)
            return e.get();
    return nullptr;
}

void OverlayManager::sortByZ()
{
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const std::unique_ptr<Entry> &a, const std::unique_ptr<Entry> &b)
                     {
                         return a->zIndex < b->zIndex;
                     });
}

struct OverlayManager::DispatchScope
{
    OverlayManager *mgr;
    explicit DispatchScope(OverlayManager *m) : mgr(m) { ++mgr->dispatchDepth_; }
    ~DispatchScope()
    {
        if (--mgr->dispatchDepth_ == 0)
            mgr->pruneRemoved_();
    }
};

void OverlayManager::pruneRemoved_()
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [](const std::unique_ptr<Entry> &en)
                                  { return en->pendingRemoval; }),
                   entries_.end());
}

void OverlayManager::show(OverlayContent *content, int clientX, int clientY,
                          int w, int h, int zIndex, FontCache &fontCache)
{
    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
        return;

    Entry *e = find(content);
    if (!e)
    {
        entries_.push_back(std::make_unique<Entry>());
        e = entries_.back().get();
        e->content = content;
    }
    e->clientX = clientX;
    e->clientY = clientY;
    e->w = w;
    e->h = h;
    e->zIndex = zIndex;
    sortByZ();

    e->visible_ = true;
    ui->invalidateWidget(clientX, clientY, w, h);
}

void OverlayManager::hide(OverlayContent *content)
{
    Entry *e = find(content);
    if (!e)
        return;

    int x = e->clientX, y = e->clientY, w = e->w, h = e->h;
    e->destroyPopup();
    e->pendingRemoval = true;

    if (dispatchDepth_ == 0)
        pruneRemoved_();

    if (auto *ui = FluxUI::getCurrentInstance())
        ui->invalidateWidget(x, y, w, h);
}

void OverlayManager::refresh(OverlayContent *content, FontCache &fontCache)
{
    Entry *e = find(content);
    if (!e || !e->visible())
        return;
    if (auto *ui = FluxUI::getCurrentInstance())
        ui->invalidateWidget(e->clientX, e->clientY, e->w, e->h);
}

bool OverlayManager::isOpen(OverlayContent *content) const
{
    const Entry *e = find(content);
    return e && e->visible();
}

void OverlayManager::closeAll()
{
    for (auto &e : entries_)
    {
        e->destroyPopup();
        e->pendingRemoval = true;
    }
    if (dispatchDepth_ == 0)
        pruneRemoved_();
}

bool OverlayManager::hasBlockingOverlay() const
{
    for (auto &e : entries_)
        if (e->visible() && e->content->overlayPolicy().blocksHoverBelow)
            return true;
    return false;
}

bool OverlayManager::dispatchMouseDown(int clientX, int clientY)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        bool inside = clientX >= e->clientX && clientX < e->clientX + e->w &&
                      clientY >= e->clientY && clientY < e->clientY + e->h;
        if (inside)
        {
            if (e->content->onOverlayMouseDown(clientX - e->clientX, clientY - e->clientY))
                return true;
        }
        else
        {
            e->content->onOverlayOutsideClick();
        }
        if (e->content->overlayPolicy().modal)
            return true;
    }
    return false;
}

bool OverlayManager::dispatchMouseUp(int clientX, int clientY)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->onOverlayMouseUp(clientX - e->clientX, clientY - e->clientY))
            return true;
    }
    return false;
}

bool OverlayManager::dispatchMouseMove(int clientX, int clientY)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->onOverlayMouseMove(clientX - e->clientX, clientY - e->clientY))
            return true;
    }
    return false;
}

bool OverlayManager::dispatchMouseWheel(int delta)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->onOverlayMouseWheel(delta))
            return true;
    }
    return false;
}

bool OverlayManager::dispatchKeyDown(int keyCode)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->overlayPolicy().capturesKeyboard)
            return e->content->onOverlayKeyDown(keyCode);
    }
    return false;
}

bool OverlayManager::dispatchRightClick(int clientX, int clientY)
{
    DispatchScope scope(this);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    {
        Entry *e = it->get();
        if (e->pendingRemoval || !e->visible())
            continue;
        if (e->content->onOverlayRightClick(clientX - e->clientX, clientY - e->clientY))
            return true;
    }
    return false;
}

void OverlayManager::renderAll(GraphicsContext &ctx, FontCache &fc)
{
    for (auto &e : entries_)
    {
        if (!e->visible_ || e->w <= 0 || e->h <= 0)
            continue;

#ifdef _WIN32
        ctx.dc->PushAxisAlignedClip(
            D2D1::RectF((float)e->clientX, (float)e->clientY,
                        (float)(e->clientX + e->w),
                        (float)(e->clientY + e->h)),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        D2D1_MATRIX_3X2_F prev;
        ctx.dc->GetTransform(&prev);
        ctx.dc->SetTransform(
            D2D1::Matrix3x2F::Translation((float)e->clientX, (float)e->clientY) * prev);

        GraphicsContext localCtx = ctx;
        e->content->renderOverlay(localCtx, fc);

        ctx.dc->SetTransform(prev);
        ctx.dc->PopAxisAlignedClip();

#elif defined(__linux__) && !defined(__ANDROID__)
        cairo_t *cr = ctx.cr;
        cairo_save(cr);
        cairo_rectangle(cr, e->clientX, e->clientY, e->w, e->h);
        cairo_clip(cr);
        cairo_translate(cr, e->clientX, e->clientY);
        GraphicsContext localCtx(cr, e->w, e->h);
        e->content->renderOverlay(localCtx, fc);
        cairo_restore(cr);

#elif defined(__APPLE__)
        CGContextRef cg = ctx.cgContext;
        if (!cg)
            continue;
        CGContextSaveGState(cg);
        CGContextClipToRect(cg, CGRectMake(e->clientX, e->clientY, e->w, e->h));
        CGContextTranslateCTM(cg, e->clientX, e->clientY);
        GraphicsContext localCtx(cg, e->w, e->h);
        e->content->renderOverlay(localCtx, fc);
        CGContextRestoreGState(cg);

#elif defined(__EMSCRIPTEN__)
        EM_ASM({
            var c = Module._fluxCtx2D;
            if (!c) return;
            c.save();
            c.beginPath();
            c.rect($0, $1, $2, $3);
            c.clip();
            c.translate($0, $1);
        }, e->clientX, e->clientY, e->w, e->h);
        {
            GraphicsContext localCtx(e->w, e->h);
            e->content->renderOverlay(localCtx, fc);
        }
        EM_ASM({ var c = Module._fluxCtx2D; if (c) c.restore(); });

#else // Android / NanoVG
        NVGcontext *vg = FluxAndroid_getVG();
        if (!vg)
            continue;
        nvgSave(vg);
        float dpi = FluxAndroid_getDpiScale();
        nvgTranslate(vg, e->clientX * dpi, e->clientY * dpi);
        GraphicsContext localCtx(e->w, e->h);
        e->content->renderOverlay(localCtx, fc);
        nvgRestore(vg);
#endif
    }
}