#pragma once
#include "../flux_core.hpp"
#include "../flux_json.hpp"
#include "../flux_widget.hpp"
#include "../flux_socket.hpp"

#include <functional>
#include <memory>
#include <string>

// ============================================================================
// STREAM SNAPSHOT — mirrors AsyncSnapshot / ConnectionState
// ============================================================================

enum class StreamState {
    None,        // not yet started
    Connecting,  // socket handshake in progress
    Active,      // connection open, data may have arrived
    Done,        // server closed cleanly
    Error        // connection failed or server error
};

template <typename T>
struct StreamSnapshot {
    StreamState state = StreamState::None;
    T           data{};
    std::string error;

    bool hasData()       const { return state == StreamState::Active && dataReady; }
    bool isConnecting()  const { return state == StreamState::Connecting; }
    bool isActive()      const { return state == StreamState::Active; }
    bool isDone()        const { return state == StreamState::Done; }
    bool hasError()      const { return state == StreamState::Error; }

    // Internal flag — set to true once the first message has been decoded.
    bool dataReady = false;
};

// ============================================================================
// STREAM BUILDER WIDGET
//
// Drop-in companion to FutureBuilderWidget.  Opens a WebSocket when first
// laid out and calls builder() on every incoming message so the UI always
// reflects the latest server push.
//
// Usage:
//   auto w = StreamBuilder<JsonValue>("wss://example.com/feed",
//       [](const StreamSnapshot<JsonValue> &snap) -> WidgetPtr {
//           if (snap.isConnecting()) return Text("Connecting…");
//           if (snap.hasError())     return Text("Error: " + snap.error);
//           if (!snap.hasData())     return Text("Waiting for data…");
//           return Text(snap.data["price"].getString());
//       },
//       [](const std::string &raw, JsonValue &out) {
//           return JsonParser::tryParse(raw, out);
//       });
// ============================================================================

template <typename T>
class StreamBuilderWidget : public Widget {
public:
    using Builder = std::function<WidgetPtr(const StreamSnapshot<T> &)>;

    // Called on every raw text frame — decode it into T.
    // Return false to treat the frame as a non-fatal parse error.
    using Decoder = std::function<bool(const std::string &raw, T &out)>;

    Builder builder;
    Decoder decoder;

    // ── computeLayout ────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                       FontCache &fontCache) override {
        if (snapshot_.state == StreamState::None)
            startStream();

        rebuildChild(ctx, fontCache);

        auto c = child_;
        if (c) {
            c->computeLayout(ctx, constraints, fontCache);
            width  = c->width;
            height = c->height;
        } else {
            width  = constraints.clampWidth(0);
            height = constraints.clampHeight(0);
        }
        needsLayout = false;
    }

    // ── positionChildren ─────────────────────────────────────────────────────
    void positionChildren(int cx, int cy, int /*cw*/, int /*ch*/) override {
        auto c = child_;
        if (!c) return;
        c->x = cx;
        c->y = cy;
        c->positionChildren(
            cx + c->paddingLeft, cy + c->paddingTop,
            c->width  - c->paddingLeft - c->paddingRight,
            c->height - c->paddingTop  - c->paddingBottom);
    }

    // ── render ───────────────────────────────────────────────────────────────
    void render(GraphicsContext &ctx, FontCache &fontCache) override {
        auto c = child_;
        if (c) c->render(ctx, fontCache);
        needsPaint = false;
    }

    // ── onDetach — close socket when widget leaves the tree ──────────────────
    void onDetach() override {
        if (socket_) socket_->close();
        Widget::onDetach();
    }

    // ── Public controls ───────────────────────────────────────────────────────

    // Send an arbitrary text frame to the server.
    void sendMessage(const std::string &msg) {
        if (socket_) socket_->send(msg);
    }


    void reconnect() {
        if (socket_) socket_->close();
        socket_.reset();
        snapshot_      = {};
        child_.reset();
        markNeedsLayout();
    }

    const StreamSnapshot<T> &snapshot() const { return snapshot_; }

    // ── Fluent setters ────────────────────────────────────────────────────────
    std::shared_ptr<StreamBuilderWidget<T>> setBuilder(Builder b) {
        builder = std::move(b); return self();
    }
    std::shared_ptr<StreamBuilderWidget<T>> setDecoder(Decoder d) {
        decoder = std::move(d); return self();
    }
    std::shared_ptr<StreamBuilderWidget<T>> setUrl(std::string u) {
        url_ = std::move(u); return self();
    }

