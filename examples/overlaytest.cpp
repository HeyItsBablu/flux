#include "flux/flux.hpp"

class OverlayTestApp : public Widget
{

  State<int> counter{0};
  State<int> dropdownIndex{-1};
  State<std::string> lastAction{"(none)"};
  State<bool> dialogOpen{false};

  std::shared_ptr<DialogWidget> confirmDialog;

public:
  WidgetPtr build() override
  {

    // ── Dialog ───────────────────────────────────────────────────────────
    // Built fresh on every build() so the pointer is never stale.
    confirmDialog = Dialog(
                        Column({
                                   Text("Dialog is open!")
                                       ->setFontSize(18)
                                       ->setFontWeight(FontWeight::Bold),
                                   Text("This modal blocks all input beneath it."),
                                   Text("Click Confirm / Cancel, or press Escape."),
                                   Row({
                                           Button("Confirm", [this]
                                                  {
            std::cout<<"Confirm clicked" << std::endl;
            lastAction.set("Dialog: Confirmed");
            dialogOpen.set(false);
            confirmDialog->close(); }),
                                           Button("Cancel", [this]
                                                  {
            lastAction.set("Dialog: Cancelled");
            dialogOpen.set(false);
            confirmDialog->close(); }),
                                       })
                                       ->setSpacing(12)
                                       ->setCrossAxisAlignment(CrossAxisAlignment::Center),
                               })
                            ->setSpacing(10)
                            ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                        ->setSize(420, 220)
                        ->setCloseOnClickOutside(true)
                        ->setOnClose([this]
                                     {
        dialogOpen.set(false);
        lastAction.set("Dialog: closed"); });

    // ── Dropdown ─────────────────────────────────────────────────────────
    auto dropdown = Dropdown({"Apple", "Banana", "Cherry",
                              "Date", "Elderberry", "Fig",
                              "Grape", "Honeydew"})
                        ->setSelectedIndex(dropdownIndex)
                        ->setPlaceholder("Pick a fruit...")
                        ->setMaxVisibleItems(5)
                        ->setOnSelectionChanged([this](int idx, const std::string &val)
                                                {
          dropdownIndex.set(idx);
          lastAction.set("Dropdown: " + val); })
                        ->setWidth(260);

    // ── Context menu ─────────────────────────────────────────────────────
    auto ctxAnchor = Container(
                         Text("Right-click me")
                             ->setTextColor(Color::fromRGB(255, 255, 255))
                             ->setFontSize(14))
                         ->setWidth(160)
                         ->setHeight(38)
                         ->setBorderRadius(6)
                         ->setBackgroundColor(Color::fromRGB(33, 150, 243))
                         ->setPadding(12);

    auto ctxMenu = ContextMenu(ctxAnchor, {
                                              ContextMenuItem::Action("Copy", [this]
                                                                      { lastAction.set("ContextMenu: Copy"); }),
                                              ContextMenuItem::Action("Paste", [this]
                                                                      { lastAction.set("ContextMenu: Paste"); }),
                                              ContextMenuItem::Separator(),
                                              ContextMenuItem::Action("Delete", [this]
                                                                      { lastAction.set("ContextMenu: Delete"); }),
                                              ContextMenuItem::Action("Disabled action", nullptr, /*enabled=*/false),
                                          });

    // ── Tooltip ──────────────────────────────────────────────────────────
    auto tooltipBtn = Button("Hover me for tooltip", nullptr);
    auto tip = Tooltip(tooltipBtn,
                       "Fixed: close ordering collapses two repaints into one");

    // ── Thin rule helper (replaces Divider / SizedBox) ────────────────────
    auto rule = []() -> WidgetPtr
    {
      return Container(nullptr)
          ->setHeight(1)
          ->setBackgroundColor(Color::fromRGB(220, 220, 220));
    };

    // ── Main layout ───────────────────────────────────────────────────────
    return Scaffold(
        AppBar("Overlay System Test"),
        Center(
            Container(
                Column({

                           // ── Counter ───────────────────────────────────────────────
                           Text("Normal Widget Tree")
                               ->setFontSize(15)
                               ->setFontWeight(FontWeight::Bold),

                           Row({
                                   Text(counter)->setFontSize(18),
                                   Button("Increment", [this]
                                          {
                counter++;
                lastAction.set("Counter: " + counter.toString()); }),

                                   Button("Reset state", [this]
                                          {
                counter.set(0);
                dropdownIndex.set(-1);
                lastAction.set("State reset"); }),
                               })
                               ->setSpacing(10)
                               ->setCrossAxisAlignment(CrossAxisAlignment::Center),

                           rule(),

                           // ── Dropdown ──────────────────────────────────────────────
                           Text("Dropdown  (scroll & keyboard nav)")
                               ->setFontSize(15)
                               ->setFontWeight(FontWeight::Bold),

                           dropdown,

                           rule(),

                           // ── Context menu ──────────────────────────────────────────
                           Text("Context Menu  (right-click the blue box)")
                               ->setFontSize(15)
                               ->setFontWeight(FontWeight::Bold),

                           ctxMenu,

                           rule(),

                           // ── Tooltip ───────────────────────────────────────────────
                           Text("Tooltip  (hover the button)")
                               ->setFontSize(15)
                               ->setFontWeight(FontWeight::Bold),

                           tip,

                           rule(),

                           // ── Dialog ────────────────────────────────────────────────
                           Text("Dialog  (modal, Escape closes, keyboard-routed)")
                               ->setFontSize(15)
                               ->setFontWeight(FontWeight::Bold),

                           Button("Open Dialog", [this]
                                  {
              if (!dialogOpen.get()) {
                dialogOpen.set(true);
                confirmDialog->open();
                lastAction.set("Dialog: opened");
              } }),

                           rule(),

                           // ── Status ────────────────────────────────────────────────
                           Row({
                                   Text("Last action: ")
                                       ->setTextColor(Color::fromRGB(120, 120, 120))
                                       ->setFontSize(13),
                                   Text(lastAction)
                                       ->setFontSize(13)
                                       ->setFontWeight(FontWeight::Bold),
                               })
                               ->setSpacing(4)
                               ->setCrossAxisAlignment(CrossAxisAlignment::Center),

                       })
                    ->setSpacing(12)
                    ->setCrossAxisAlignment(CrossAxisAlignment::Start))
                ->setWidth(520)
                ->setBorderRadius(10)
                ->setPadding(28)
                ->setBackgroundColor(Color::fromRGB(255, 255, 255))),
        nullptr, nullptr);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp(
      "FluxUI — Overlay System Test",
      std::make_shared<OverlayTestApp>(),
      AppTheme::light(),
      false,
      720,
      700,
      true,
      false);
}