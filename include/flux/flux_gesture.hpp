#pragma once
#include "flux_platform.hpp"
#include <cmath>
#include <cstdint>

// ============================================================================
// GestureState
// ============================================================================
//
// Translates raw pointer/touch events into scroll deltas + fling velocity.
// Sits between the platform event and ScrollbarState::applyDelta().
//
// Usage (inside a scrollable widget):
//
//   GestureState gesture;          // one per widget, alongside ScrollbarState
//
//   handleMouseDown / touch ACTION_DOWN:
//     gesture.onDown(x, y);
//
//   handleMouseMove / touch ACTION_MOVE:
//     int delta = gesture.onMove(x, y);   // returns pixel delta along scroll axis
//     sb.scrollOffset -= delta;           // apply immediately (direct drag)
//     sb.clamp(); sb.updateThumb();
//
//   handleMouseUp / touch ACTION_UP:
//     gesture.onUp(x, y);
//     // fling velocity is now set — start a timer to decay it
//
//   handleTimer (fling decay):
//     int delta = gesture.tickFling();    // returns 0 when deceleration ends
//     if (delta != 0) { sb.scrollOffset -= delta; sb.clamp(); ... }
//
// For horizontal lists, pass the x coordinate to onMove; for vertical lists,
// pass y.  The helper methods onMoveV / onMoveH do this automatically.

struct GestureState {
    // ── Configuration ─────────────────────────────────────────────────────
    bool  horizontal    = false;   // true = horizontal scroll axis
    float deceleration  = 0.85f;   // velocity multiplier per timer tick (~16ms)
    int   minFlingSpeed = 5;       // px/tick below which fling stops
    int   maxFlingSpeed = 120;     // clamp initial fling velocity

    // ── State ──────────────────────────────────────────────────────────────
    bool  isDragging    = false;
    int   dragStartX    = 0;
    int   dragStartY    = 0;
    int   lastX         = 0;
    int   lastY         = 0;

    // Velocity sampling — keep last 3 move events for a stable estimate
    static constexpr int kSamples = 3;
    int   velSamples[kSamples]    = {};
    int   sampleHead              = 0;
    int   sampleCount             = 0;

    float flingVelocity           = 0.f;  // px/tick, positive = forward

    // ── Down ──────────────────────────────────────────────────────────────

    void onDown(int x, int y) {
        isDragging    = true;
        dragStartX    = x;
        dragStartY    = y;
        lastX         = x;
        lastY         = y;
        flingVelocity = 0.f;
        sampleHead    = 0;
        sampleCount   = 0;
        for (int &s : velSamples) s = 0;
    }

    // ── Move — returns signed delta pixels along the scroll axis ──────────
    //
    // Positive delta → content moves forward (offset increases).
    // The caller should do:  scrollOffset += delta; clamp(); updateThumb();

    int onMove(int x, int y) {
        if (!isDragging) return 0;

        int raw = horizontal ? (lastX - x) : (lastY - y);

        // Record sample for velocity estimate
        velSamples[sampleHead % kSamples] = raw;
        sampleHead++;
        if (sampleCount < kSamples) sampleCount++;

        lastX = x;
        lastY = y;
        return raw;
    }

    // ── Up — commits fling velocity ────────────────────────────────────────

    void onUp(int x, int y) {
        if (!isDragging) return;
        isDragging = false;

        // Average recent samples for a stable initial velocity
        if (sampleCount == 0) { flingVelocity = 0.f; return; }
        int sum = 0;
        for (int i = 0; i < sampleCount; i++) sum += velSamples[i];
        float v = (float)sum / sampleCount;

        // Clamp
        if (v >  maxFlingSpeed) v =  (float)maxFlingSpeed;
        if (v < -maxFlingSpeed) v = -(float)maxFlingSpeed;

        flingVelocity = (std::abs(v) >= minFlingSpeed) ? v : 0.f;
        (void)x; (void)y;
    }

    // ── Tick — call from handleTimer to decay fling ────────────────────────
    //
    // Returns the scroll delta to apply this tick.
    // Returns 0 when fling has decayed to zero (caller should stop the timer).

    int tickFling() {
        if (std::abs(flingVelocity) < 0.5f) {
            flingVelocity = 0.f;
            return 0;
        }
        int delta = (int)flingVelocity;
        flingVelocity *= deceleration;
        if (std::abs(flingVelocity) < (float)minFlingSpeed)
            flingVelocity = 0.f;
        return delta;
    }

    bool isFling() const { return std::abs(flingVelocity) >= 0.5f; }

    // ── Cancel (e.g. finger lifted outside window) ─────────────────────────

    void cancel() {
        isDragging    = false;
        flingVelocity = 0.f;
    }
};