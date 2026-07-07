// src/flux_dom_adapter_stringbuilder.cpp
//
// Server-side implementation of IDomAdapter (flux_dom_adapter.hpp) — builds
// an HTML string in memory instead of touching a live browser document.
//
// This is the entire payoff of Phase 1's adapter-interface discipline:
// flux_painter_dom.cpp never changes for this file to work. Every
// setStyle/setAttr/setText/appendChild call it makes lands here instead of
// in flux_dom_adapter_live.cpp's EM_ASM calls, with identical semantics.

#ifdef FLUX_SSR

#include "flux/flux_dom_adapter.hpp"

#include <vector>
#include <unordered_map>
#include <string>
#include <sstream>

namespace
{
    struct PendingNode
    {
        std::string tag;
        std::unordered_map<std::string, std::string> styles; // insertion order not needed for correctness
        std::unordered_map<std::string, std::string> attrs;
        std::string text;
        std::vector<DomNodeHandle> children;
        bool isInput = false; // "input"/"textarea" — self-closing / value-via-attribute
    };

    // Minimal HTML-attribute escaping — style/attr/text values can contain
    // characters that would otherwise break out of a quoted attribute or
    // the text node itself.
    std::string escapeHtml(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
            }
        }
        return out;
    }
}

class StringBuilderDomAdapter : public IDomAdapter
{
public:
    DomNodeHandle createNode(const char *tag, const std::string &hydrationId) override
    {
        nodes_.push_back(PendingNode{});
        DomNodeHandle handle = (DomNodeHandle)nodes_.size(); // 1-based; 0 stays kInvalidDomNode
        nodes_.back().tag = tag;
        nodes_.back().isInput = (nodes_.back().tag == "input" || nodes_.back().tag == "textarea");
        // The marker flux_dom_adapter_live.cpp looks for to adopt this
        // exact element instead of recreating it on the client. Written
        // as a real HTML attribute so it survives into the served page.
        if (!hydrationId.empty())
            nodes_.back().attrs["data-flux-id"] = hydrationId;
        return handle;
    }

    void setStyle(DomNodeHandle node, const char *prop, const std::string &value) override
    {
        if (auto *n = get(node)) n->styles[prop] = value;
    }

    void setAttr(DomNodeHandle node, const char *name, const std::string &value) override
    {
        if (auto *n = get(node)) n->attrs[name] = value;
    }

    void setText(DomNodeHandle node, const std::string &utf8Text) override
    {
        if (auto *n = get(node)) n->text = utf8Text;
    }

    void appendChild(DomNodeHandle parent, DomNodeHandle child) override
    {
        auto *p = get(parent);
        if (!p) return;
        // Same idempotency contract as the live adapter — a single-shot
        // SSR render only ever appends each child once anyway (no repaint
        // loop calling this repeatedly), but keep the check for parity.
        for (auto c : p->children)
            if (c == child) return;
        p->children.push_back(child);
    }

    void removeNode(DomNodeHandle) override
    {
        // No-op: a single-shot SSR render never tears down mid-render —
        // there is no live document to detach FROM. Safe to leave the
        // node in the pool; it simply won't be reachable from root if it
        // was never (re-)appended after removal, matching real DOM
        // semantics closely enough for this one-pass use case.
    }

    void setRoot(DomNodeHandle node) override { root_ = node; }

    // ── Real <input> support ─────────────────────────────────────────────
    void setInputValue(DomNodeHandle node, const std::string &value) override
    {
        // For a STATIC html string (no live element to assign .value to
        // later), the way to make an <input> show the right content on
        // first paint IS the value attribute — this is the one place
        // setAttr's semantics (persistent HTML attribute) are actually
        // the correct behavior, unlike the live adapter's comment
        // explicitly warning that attributes don't reflect live typing
        // (irrelevant here — nothing is live yet).
        if (auto *n = get(node)) n->attrs["value"] = value;
    }
    void focusNode(DomNodeHandle) override { /* nothing to focus in static HTML */ }
    void blurNode(DomNodeHandle) override { }
    void bindInputEvents(DomNodeHandle, Widget *) override
    {
        // Real event binding is meaningless server-side. Phase 5 is where
        // this adapter (and the live one) start writing matching
        // data-flux-id markers so the CLIENT can find and adopt this
        // exact node instead of creating a new one — deliberately not
        // done yet, tracked as a Phase 5 item.
    }

    // ── Serialization ─────────────────────────────────────────────────────
    std::string serialize() const
    {
        std::ostringstream out;
        if (root_ != kInvalidDomNode)
            writeNode(out, root_);
        return out.str();
    }

private:
    std::vector<PendingNode> nodes_;
    DomNodeHandle root_ = kInvalidDomNode;

    PendingNode *get(DomNodeHandle h)
    {
        if (h == kInvalidDomNode || h > nodes_.size())
            return nullptr;
        return &nodes_[h - 1];
    }
    const PendingNode *get(DomNodeHandle h) const
    {
        if (h == kInvalidDomNode || h > nodes_.size())
            return nullptr;
        return &nodes_[h - 1];
    }

    void writeNode(std::ostringstream &out, DomNodeHandle handle) const
    {
        const PendingNode *n = get(handle);
        if (!n) return;

        out << "<" << n->tag;
        if (!n->styles.empty())
        {
            out << " style=\"";
            for (auto &[k, v] : n->styles)
                out << k << ":" << escapeHtml(v) << ";";
            out << "\"";
        }
        for (auto &[k, v] : n->attrs)
            out << " " << k << "=\"" << escapeHtml(v) << "\"";

        if (n->isInput)
        {
            out << " />"; // <input>/<textarea> as self-closing is fine for
                          // a static, non-editable server render
            return;
        }

        out << ">";
        out << escapeHtml(n->text);
        for (auto child : n->children)
            writeNode(out, child);
        out << "</" << n->tag << ">";
    }
};

// ── Wiring into the active-adapter accessor ──────────────────────────────
//
// Unlike the live adapter (one page-lifetime singleton), SSR needs a FRESH
// instance PER REQUEST — a single long-running server thread renders many
// requests sequentially, and each one's HTML must be built from scratch,
// not accumulated onto the previous request's leftover node pool.
// ssr/main.cpp owns the instance's lifetime directly (stack-allocated per
// request) rather than this file holding a static singleton.

IDomAdapter *fluxSsrCreateDomAdapter()
{
    return new StringBuilderDomAdapter();
}

std::string fluxSsrSerializeDomAdapter(IDomAdapter *adapter)
{
    // Safe: the only instances ever passed here are ones this file
    // created via fluxSsrCreateDomAdapter() above.
    return static_cast<StringBuilderDomAdapter *>(adapter)->serialize();
}

void fluxSsrDestroyDomAdapter(IDomAdapter *adapter)
{
    delete static_cast<StringBuilderDomAdapter *>(adapter);
}

#endif // FLUX_SSR