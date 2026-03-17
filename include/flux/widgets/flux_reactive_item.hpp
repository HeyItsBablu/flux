#ifndef FLUX_REACTIVE_ITEM_HPP
#define FLUX_REACTIVE_ITEM_HPP

// ============================================================================
// ReactiveItem<T>
// ============================================================================
//
// A thin reactive wrapper around a plain struct. Unlike State<T>, it has no
// dependency on a UIContext and needs no "context" argument at construction.
//
// Designed to be stored inside State<vector<shared_ptr<ReactiveItem<T>>>>
// so that:
//   - Structural changes  (add / delete)  go through the outer State<vector>
//     and trigger a list-level diff + keyed rebuild.
//   - Field-level mutations  (toggle, rename, …)  go through ReactiveItem
//     and trigger only the single row that owns this item.
//
// ── Usage ────────────────────────────────────────────────────────────────
//
//   struct Task { int id; std::string title; bool done = false; };
//   using TaskRef = std::shared_ptr<ReactiveItem<Task>>;
//
//   // Construction — plain aggregate init, zero UI boilerplate
//   auto t = std::make_shared<ReactiveItem<Task>>(Task{1, "Buy milk"});
//
//   // Read
//   const Task& data = t->get();
//
//   // Mutate a single field and notify subscribers
//   t->update([](Task& d){ d.done = !d.done; });
//
//   // Replace the whole value
//   t->set(Task{1, "Buy oat milk", true});
//
//   // Subscribe (used internally by ListViewBuilder)
//   t->listen([](const Task& d){ /* … */ });
//
// ── Key function ─────────────────────────────────────────────────────────
//
//   By default ListViewBuilder uses the pointer address as the item key,
//   so pointer-stable items (shared_ptr kept alive across set() calls) are
//   recognised as "same item, just mutated" automatically.
//
//   If you prefer identity by a data field supply a key function:
//
//     ListView(items)
//       ->setKeyFn([](const TaskRef& r) -> uintptr_t { return r->get().id; })
//       ->itemBuilder(…);
//
// ============================================================================

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

template <typename T>
class ReactiveItem {
public:
    using ValueType    = T;
    using Listener     = std::function<void(const T&)>;
    using ListenerList = std::vector<Listener>;

    // ── Construction ─────────────────────────────────────────────────────

    explicit ReactiveItem(T data) : _data(std::move(data)) {}

    // Non-copyable — always held by shared_ptr
    ReactiveItem(const ReactiveItem&)            = delete;
    ReactiveItem& operator=(const ReactiveItem&) = delete;

    // ── Read ─────────────────────────────────────────────────────────────

    const T& get() const { return _data; }

    // ── Mutate & notify ───────────────────────────────────────────────────

    // Replace the whole value.
    void set(T newData) {
        _data = std::move(newData);
        notify();
    }

    // Mutate in-place via a lambda — avoids a full copy when only one field
    // changes.  The lambda receives a T& and should modify it directly.
    //
    //   ri->update([](Task& d){ d.done = !d.done; });
    //
    template <typename Fn,
              typename = std::enable_if_t<std::is_invocable_v<Fn, T&>>>
    void update(Fn&& mutator) {
        std::forward<Fn>(mutator)(_data);
        notify();
    }

    // ── Subscription ─────────────────────────────────────────────────────

    // Returns a handle that can be stored to unsubscribe later.
    // If you don't need to unsubscribe, ignore the return value.
    size_t listen(Listener fn) {
        size_t id = _nextId++;
        _listeners.push_back({id, std::move(fn)});
        return id;
    }

    void unlisten(size_t id) {
        _listeners.erase(
            std::remove_if(_listeners.begin(), _listeners.end(),
                           [id](const Entry& e){ return e.id == id; }),
            _listeners.end());
    }

    // How many subscribers are currently attached (useful for debugging)
    size_t listenerCount() const { return _listeners.size(); }

private:
    struct Entry { size_t id; Listener fn; };

    T                 _data;
    std::vector<Entry> _listeners;
    size_t            _nextId = 0;

    void notify() {
        // Copy the list in case a listener unsubscribes itself mid-call
        auto snapshot = _listeners;
        for (auto& e : snapshot) e.fn(_data);
    }
};

// ── Convenience alias ─────────────────────────────────────────────────────────

template <typename T>
using ReactiveItemPtr = std::shared_ptr<ReactiveItem<T>>;

// Factory helper — mirrors make_shared sugar
//   auto item = MakeReactive(Task{1, "Buy milk"});
template <typename T>
ReactiveItemPtr<T> MakeReactive(T data) {
    return std::make_shared<ReactiveItem<T>>(std::move(data));
}

#endif // FLUX_REACTIVE_ITEM_HPP