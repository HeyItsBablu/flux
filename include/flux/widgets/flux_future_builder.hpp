#pragma once
#include "flux/flux_core.hpp"
#include "flux/flux_http.hpp"
#include "flux/flux_http_platform.hpp"
#include "flux/flux_hydration.hpp"
#include "flux/flux_json.hpp"
#include "flux/flux_widget.hpp"
#include <functional>
#include <memory>
#include <string>

// ============================================================================
// ASYNC SNAPSHOT — mirrors Flutter's ConnectionState
// ============================================================================

enum class ConnectionState
{
  None,
  Waiting,
  Done,
  Error
};

template <typename T>
struct AsyncSnapshot
{
  ConnectionState state = ConnectionState::None;
  T data{};
  std::string error;

  bool hasData() const { return state == ConnectionState::Done; }
  bool hasError() const { return state == ConnectionState::Error; }
  bool isLoading() const { return state == ConnectionState::Waiting; }
  bool isNone() const { return state == ConnectionState::None; }
};

// ============================================================================
// FUTURE BUILDER WIDGET
// T is the data type your builder fn receives once the request completes.
// ============================================================================

template <typename T>
class FutureBuilderWidget : public Widget
{
public:
  using Builder = std::function<WidgetPtr(const AsyncSnapshot<T> &)>;
  using Fetcher =
      std::function<void(std::function<void(T)> /*onSuccess*/,
                         std::function<void(std::string)> /*onError*/)>;

  // Converts a raw hydration string (see flux_hydration.hpp) into T.
  // Returns false if the raw value couldn't be turned into a valid T —
  // callers treat that identically to "no hydration data at all" and
  // fall through to a real fetch. Set automatically by FetchBuilder/
  // JsonBuilder/TypedJsonBuilder below; can also be set directly for a
  // hand-built FutureBuilderWidget<T>.
  using HydrationMapper = std::function<bool(const std::string &, T &)>;

  Builder builder;
  Fetcher fetcher;
  HydrationMapper hydrationMapper;

