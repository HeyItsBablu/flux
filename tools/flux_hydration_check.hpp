#pragma once

// ============================================================================
// flux_hydration_check.hpp  (developer/debug tool only — not part of the
// runtime library, not required for the app to function)
//
// Early-warning system for "something in a widget isn't SSR-safe yet."
// Computes a cheap structural fingerprint of the widget tree (widget type
// name + child count, walked pre-order) and compares it against a
// fingerprint a server (Phase 4) is expected to embed alongside its
// hydration blob under the reserved id "__structHash__".
//
// A mismatch means the SERVER and CLIENT built genuinely different trees
// for what should have been the identical page — e.g. a widget whose
// constructor branches on something that differs between the two
// environments (a std::this_thread-dependent value, an OS-specific
// #ifdef that shouldn't be there, a data-dependent conditional that
// forgot to seed from hydration first and built a "loading" placeholder
// client-side instead of matching the server's already-resolved content).
//
// Usage today (no real server yet) — MANUAL TEST HARNESS:
//
//   // Pretend to be "the server": build the tree once, compute its hash,
//   // and manually stash it exactly where a real server would embed it.
//   WidgetPtr serverTree = createApp(app);
//   std::string serverHash = fluxComputeTreeStructureHash(serverTree.get());
//   fluxHydrationAddWidgetData("__structHash__", serverHash); // pretend blob
//
//   // ... later, simulate the client's independent build ...
//   WidgetPtr clientTree = createApp(app);
//   fluxCheckHydrationMismatch(clientTree.get()); // logs if they disagree
//
// Real usage (once Phase 4 exists): the SSR host calls
// fluxComputeTreeStructureHash() on the tree it just rendered and writes
// the result into the blob under "__structHash__" the same way any other
// widget id is added (fluxHydrationAddWidgetData). web/main.cpp's existing
// hydration-parse step (see web/main.cpp) already makes that value
// available via fluxHydrationGetWidgetData — this file just adds the
// comparison step, which the roadmap suggests calling once, right after
// the first build() completes.
// ============================================================================

#include "flux/flux_widget.hpp"
#include "flux/flux_hydration.hpp"

#include <string>
#include <typeinfo>
#include <cstdint>
#include <iostream>

namespace flux_hydration_check_detail
{
    // Simple, deterministic combine — doesn't need to be cryptographic,
    // just needs to reliably differ when the tree SHAPE differs.
    inline void hashCombine(uint64_t &seed, uint64_t v)
    {
        seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    }

    inline void walk(Widget *w, uint64_t &hash)
    {
        if (!w)
            return;
        // typeid(*w).name() is compiler-specific (mangled) but STABLE
        // within one build — server and client are built from the same
        // source, so the same widget type produces the same string on
        // both sides. Not meant to be human-readable, only comparable.
        std::hash<std::string> strHash;
        hashCombine(hash, strHash(typeid(*w).name()));
        hashCombine(hash, (uint64_t)w->children.size());
        for (auto &child : w->children)
            walk(child.get(), hash);
    }
}

inline std::string fluxComputeTreeStructureHash(Widget *root)
{
    uint64_t hash = 1469598103934665603ULL; // FNV offset basis, arbitrary
                                            // deterministic seed
    flux_hydration_check_detail::walk(root, hash);
    return std::to_string(hash);
}

// Logs a warning (FLUX_DEBUG builds only — this is a dev tool, never
// meant to run, or cost anything, in a release build) if the current
// tree's structural hash doesn't match the one embedded in the hydration
// blob under "__structHash__". No-op if that id was never present (e.g.
// a plain client-side load with no SSR involved at all — nothing to
// compare against, and that's expected, not an error).
inline void fluxCheckHydrationMismatch(Widget *root)
{
#ifdef FLUX_DEBUG
    const std::string *expected = fluxHydrationGetWidgetData("__structHash__");
    if (!expected)
        return; // no server comparison available — not an error

    std::string actual = fluxComputeTreeStructureHash(root);
    if (actual != *expected)
    {
        std::cerr << "[flux_hydration_check] MISMATCH: client tree structure "
                  << "differs from server's. server=" << *expected
                  << " client=" << actual
                  << " — a widget is building different content on the "
                     "client than the server rendered. Check for anything "
                     "that should be reading hydrated data (Navigator::"
                     "hydratedData<T> / FutureBuilderWidget hydration) but "
                     "is instead fetching or branching client-only.\n";
    }
#else
    (void)root;
#endif
}