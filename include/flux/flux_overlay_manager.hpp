#ifndef FLUX_OVERLAY_MANAGER_HPP
#define FLUX_OVERLAY_MANAGER_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"
#include <memory>
#include <vector>

// ============================================================================
// OverlayContent — implemented by any widget that wants a floating overlay
// (dropdown list, context menu, tooltip, dialog, ...).
//
// Pure content interface. Knows nothing about popups, HWNDs, screen
// coordinates, or other overlays — all mechanics live in OverlayManager.
// Every coordinate passed into these methods is LOCAL to the overlay's own
// rect: (0,0) is this overlay's top-left corner.
// ============================================================================

struct OverlayPolicy
{
    bool modal = false;            // swallows clicks outside it; blocks tree below
    bool blocksHoverBelow = false; // stops hover/move reaching widgets underneath
    bool capturesKeyboard = true;  // gets first shot at key events while open
};

class OverlayContent
{
public:
    virtual ~OverlayContent() = default;

    virtual void renderOverlay(GraphicsContext &ctx, FontCache &fc) = 0;

    virtual bool onOverlayMouseDown(int /*x*/, int /*y*/) { return false; }
    virtual bool onOverlayMouseUp(int /*x*/, int /*y*/) { return false; }
    virtual bool onOverlayMouseMove(int /*x*/, int /*y*/) { return false; }
    virtual bool onOverlayMouseWheel(int /*delta*/) { return false; }
    virtual bool onOverlayKeyDown(int /*keyCode*/) { return false; }
    virtual bool onOverlayRightClick(int /*x*/, int /*y*/) { return false; }

    // Called when a click lands outside this overlay while it's open.
    // Implementations typically hide themselves here.
    virtual void onOverlayOutsideClick() {}

    virtual OverlayPolicy overlayPolicy() const { return {}; }
};

// ============================================================================
// OverlayManager — owned by FluxUI. The single place that knows about
// native popups, monitor clamping, z-order, and input routing. Widgets
// never see any of that; they only call show()/hide()/refresh().
// ============================================================================

class OverlayManager
{
public:
    OverlayManager();
    ~OverlayManager();

    OverlayManager(const OverlayManager &) = delete;
    OverlayManager &operator=(const OverlayManager &) = delete;

    // clientX/clientY/w/h are in MAIN-WINDOW CLIENT coordinates — the same
    // space every widget already lays itself out in. Screen-space
    // conversion, monitor-edge clamping, and native popup creation are
    // handled internally; callers never touch any of that.
    void show(OverlayContent *content, int clientX, int clientY,
              int w, int h, int zIndex, FontCache &fontCache);
    void hide(OverlayContent *content);
    void refresh(OverlayContent *content, FontCache &fontCache);
    bool isOpen(OverlayContent *content) const;
    void closeAll();

    bool dispatchMouseDown(int clientX, int clientY);
    bool dispatchMouseUp(int clientX, int clientY);
    bool dispatchMouseMove(int clientX, int clientY);
    bool dispatchMouseWheel(int delta);
    bool dispatchKeyDown(int keyCode);
    bool dispatchRightClick(int clientX, int clientY);
    bool hasBlockingOverlay() const;

    // Non-Win32 only: paints every open overlay inline, on top of the
    // normal widget tree. Call once per frame, after the tree renders.
    // Win32: no-op — popups paint themselves via their own layered windows.
    void renderAll(GraphicsContext &ctx, FontCache &fc);

private:
    struct Entry;
    struct DispatchScope;
    std::vector<std::unique_ptr<Entry>> entries_;
    int dispatchDepth_ = 0;

    Entry *find(OverlayContent *content);
    const Entry *find(OverlayContent *content) const;
    void sortByZ();
    void pruneRemoved_();
};

#endif // FLUX_OVERLAY_MANAGER_HPP