#ifndef FLUX_OVERLAY_HOST_HPP
#define FLUX_OVERLAY_HOST_HPP

// ============================================================================
// OVERLAY HOST MIXIN
//
// Any widget that participates in the overlay system (Dropdown, Tooltip,
// ContextMenu, Dialog, or any future overlay widget) should inherit from
// this interface IN ADDITION to Widget:
//
//   class MyOverlayWidget : public Widget, public OverlayHost { ... };
//
// FluxUI::wireScaffoldToWidgets does a single dynamic_cast<OverlayHost*>
// check — no registration, no switch, no touching flux_core_impl.hpp when
// adding new overlay widget types.
// ============================================================================

// Forward declaration — ScaffoldWidget is defined in flux_structure.hpp.
// We only need the pointer here so a forward decl is sufficient.
class ScaffoldWidget;

class OverlayHost {
public:
    virtual ~OverlayHost() = default;

    // Called by FluxUI::wireScaffoldToWidgets after every rebuild().
    // Implementations must store the pointer and use it to call
    // scaffold->addOverlay / removeOverlay.
    // 's' may be nullptr if the widget is being detached from a scaffold-less
    // tree (e.g. a standalone test harness) — implementations must handle that.
    virtual void setScaffold(ScaffoldWidget *s) = 0;
};

#endif // FLUX_OVERLAY_HOST_HPP