// flux_render_loop.cpp
#ifdef _WIN32

#include "flux/flux_render_loop.hpp"
#include "flux/flux_debug_log.hpp"
#include <algorithm>
#include <climits>

// ============================================================================
// Constructor / Destructor
// ============================================================================

RenderLoop::RenderLoop()
{
    // Auto-reset event: WaitForSingleObject wakes on Set(), then resets.
    wakeEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    lastFrame_  = Clock::now();
}

RenderLoop::~RenderLoop()
{
    stop();
    if (wakeEvent_)
    {
        CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
    }
}

// ============================================================================
// start / stop
// ============================================================================

void RenderLoop::start()
{
    if (thread_)
        return;

    running_.store(true);
    thread_ = CreateThread(nullptr, 0, threadProc, this, 0, nullptr);

    // Slightly above normal priority so the render thread isn't starved
    // by the message pump when the CPU is busy.
    if (thread_)
        SetThreadPriority(thread_, THREAD_PRIORITY_ABOVE_NORMAL);
}

void RenderLoop::stop()
{
    if (!thread_)
        return;

    running_.store(false);
    SetEvent(wakeEvent_); // wake the thread so it can exit

    WaitForSingleObject(thread_, 5000); // wait up to 5 s
    CloseHandle(thread_);
    thread_ = nullptr;
}

// ============================================================================
// Dirty signaling
// ============================================================================

void RenderLoop::markDirty()
{
    // Print call stack to find who is calling markDirty
    void* stack[10];
    USHORT frames = CaptureStackBackTrace(0, 10, stack, nullptr);
    
    std::string trace = "[RenderLoop::markDirty] called from:\n";
    for (USHORT i = 0; i < frames; i++) {
        char buf[32];
        sprintf_s(buf, "  [%d] 0x%p\n", i, stack[i]);
        trace += buf;
    }
    fluxLog(trace);

    paintDirty_.store(true);
    SetEvent(wakeEvent_);
}

void RenderLoop::markLayoutDirty()
{
    fluxLog("[RenderLoop::markLayoutDirty] called");
    layoutDirty_.store(true);
    paintDirty_.store(true);
    SetEvent(wakeEvent_);
}

void RenderLoop::markResize(int w, int h)
{
    pendingW_.store(w);
    pendingH_.store(h);
    resizePending_.store(true);
    layoutDirty_.store(true);
    paintDirty_.store(true);
    SetEvent(wakeEvent_);
}

void RenderLoop::setContinuous(bool continuous)
{
    continuous_.store(continuous);
    if (continuous)
        SetEvent(wakeEvent_);
}

// ============================================================================
// Timer management
// ============================================================================

uint32_t RenderLoop::addTimer(int intervalMs, std::function<void()> callback)
{
    std::lock_guard<std::mutex> lk(timersMutex_);
    RenderTimer t;
    t.id         = nextTimerId_++;
    t.intervalMs = intervalMs;
    t.callback   = std::move(callback);
    t.nextFire   = Clock::now() + std::chrono::milliseconds(intervalMs);
    timers_.push_back(std::move(t));
    SetEvent(wakeEvent_); // wake so the new wait timeout is computed
    return timers_.back().id;
}

void RenderLoop::removeTimer(uint32_t id)
{
    std::lock_guard<std::mutex> lk(timersMutex_);
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [id](const RenderTimer& t){ return t.id == id; }),
        timers_.end());
}

// ============================================================================
// Thread entry point
// ============================================================================

DWORD WINAPI RenderLoop::threadProc(LPVOID param)
{
    auto* self = reinterpret_cast<RenderLoop*>(param);
    self->runLoop();
    return 0;
}

// ============================================================================
// msUntilNextTimer
// Returns how many ms until the soonest timer fires, or INT_MAX if none.
// ============================================================================

int RenderLoop::msUntilNextTimer()
{
    std::lock_guard<std::mutex> lk(timersMutex_);
    if (timers_.empty())
        return INT_MAX;

    auto now = Clock::now();
    int minMs = INT_MAX;
    for (auto& t : timers_)
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      t.nextFire - now).count();
        int remaining = (ms < 0) ? 0 : (int)ms;
        if (remaining < minMs)
            minMs = remaining;
    }
    return minMs;
}

// ============================================================================
// tickTimers
// Fire all timers whose nextFire <= now. Called from the render thread.
// ============================================================================

void RenderLoop::tickTimers()
{
    auto now = Clock::now();

    // Copy snapshot to avoid holding the lock during callbacks.
    std::vector<RenderTimer> snapshot;
    {
        std::lock_guard<std::mutex> lk(timersMutex_);
        snapshot = timers_;
    }

    bool anyFired = false;
    for (auto& t : snapshot)
    {
        if (now >= t.nextFire)
        {
            t.callback();
            anyFired = true;
        }
    }

    // Advance nextFire for fired timers.
    if (anyFired)
    {
        std::lock_guard<std::mutex> lk(timersMutex_);
        for (auto& live : timers_)
        {
            if (now >= live.nextFire)
            {
                // Advance by interval; skip missed beats to avoid pile-up.
                do {
                    live.nextFire += std::chrono::milliseconds(live.intervalMs);
                } while (live.nextFire <= now);
            }
        }

        // Timer callbacks may update widget state → we need a paint.
        paintDirty_.store(true);
    }
}

// ============================================================================
// runLoop — the render thread body
// ============================================================================

void RenderLoop::runLoop()
{
    constexpr int kTargetFrameMs = 16;

    while (running_.load())
    {
        DWORD waitMs;
        if (continuous_.load())
        {
            waitMs = 0;
        }
        else
        {
            int timerMs = msUntilNextTimer();
            waitMs = (timerMs == INT_MAX) ? INFINITE : (DWORD)timerMs;
        }

        fluxLog("[runLoop] waiting waitMs=" +
                (waitMs == INFINITE ? std::string("INFINITE")
                                    : std::to_string(waitMs)) +
                " continuous=" + std::to_string(continuous_.load()) +
                " paintDirty=" + std::to_string(paintDirty_.load()) +
                " layoutDirty=" + std::to_string(layoutDirty_.load()));

        if (waitMs > 0)
            WaitForSingleObject(wakeEvent_, waitMs);

        if (!running_.load()) break;

        if (resizePending_.exchange(false))
        {
            int newW = pendingW_.load();
            int newH = pendingH_.load();
            fluxLog("[runLoop] handling resize " +
                    std::to_string(newW) + "x" + std::to_string(newH));
            if (onResize) onResize(newW, newH);
        }

        tickTimers();

        if (layoutDirty_.exchange(false))
        {
            fluxLog("[runLoop] running layout");
            if (onLayout) onLayout();
        }

        bool shouldPaint = paintDirty_.exchange(false) || continuous_.load();
        fluxLog("[runLoop] shouldPaint=" + std::to_string(shouldPaint));

        if (shouldPaint)
        {
            auto now = Clock::now();
            float dt = std::chrono::duration<float>(now - lastFrame_).count();
            lastFrame_ = now;

            if (onFrame) onFrame(dt);

            if (continuous_.load())
            {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   Clock::now() - now).count();
                int sleepMs = kTargetFrameMs - (int)elapsed;
                if (sleepMs > 0)
                    Sleep((DWORD)sleepMs);
            }
        }
    }
}

void RenderLoop::post(std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lk(pendingMutex_);
        pending_.push_back(std::move(fn));
    }
    markDirty();
}

void RenderLoop::drainPending()
{
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lk(pendingMutex_);
        local.swap(pending_);
    }
    for (auto& fn : local)
        fn();
}

#endif // _WIN32