private:
    std::string              url_;
    StreamSnapshot<T>        snapshot_;
    WidgetPtr                child_;
    std::shared_ptr<FluxSocket> socket_;

    // ── Open the socket and wire up event callbacks ───────────────────────────
    void startStream() {
        snapshot_.state = StreamState::Connecting;

        socket_ = std::make_shared<FluxSocket>();
        auto weak = std::weak_ptr<StreamBuilderWidget<T>>(self());

        socket_->onOpen = [weak]() {
            auto s = weak.lock(); if (!s) return;
            s->snapshot_.state = StreamState::Active;
            s->triggerRebuild();
        };

        socket_->onMessage = [weak](std::string raw) {
            auto s = weak.lock(); if (!s) return;
            T value{};
            if (s->decoder && !s->decoder(raw, value)) {
                // Soft decode error — stay active, keep last good data.
                return;
            }
            s->snapshot_.state     = StreamState::Active;
            s->snapshot_.data      = std::move(value);
            s->snapshot_.dataReady = true;
            s->triggerRebuild();
        };

        socket_->onError = [weak](std::string err) {
            auto s = weak.lock(); if (!s) return;
            s->snapshot_.state = StreamState::Error;
            s->snapshot_.error = std::move(err);
            s->triggerRebuild();
        };

        socket_->onClose = [weak](int /*code*/) {
            auto s = weak.lock(); if (!s) return;
            if (s->snapshot_.state != StreamState::Error)
                s->snapshot_.state = StreamState::Done;
            s->triggerRebuild();
        };

        socket_->connect(url_);
    }

    void triggerRebuild() {
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->partialRebuild(this);
        markNeedsLayout();
    }

    void rebuildChild(GraphicsContext & /*ctx*/, FontCache & /*fontCache*/) {
        if (!builder) return;
        child_ = builder(snapshot_);
        auto c = child_;
        if (!c) return;
        c->parent = this;
        if (!fontFamily.empty()) c->fontFamily = fontFamily;
    }

    std::shared_ptr<StreamBuilderWidget<T>> self() {
        return std::static_pointer_cast<StreamBuilderWidget<T>>(shared_from_this());
    }
};

// ============================================================================
// FACTORY HELPERS  (mirror FetchBuilder / JsonBuilder / TypedJsonBuilder)
// ============================================================================

// ── StreamBuilder — raw text frames ─────────────────────────────────────────

inline std::shared_ptr<StreamBuilderWidget<std::string>>
StreamBuilder(
    const std::string &url,
    std::function<WidgetPtr(const StreamSnapshot<std::string> &)> builder)
{
    auto w = std::make_shared<StreamBuilderWidget<std::string>>();
    w->setUrl(url);
    w->setBuilder(std::move(builder));
    // No decoder needed — raw string passthrough.
    w->setDecoder([](const std::string &raw, std::string &out) {
        out = raw;
        return true;
    });
    return w;
}

// ── JsonStreamBuilder — auto-parsed JsonValue on every frame ─────────────────

inline std::shared_ptr<StreamBuilderWidget<JsonValue>>
JsonStreamBuilder(
    const std::string &url,
    std::function<WidgetPtr(const StreamSnapshot<JsonValue> &)> builder)
{
    auto w = std::make_shared<StreamBuilderWidget<JsonValue>>();
    w->setUrl(url);
    w->setBuilder(std::move(builder));
    w->setDecoder([](const std::string &raw, JsonValue &out) {
        return JsonParser::tryParse(raw, out);
    });
    return w;
}

// ── TypedStreamBuilder — user-supplied mapper from JsonValue → T ─────────────

template <typename T>
std::shared_ptr<StreamBuilderWidget<T>>
TypedStreamBuilder(
    const std::string &url,
    std::function<T(const JsonValue &)> mapper,
    std::function<WidgetPtr(const StreamSnapshot<T> &)> builder)
{
    auto w = std::make_shared<StreamBuilderWidget<T>>();
    w->setUrl(url);
    w->setBuilder(std::move(builder));
    w->setDecoder([mapper](const std::string &raw, T &out) {
        JsonValue parsed;
        if (!JsonParser::tryParse(raw, parsed)) return false;
        try { out = mapper(parsed); return true; }
        catch (...) { return false; }
    });
    return w;
}