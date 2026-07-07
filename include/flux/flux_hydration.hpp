#pragma once

// ============================================================================
// flux_hydration.hpp
//
// Defines the data-handoff contract between a future SSR host (Phase 4) and
// the browser: HOW a server hands data it already fetched to the client, so
// the client never re-fetches (and never flashes a "loading..." state right
// after real content already showed).
//
// Deliberately NOT Emscripten-only — Phase 4's SSR host is a native
// (non-web) build that will call the SETTER side of this API
// (fluxHydrationSetPageData / fluxHydrationAddWidgetData) to build the blob
// it embeds in its HTML output. The browser calls the READER side
// (fluxHydrationParseBlob + the Get*/Has* queries) to consume it. Both
// sides share this one file so the contract can never drift between them.
//
// Wire format — deliberately simple
// ──────────────────────────────────
// Every hydration value (page-level or per-widget) is stored and
// transported as a RAW STRING, never a parsed object. Consumers
// (Navigator::hydratedData<T>, FutureBuilderWidget's hydration mappers)
// already know how to turn a raw string into T — they reuse the exact
// JsonParser::tryParse(str, JsonValue&) + mapper(JsonValue) pattern
// TypedJsonBuilder already established in flux_future_builder.hpp. This
// file never needs to know anything about JsonValue's internals beyond
// that one function existing.
//
// Stable widget IDs
// ─────────────────
// A widget that wants hydration data (today: FutureBuilderWidget) needs an
// identity a server and a client can BOTH independently arrive at, without
// ever communicating — a raw pointer address obviously can't work (server
// and client are different processes). The fix: assign IDs by COUNTING
// in construction order, reset to zero right before the very first
// build() of a page. As long as createApp()/build() run the same
// deterministic code path on both sides (same widget tree, same order,
// same conditionals — the same requirement React/Next.js hooks have),
// the Nth hydration-aware widget constructed on the server and the Nth
// one constructed on the client are guaranteed to be "the same" widget,
// and get the same generated ID ("w0", "w1", "w2", ...).
// ============================================================================

#include <string>
#include <unordered_map>

// ── thread_local storage ─────────────────────────────────────────────────────
// Same rationale as every other piece of shared state since Phase 0: one
// browser tab, or one native app, has exactly one thread doing this work,
// so this behaves as a plain global there. It matters once Phase 4's SSR
// host renders multiple requests concurrently on different threads — each
// request's hydration data and ID counter must be fully isolated from
// every other concurrently-rendering request.

namespace flux_hydration_detail
{
    inline thread_local std::unordered_map<std::string, std::string> g_widgetData;
    inline thread_local std::string g_pageData;
    inline thread_local bool g_hasPageData = false;
    inline thread_local int g_idCounter = 0;
}

// ── Writer side (used by: Phase 4's SSR host; a manual test harness now) ────

inline void fluxHydrationSetPageData(const std::string &rawJson)
{
    flux_hydration_detail::g_pageData = rawJson;
    flux_hydration_detail::g_hasPageData = true;
}

inline void fluxHydrationAddWidgetData(const std::string &id, const std::string &rawValue)
{
    flux_hydration_detail::g_widgetData[id] = rawValue;
}

// Clears everything — called before parsing a fresh blob (or when a page
// load has no blob at all, e.g. plain client-side navigation with no SSR).
inline void fluxHydrationClear()
{
    flux_hydration_detail::g_widgetData.clear();
    flux_hydration_detail::g_pageData.clear();
    flux_hydration_detail::g_hasPageData = false;
}

// ── Reader side (used by: Navigator::hydratedData<T>, FutureBuilderWidget) ──

inline bool fluxHydrationHasPageData()
{
    return flux_hydration_detail::g_hasPageData;
}

inline const std::string &fluxHydrationGetPageData()
{
    return flux_hydration_detail::g_pageData;
}

// Returns nullptr if no data was ever supplied for this id — deliberately
// distinct from "data was supplied but empty," which a caller may want to
// treat differently.
inline const std::string *fluxHydrationGetWidgetData(const std::string &id)
{
    auto it = flux_hydration_detail::g_widgetData.find(id);
    return (it != flux_hydration_detail::g_widgetData.end()) ? &it->second : nullptr;
}

// ── Stable ID allocator ──────────────────────────────────────────────────────

// Call ONCE, right before the very first build() of a page — never on
// subsequent Navigator page swaps (those are client-only transitions with
// no server counterpart to stay in sync with).
inline void fluxHydrationResetIdCounter()
{
    flux_hydration_detail::g_idCounter = 0;
}

inline std::string fluxHydrationNextId()
{
    return "w" + std::to_string(flux_hydration_detail::g_idCounter++);
}

// ============================================================================
// fluxHydrationSerializeBlob — server-side wire format ENCODER.
//
// The exact inverse of fluxHydrationParseBlob() below: packs whatever
// fluxHydrationSetPageData()/fluxHydrationAddWidgetData() collected during
// THIS request's render into the \x1E/\x1F-delimited wire format the
// client already knows how to decode. ssr/main.cpp calls this once, after
// rendering, to get the string it embeds in the response for the browser
// to later hand to fluxHydrationParseBlob() on boot.
//
// Didn't exist before Phase 5 because nothing needed to go this direction
// yet — Phase 3 only exercised the reader side via a manual test harness.
// ============================================================================

inline std::string fluxHydrationSerializeBlob()
{
    std::string out;
    auto appendRecord = [&](const std::string &id, const std::string &value)
    {
        if (!out.empty()) out += '\x1E';
        out += id;
        out += '\x1F';
        out += value;
    };
    if (flux_hydration_detail::g_hasPageData)
        appendRecord("__page__", flux_hydration_detail::g_pageData);
    for (auto &[id, value] : flux_hydration_detail::g_widgetData)
        appendRecord(id, value);
    return out;
}


// ============================================================================
// fluxHydrationParseBlob — client-side wire format decoder.
//
// Format: records separated by \x1E (ASCII Record Separator), each record
// is "id\x1Fvalue" (\x1F = ASCII Unit Separator). Both control characters
// are ILLEGAL inside a syntactically valid JSON string (the JSON spec
// requires every control character U+0000–U+001F to be \u-escaped), so
// they can never collide with real JSON content — no escaping needed on
// either side. The reserved id "__page__" maps to page-level data
// (Navigator::hydratedData<T>); every other id maps to a widget's data
// (FutureBuilderWidget::seedFromHydration / its automatic mapper).
//
// This function is what web/main.cpp calls, once, at boot, after reading
// the encoded string produced by shell.html.in's flattening script (see
// that file for the JS-side half of this format).
// ============================================================================

inline void fluxHydrationParseBlob(const std::string &encoded)
{
    fluxHydrationClear();
    if (encoded.empty())
        return;

    size_t recStart = 0;
    while (recStart <= encoded.size())
    {
        size_t recEnd = encoded.find('\x1E', recStart);
        std::string record = (recEnd == std::string::npos)
                                  ? encoded.substr(recStart)
                                  : encoded.substr(recStart, recEnd - recStart);

        size_t sep = record.find('\x1F');
        if (sep != std::string::npos)
        {
            std::string id = record.substr(0, sep);
            std::string value = record.substr(sep + 1);
            if (id == "__page__")
                fluxHydrationSetPageData(value);
            else
                fluxHydrationAddWidgetData(id, value);
        }

        if (recEnd == std::string::npos)
            break;
        recStart = recEnd + 1;
    }
}