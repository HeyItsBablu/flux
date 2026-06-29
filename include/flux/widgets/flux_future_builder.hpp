#pragma once
#include "../flux_core.hpp"
#include "../flux_http.hpp"
#include "../flux_http_platform.hpp"
#include "../flux_json.hpp"
#include "../flux_widget.hpp"
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

  Builder builder;
  Fetcher fetcher;

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {

    if (snapshot_.state == ConnectionState::None)
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