#pragma once
#ifdef _WIN32

// ============================================================================
// flux_render_loop.hpp
//
// Runs a dedicated render thread that:
//   1. Waits for a dirty signal (or a fixed interval when animating)
//   2. Calls the frame callback (layout if needed, then paint)
//   3. Drives timer callbacks at sub-frame accuracy
//
// Message thread → signals dirty via markDirty() / markLayoutDirty()
// Render thread  → calls onFrame / onLayout / timer callbacks
//
// All D2D calls happen on the render thread. The message thread only
// signals and reads atomic flags.
// ============================================================================

#include <windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
#include <chrono>
#include <cstdint>

// ============================================================================
// TimerEntry — a repeating callback scheduled by setInterval()
// ============================================================================

struct RenderTimer
{
    uint32_t id = 0;
    int intervalMs = 0;
    std::function<void()> callback;

    using Clock = std::chrono::steady_clock;
    Clock::time_point nextFire;
};

// ============================================================================
// RenderLoop
// ============================================================================

class RenderLoop
{
public:
    // ── Callbacks set by FluxUI::wireCallbacks ────────────────────────────────

    // Called on the render thread every frame when dirty.
    // Should call D3DDevice::beginDraw, render all widgets, endDrawAndPresent.
    std::function<void(float dt)> onFrame;

    // Called on the render thread when layout is dirty before onFrame.
    // Should call LayoutEngine::computeLayout + positionWidget.
    std::function<void()> onLayout;

    // Called on the render thread when the window is resized.
    // newW, newH are in physical pixels.
    std::function<void(int newW, int newH)> onResize;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    RenderLoop();
    ~RenderLoop();

    // Start the render thread. Must be called after D3DDevice::create().
    void start();

    // Signal the render thread to stop and join. Blocks until thread exits.
    void stop();

    // ── Dirty signaling (safe to call from any thread) ────────────────────────

    // Mark that the visual output needs redrawing (paint only).
    void markDirty();

    // Mark that layout must be recomputed before the next paint.
    // Implies markDirty().
    void markLayoutDirty();

    // Signal a resize. Thread-safe. Layout dirty is implied.
    void markResize(int w, int h);

    // ── Frame mode ────────────────────────────────────────────────────────────

    // When true, the render thread renders continuously (~60fps target)
    // without waiting for a dirty signal. Use for animations.
    void setContinuous(bool continuous);
    bool isContinuous() const { return continuous_.load(); }

    // ── Timer management (replace Win32 SetTimer) ─────────────────────────────
    // These are called from the message thread; execution is on render thread.

    uint32_t addTimer(int intervalMs, std::function<void()> callback);
    void removeTimer(uint32_t id);

    void post(std::function<void()> fn);
    void drainPending();

private:
    // ── Thread ────────────────────────────────────────────────────────────────
    HANDLE thread_ = nullptr;
    HANDLE wakeEvent_ = nullptr; // auto-reset event
    std::atomic<bool> running_ = false;

    // ── Dirty flags ───────────────────────────────────────────────────────────
    std::atomic<bool> paintDirty_ = false;
    std::atomic<bool> layoutDirty_ = false;
    std::atomic<bool> continuous_ = false;

    // ── Resize pending ────────────────────────────────────────────────────────
    std::atomic<bool> resizePending_ = false;
    std::atomic<int> pendingW_ = 0;
    std::atomic<int> pendingH_ = 0;

    // ── Timers ────────────────────────────────────────────────────────────────
    std::mutex timersMutex_;
    std::vector<RenderTimer> timers_;
    uint32_t nextTimerId_ = 1;

    // ── Frame timing ──────────────────────────────────────────────────────────
    using Clock = std::chrono::steady_clock;
    Clock::time_point lastFrame_;

    // ── Cross-thread post queue ───────────────────────────────────────────────
    std::mutex pendingMutex_;
    std::vector<std::function<void()>> pending_;

    // ── Internal ──────────────────────────────────────────────────────────────
    static DWORD WINAPI threadProc(LPVOID param);
    void runLoop();
    void tickTimers();
    int msUntilNextTimer(); // returns INT_MAX if no timers
};

#endif // _WIN32