  FutureBuilderWidget()
  {
    // Assigned at CONSTRUCTION time, not at first layout — construction
    // order is what's actually deterministic and shared between a future
    // SSR host and the client (both walk createApp()'s same code path in
    // the same order); layout timing is not something a headless
    // single-pass SSR render and a live, possibly-reflowed browser
    // session are guaranteed to agree on.
    hydrationId_ = fluxHydrationNextId();
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {

    if (snapshot_.state == ConnectionState::None && !tryHydrate())
      startFetch();

    rebuildChild(ctx, fontCache);

    auto c = child_;
    if (c)
    {
      c->computeLayout(ctx, constraints, fontCache);
      width = c->width;
      height = c->height;
    }
    else
    {
      width = constraints.clampWidth(0);
      height = constraints.clampHeight(0);
    }
    needsLayout = false;
  }

  void positionChildren(int cx, int cy, int /*cw*/, int /*ch*/) override
  {

    auto c = child_;
    if (!c)
      return;

    c->x = cx;
    c->y = cy;
    c->positionChildren(
        cx + c->paddingLeft,
        cy + c->paddingTop,
        c->width - c->paddingLeft - c->paddingRight,
        c->height - c->paddingTop - c->paddingBottom);
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    // FIX: same local-snapshot pattern.
    auto c = child_;
    if (c)
      c->render(ctx, fontCache);
    needsPaint = false;
  }

  // -----------------------------------------------------------------------
  // Fluent setters
  // -----------------------------------------------------------------------
  std::shared_ptr<FutureBuilderWidget<T>> setBuilder(Builder b)
  {
    builder = std::move(b);
    return self();
  }
  std::shared_ptr<FutureBuilderWidget<T>> setFetcher(Fetcher f)
  {
    fetcher = std::move(f);
    return self();
  }
  std::shared_ptr<FutureBuilderWidget<T>> setHydrationMapper(HydrationMapper m)
  {
    hydrationMapper = std::move(m);
    return self();
  }

  // Manual override — lets a caller directly hand this widget an already-
  // resolved snapshot (bypassing hydrationMapper entirely), for cases
  // where the raw-string automatic path above doesn't fit. Matches the
  // roadmap's original wording exactly: "here's the answer already,
  // don't fetch again."
  std::shared_ptr<FutureBuilderWidget<T>> seedFromHydration(AsyncSnapshot<T> snap)
  {
    snapshot_ = std::move(snap);
    fetchStarted_ = true; // suppresses the computeLayout() fetch trigger
    return self();
  }

  const std::string &hydrationId() const { return hydrationId_; }

  void refresh()
  {
    snapshot_ = {};
    fetchStarted_ = false;
    markNeedsLayout();
  }

private:
  AsyncSnapshot<T> snapshot_;
  WidgetPtr child_;

  bool fetchStarted_ = false;
  std::string hydrationId_;

  // Returns true if hydration data existed for this widget AND the
  // mapper successfully converted it — in which case snapshot_ is now
  // Done and startFetch() must NOT run. Returns false in every other
  // case (no mapper set, no data for this id, or the mapper rejected the
  // raw value), leaving snapshot_ untouched so the normal fetch path runs
  // exactly as it did before this file existed.
  bool tryHydrate()
  {
    if (!hydrationMapper)
      return false;
    const std::string *raw = fluxHydrationGetWidgetData(hydrationId_);
    if (!raw)
      return false;
    T value;
    if (!hydrationMapper(*raw, value))
      return false;
    snapshot_.state = ConnectionState::Done;
    snapshot_.data = std::move(value);
    fetchStarted_ = true;
    return true;
  }

  void startFetch()
  {
    if (fetchStarted_)
      return;
    fetchStarted_ = true;
    snapshot_.state = ConnectionState::Waiting;

    auto weak = std::weak_ptr<FutureBuilderWidget<T>>(self());

    auto onSuccess = [weak](T value)
    {
      // This lambda is always invoked on the UI thread (postToUI=true).
      auto self = weak.lock();
      if (!self)
        return; // widget was destroyed — safe to bail

      self->snapshot_.state = ConnectionState::Done;
      self->snapshot_.data = std::move(value);
      self->triggerRebuild();
    };

    auto onError = [weak](std::string err)
    {
      // Same: always UI thread.
      auto self = weak.lock();
      if (!self)
        return;

      self->snapshot_.state = ConnectionState::Error;
      self->snapshot_.error = std::move(err);
      self->triggerRebuild();
    };

    if (fetcher)
      fetcher(std::move(onSuccess), std::move(onError));
  }

  void triggerRebuild()
  {
    needsLayout = true;
    needsPaint = true;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->scheduleRebuild(this); 
  }

  void rebuildChild(GraphicsContext & /*ctx*/, FontCache & /*fontCache*/)
  {
    if (!builder)
      return;

    child_ = builder(snapshot_);
    auto c = child_;
    if (!c)
      return;

    c->parent = this;

    // Propagate font from parent whenever parent has a non-empty override.
    if (!fontFamily.empty())
      c->fontFamily = fontFamily;
  }

  std::shared_ptr<FutureBuilderWidget<T>> self()
  {
    return std::static_pointer_cast<FutureBuilderWidget<T>>(shared_from_this());
  }
};

// ============================================================================
// FACTORY HELPERS
// ============================================================================

// ── FetchBuilder — raw string response ──────────────────────────────────────

inline std::shared_ptr<FutureBuilderWidget<std::string>> FetchBuilder(
    const std::string &url,
    std::function<WidgetPtr(const AsyncSnapshot<std::string> &)> builder,
    bool postToUI = true)
{
  auto w = std::make_shared<FutureBuilderWidget<std::string>>();
  w->setBuilder(builder);
  // Raw text, hand-fetched with FluxHttp::get — the hydration value IS
  // the response body verbatim, no JSON parsing involved either way.
  w->setHydrationMapper([](const std::string &raw, std::string &out)
                        { out = raw; return true; });
  w->setFetcher([url, postToUI](std::function<void(std::string)> onSuccess,
                                std::function<void(std::string)> onError)
                {

    (void)postToUI;
    FluxHttp::get(
        url,
        [onSuccess, onError](HttpResult r) {
          if (r.success)
            onSuccess(r.body);
          else
            onError(r.error.empty()
                        ? "HTTP " + std::to_string(r.statusCode)
                        : r.error);
        },
        /*postToUI=*/true); });
  return w;
}

// ── JsonBuilder — parsed JsonValue ──────────────────────────────────────────

inline std::shared_ptr<FutureBuilderWidget<JsonValue>>
JsonBuilder(const std::string &url,
            std::function<WidgetPtr(const AsyncSnapshot<JsonValue> &)> builder,
            bool postToUI = true)
{
  auto w = std::make_shared<FutureBuilderWidget<JsonValue>>();
  w->setBuilder(builder);
  // T is JsonValue itself — the hydration value is just the raw JSON
  // text; parsing it IS the whole conversion, no field extraction needed.
  w->setHydrationMapper([](const std::string &raw, JsonValue &out)
                        { return JsonParser::tryParse(raw, out); });
  w->setFetcher([url, postToUI](std::function<void(JsonValue)> onSuccess,
                                std::function<void(std::string)> onError)
                {
    (void)postToUI;
    FluxHttp::get(
        url,
        [onSuccess, onError](HttpResult r) {
          if (!r.success) {
            onError(r.error.empty()
                        ? "HTTP " + std::to_string(r.statusCode)
                        : r.error);
            return;
          }
          JsonValue parsed;
          if (JsonParser::tryParse(r.body, parsed))
            onSuccess(std::move(parsed));
          else
            onError("JSON parse error");
        },
        /*postToUI=*/true); });
  return w;
}

// ── TypedJsonBuilder — deserialized T using a user-supplied mapper ───────────

template <typename T>
std::shared_ptr<FutureBuilderWidget<T>>
TypedJsonBuilder(const std::string &url,
                 std::function<T(const JsonValue &)> mapper,
                 std::function<WidgetPtr(const AsyncSnapshot<T> &)> builder,
                 bool postToUI = true)
{
  auto w = std::make_shared<FutureBuilderWidget<T>>();
  w->setBuilder(builder);

  // Reuses the EXACT SAME user-supplied `mapper` the caller already gave
  // us for the live-fetch path below — one mapper, two callers (a real
  // network response's parsed JSON, or a hydrated raw string's parsed
  // JSON). This is deliberate: whatever JsonValue-consuming logic the
  // caller already trusts for live data is automatically correct for
  // hydration too, with no separate mapper to keep in sync.
  w->setHydrationMapper([mapper](const std::string &raw, T &out) -> bool
                        {
                          JsonValue parsed;
                          if (!JsonParser::tryParse(raw, parsed))
                            return false;
                          try { out = mapper(parsed); return true; }
                          catch (...) { return false; }
                        });
  w->setFetcher(
      [url, mapper, postToUI](std::function<void(T)> onSuccess,
                              std::function<void(std::string)> onError)
      {
        (void)postToUI;
        FluxHttp::get(
            url,
            [onSuccess, onError, mapper](HttpResult r)
            {
              if (!r.success)
              {
                onError(r.error.empty()
                            ? "HTTP " + std::to_string(r.statusCode)
                            : r.error);
                return;
              }
              JsonValue parsed;
              if (!JsonParser::tryParse(r.body, parsed))
              {
                onError("JSON parse error");
                return;
              }
              try
              {
                onSuccess(mapper(parsed));
              }
              catch (const std::exception &e)
              {
                onError(e.what());
              }
            },
            /*postToUI=*/true);
      });
  return w;
}