#pragma once
#include "flux.hpp"

// =============================================================================
// OVERLAY TEST  —  Tooltip, Dropdown, ContextMenu, Dialog
// =============================================================================

class OverlayTestApp : public Component {

  State<std::string> dropdownVal;
  State<int>         dropdownIdx;
  State<std::string> log;
  State<std::string> dialogInput;
  State<std::string> dialogResult;

  void appendLog(const std::string &msg) {
    std::string cur = log.get();
    if (!cur.empty()) cur += "\n";
    cur += "> " + msg;
    int nl = 0;
    for (char c : cur) if (c == '\n') nl++;
    while (nl > 7) {
      auto pos = cur.find('\n');
      if (pos == std::string::npos) break;
      cur = cur.substr(pos + 1);
      nl--;
    }
    log.set(cur);
  }

public:
  OverlayTestApp()
    : dropdownVal("",        context),
      dropdownIdx(-1,        context),
      log("Interact with the widgets below...", context),
      dialogInput("",        context),
      dialogResult("(none)", context) {}

  WidgetPtr build() override {

    // ── palette ──────────────────────────────────────────────────────────────
    COLORREF kBg       = RGB(245, 246, 250);
    COLORREF kCard     = RGB(255, 255, 255);
    COLORREF kBorder   = RGB(218, 220, 232);
    COLORREF kAccent   = RGB( 79, 109, 245);
    COLORREF kAccentHv = RGB( 60,  88, 220);
    COLORREF kDanger   = RGB(220,  60,  60);
    COLORREF kGreen    = RGB( 40, 170, 100);
    COLORREF kText     = RGB( 30,  35,  50);
    COLORREF kMuted    = RGB(110, 118, 145);
    COLORREF kLogBg    = RGB( 22,  24,  35);
    COLORREF kLogText  = RGB(148, 226, 170);

    // ── shared helpers ────────────────────────────────────────────────────────
    auto sectionLabel = [&](const std::string &t) -> WidgetPtr {
      return Row({
        Text(t)->setFontSize(11)->setFontWeight(FontWeight::Bold)->setTextColor(kAccent),
        SizedBox(8, 0),
        Container(nullptr)->setHeight(1)->setFlex(1)->setBackgroundColor(kBorder)
      })->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center);
    };

    auto accentBtn = [&](const std::string &label, COLORREF bg,
                         std::function<void()> fn) -> WidgetPtr {
      return Button(label, fn)
        ->setBackgroundColor(bg)
        ->setHoverBackgroundColor(RGB(
            min(255, GetRValue(bg) + 20),
            min(255, GetGValue(bg) + 20),
            min(255, GetBValue(bg) + 20)))
        ->setTextColor(RGB(255, 255, 255))
        ->setBorderRadius(6)
        ->setPadding(12);
    };

    // =========================================================================
    // SECTION 1 — TOOLTIP
    // =========================================================================

    auto makeTooltipBtn = [&](const std::string &label,
                               const std::string &tip,
                               TooltipPosition    pos,
                               COLORREF           bg,
                               COLORREF           tipBg,
                               int                maxW = 200) -> WidgetPtr {
      auto btn = Button(label, [this, label]() { appendLog("Clicked: " + label); })
        ->setBackgroundColor(bg)
        ->setHoverBackgroundColor(RGB(
            min(255, GetRValue(bg) + 20),
            min(255, GetGValue(bg) + 20),
            min(255, GetBValue(bg) + 20)))
        ->setTextColor(RGB(255, 255, 255))
        ->setBorderRadius(6)
        ->setPadding(12);

      return Tooltip(btn, tip)
        ->setPosition(pos)
        ->setTooltipBackground(tipBg)
        ->setTooltipTextColor(RGB(255, 255, 255))
        ->setTooltipFontSize(12)
        ->setTooltipMaxWidth(maxW);
    };

    auto tooltipSection = Column({
      sectionLabel("1 - TOOLTIP  (hover each button)"),
      SizedBox(0, 10),
      Row({
        makeTooltipBtn("Above",
          "Tooltip appears ABOVE the button. Default position.",
          TooltipPosition::Above, kAccent, RGB(30, 30, 50)),
        makeTooltipBtn("Below",
          "Tooltip appears BELOW the button.",
          TooltipPosition::Below, RGB(100, 60, 200), RGB(50, 20, 80)),
        makeTooltipBtn("Auto",
          "Auto-positions based on available space. Prefers above if room exists.",
          TooltipPosition::Auto, kGreen, RGB(10, 50, 30)),
        makeTooltipBtn("Long tip (160px)",
          "This tooltip has a longer description that will word-wrap "
          "because setTooltipMaxWidth is set to 160px.",
          TooltipPosition::Above, RGB(180, 80, 30), RGB(60, 20, 0), 160)
      })->setSpacing(12)->setCrossAxisAlignment(CrossAxisAlignment::Center)
    })->setSpacing(0);

    // =========================================================================
    // SECTION 2 — DROPDOWN
    // =========================================================================

    const std::vector<std::string> countries = {
      "Nepal", "India", "Japan", "Germany", "Brazil",
      "Canada", "Australia", "South Korea", "France", "Egypt"
    };
    const std::vector<std::string> sizes = {
      "Extra Small (XS)", "Small (S)", "Medium (M)",
      "Large (L)", "Extra Large (XL)", "XXL"
    };

    auto dd1 = Dropdown(countries)
      ->setPlaceholder("Select a country...")
      ->setSelectedValue(dropdownVal)
      ->setMaxVisibleItems(5)
      ->setOnSelectionChanged([this](int i, const std::string &v) {
          appendLog("Dropdown 1 -> [" + std::to_string(i) + "] " + v);
        })
      ->setWidth(220);

    auto dd2 = Dropdown(sizes)
      ->setPlaceholder("Pick a size...")
      ->setSelectedIndex(dropdownIdx)
      ->setMaxVisibleItems(6)
      ->setOnSelectionChanged([this](int i, const std::string &v) {
          appendLog("Dropdown 2 -> [" + std::to_string(i) + "] " + v);
        })
      ->setWidth(200);

    auto dropdownSection = Column({
      sectionLabel("2 - DROPDOWN  (click, then arrow keys + Enter)"),
      SizedBox(0, 10),
      Row({
        Column({
          Text("Bound by string value")->setFontSize(11)->setTextColor(kMuted),
          SizedBox(0, 4),
          dd1,
          SizedBox(0, 4),
          Row({
            Text("Selected:")->setFontSize(10)->setTextColor(kMuted),
            Text(dropdownVal, [](const std::string &v) {
              return v.empty() ? "(none)" : v;
            })->setFontSize(10)->setTextColor(kText)
          })->setSpacing(5)
        })->setSpacing(0),

        SizedBox(24, 0),

        Column({
          Text("Bound by index")->setFontSize(11)->setTextColor(kMuted),
          SizedBox(0, 4),
          dd2,
          SizedBox(0, 4),
          Row({
            Text("Index:")->setFontSize(10)->setTextColor(kMuted),
            Text(dropdownIdx, [](int v) {
              return v < 0 ? "(none)" : std::to_string(v);
            })->setFontSize(10)->setTextColor(kText)
          })->setSpacing(5)
        })->setSpacing(0)
      })->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Start)
    })->setSpacing(0);

    // =========================================================================
    // SECTION 3 — CONTEXT MENU
    // =========================================================================

    auto makeCard = [&](const std::string &name,
                        const std::string &subtitle,
                        COLORREF           accent,
                        std::vector<ContextMenuItem> items) -> WidgetPtr {
      auto cardContent = Container(
        Column({
          Row({
            Container(nullptr)
              ->setWidth(4)->setHeight(40)
              ->setBackgroundColor(accent)->setBorderRadius(2),
            SizedBox(10, 0),
            Column({
              Text(name)->setFontSize(13)->setFontWeight(FontWeight::Bold)->setTextColor(kText),
              Text(subtitle)->setFontSize(10)->setTextColor(kMuted)
            })->setSpacing(2)
          })->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center),
          SizedBox(0, 8),
          Text("Right-click for options")->setFontSize(10)->setTextColor(kMuted)
        })->setSpacing(0)
      )->setWidth(190)
       ->setBackgroundColor(kCard)
       ->setBorderRadius(8)
       ->setBorderWidth(1)
       ->setBorderColor(kBorder)
       ->setPaddingAll(14, 12, 14, 12)
       ->setHoverBackgroundColor(RGB(248, 249, 255));

      return ContextMenu(cardContent, items);
    };

    auto cardSection = Column({
      sectionLabel("3 - CONTEXT MENU  (right-click any card)"),
      SizedBox(0, 10),
      Row({
        makeCard("Document.pdf", "Last edited 2 min ago", RGB(79, 109, 245), {
          ContextMenuItem("Open",      [this](){ appendLog("ContextMenu -> Open Document.pdf"); }),
          ContextMenuItem("Rename",    [this](){ appendLog("ContextMenu -> Rename Document.pdf"); }),
          ContextMenuItem("Copy path", [this](){ appendLog("ContextMenu -> Copy path"); }),
          ContextMenuItem::Separator(),
          ContextMenuItem("Delete",    [this](){ appendLog("ContextMenu -> Delete Document.pdf"); }),
        }),
        makeCard("Image.png", "3.2 MB  1920x1080", RGB(40, 170, 100), {
          ContextMenuItem("Preview",   [this](){ appendLog("ContextMenu -> Preview Image.png"); }),
          ContextMenuItem("Crop",      [this](){ appendLog("ContextMenu -> Crop Image.png"); }),
          ContextMenuItem("Rotate 90", [this](){ appendLog("ContextMenu -> Rotate Image.png"); }),
          ContextMenuItem::Separator(),
          ContextMenuItem("Download",  [this](){ appendLog("ContextMenu -> Download Image.png"); }),
          ContextMenuItem("Delete",    [this](){ appendLog("ContextMenu -> Delete Image.png"); }),
        }),
        makeCard("Archive.zip", "Locked  14 files", RGB(200, 120, 30), {
          ContextMenuItem("Extract here",    [this](){ appendLog("ContextMenu -> Extract Archive.zip"); }),
          ContextMenuItem("Change password", [this](){ appendLog("ContextMenu -> Change password"); }),
          ContextMenuItem::Separator(),
          ContextMenuItem("Inspect",         [this](){ appendLog("ContextMenu -> Inspect Archive.zip"); }),
          ContextMenuItem("Delete",          [this](){ appendLog("ContextMenu -> Delete Archive.zip"); }),
          ContextMenuItem::Separator(),
          ContextMenuItem("Upload (no permission)", nullptr, false),
        })
      })->setSpacing(14)->setCrossAxisAlignment(CrossAxisAlignment::Start)
    })->setSpacing(0);

    // =========================================================================
    // SECTION 4 — DIALOG
    // =========================================================================

    // Dialog A — simple confirm / cancel
    auto dlgA = Dialog();
    dlgA->setSize(360, 160)
        ->setCloseOnClickOutside(true)
        ->setOverlayAlpha(140)
        ->setOnClose([this](){ appendLog("Dialog A closed"); });

    dlgA->setContent(Column({
      Text("Confirm Action")
        ->setFontSize(16)->setFontWeight(FontWeight::Bold)->setTextColor(kText),
      SizedBox(0, 8),
      Text("Are you sure you want to proceed? This cannot be undone.")
        ->setFontSize(13)->setTextColor(kMuted),
      SizedBox(0, 20),
      Row({
        Button("Cancel", [dlgA](){ dlgA->close(); })
          ->setBackgroundColor(RGB(230, 232, 240))->setTextColor(kText)
          ->setBorderRadius(6)->setPadding(12),
        Button("Confirm", [this, dlgA](){
            appendLog("Dialog A -> Confirmed");
            dlgA->close();
          })
          ->setBackgroundColor(kAccent)->setHoverBackgroundColor(kAccentHv)
          ->setTextColor(RGB(255, 255, 255))->setBorderRadius(6)->setPadding(12)
      })->setSpacing(8)
        ->setCrossAxisAlignment(CrossAxisAlignment::Center)
        ->setMainAxisAlignment(MainAxisAlignment::End)
    })->setSpacing(0));

    // Dialog B — text input, cannot dismiss by clicking outside
    auto dlgB = Dialog();
    dlgB->setSize(400, 200)
        ->setCloseOnClickOutside(false)
        ->setOverlayAlpha(160)
        ->setOnClose([this](){ appendLog("Dialog B closed"); });

    dlgB->setContent(Column({
      Text("Enter your name")
        ->setFontSize(15)->setFontWeight(FontWeight::Bold)->setTextColor(kText),
      SizedBox(0, 10),
      TextInput("Type something here...")->setInputValue(dialogInput)->setWidth(352),
      SizedBox(0, 16),
      Row({
        Button("Cancel", [this, dlgB](){
            dialogInput.set("");
            appendLog("Dialog B -> Cancelled");
            dlgB->close();
          })
          ->setBackgroundColor(RGB(230, 232, 240))->setTextColor(kText)
          ->setBorderRadius(6)->setPadding(12),
        Button("Submit", [this, dlgB](){
            std::string val = dialogInput.get();
            if (val.empty()) val = "(empty)";
            dialogResult.set(val);
            appendLog("Dialog B -> Submitted: \"" + val + "\"");
            dialogInput.set("");
            dlgB->close();
          })
          ->setBackgroundColor(kGreen)->setHoverBackgroundColor(RGB(30, 150, 80))
          ->setTextColor(RGB(255, 255, 255))->setBorderRadius(6)->setPadding(12)
      })->setSpacing(8)
        ->setCrossAxisAlignment(CrossAxisAlignment::Center)
        ->setMainAxisAlignment(MainAxisAlignment::End)
    })->setSpacing(0));

    // Dialog C — danger, subtle overlay
    auto dlgC = Dialog();
    dlgC->setSize(340, 150)
        ->setCloseOnClickOutside(true)
        ->setOverlayAlpha(80)
        ->setOnClose([this](){ appendLog("Dialog C closed"); });

    dlgC->setContent(Column({
      Text("Delete everything?")
        ->setFontSize(15)->setFontWeight(FontWeight::Bold)->setTextColor(kDanger),
      SizedBox(0, 8),
      Text("This action is permanent and irreversible.")
        ->setFontSize(12)->setTextColor(kMuted),
      SizedBox(0, 18),
      Row({
        Button("Keep it", [dlgC](){ dlgC->close(); })
          ->setBackgroundColor(RGB(230, 232, 240))->setTextColor(kText)
          ->setBorderRadius(6)->setPadding(12),
        Button("Delete all", [this, dlgC](){
            appendLog("Dialog C -> Deleted all!");
            dlgC->close();
          })
          ->setBackgroundColor(kDanger)->setHoverBackgroundColor(RGB(180, 30, 30))
          ->setTextColor(RGB(255, 255, 255))->setBorderRadius(6)->setPadding(12)
      })->setSpacing(8)
        ->setCrossAxisAlignment(CrossAxisAlignment::Center)
        ->setMainAxisAlignment(MainAxisAlignment::End)
    })->setSpacing(0));

    auto dialogSection = Column({
      sectionLabel("4 - DIALOG  (click to open each modal variant)"),
      SizedBox(0, 10),
      Row({
        Column({
          accentBtn("Simple Confirm", kAccent, [dlgA](){ dlgA->open(); }),
          SizedBox(0, 4),
          Text("closeOnClickOutside=true  alpha=140")
            ->setFontSize(9)->setTextColor(kMuted)
        })->setSpacing(0),

        Column({
          accentBtn("With TextInput", kGreen, [dlgB](){ dlgB->open(); }),
          SizedBox(0, 4),
          Row({
            Text("Last:")->setFontSize(9)->setTextColor(kMuted),
            Text(dialogResult, [](const std::string &v){ return v; })
              ->setFontSize(9)->setTextColor(RGB(40, 170, 100))
          })->setSpacing(4)
        })->setSpacing(0),

        Column({
          accentBtn("Danger / Low Overlay", kDanger, [dlgC](){ dlgC->open(); }),
          SizedBox(0, 4),
          Text("closeOnClickOutside=true  alpha=80")
            ->setFontSize(9)->setTextColor(kMuted)
        })->setSpacing(0),

        // Dialogs live in the widget tree so the scaffold wires them.
        // They have no inline visual — rendering happens via popup window.
        dlgA, dlgB, dlgC
      })->setSpacing(20)->setCrossAxisAlignment(CrossAxisAlignment::Start)
    })->setSpacing(0);

    // =========================================================================
    // EVENT LOG
    // =========================================================================

    auto logPanel = Container(
      Column({
        Row({
          Text("EVENT LOG")
            ->setFontSize(9)->setFontWeight(FontWeight::Bold)
            ->setTextColor(RGB(80, 200, 120)),
          Expanded(
            Button("Clear", [this](){ log.set("Log cleared."); })
              ->setBackgroundColor(RGB(40, 42, 55))
              ->setHoverBackgroundColor(RGB(55, 58, 72))
              ->setTextColor(RGB(150, 155, 180))
              ->setBorderRadius(4)
              ->setPaddingAll(8, 2, 8, 2)
          )
        })->setSpacing(8)->setCrossAxisAlignment(CrossAxisAlignment::Center),
        SizedBox(0, 6),
        Text(log, [](const std::string &s){ return s; })
          ->setFontSize(11)->setTextColor(kLogText)
      })->setSpacing(0)
    )->setBackgroundColor(kLogBg)->setBorderRadius(8)->setPaddingAll(14, 12, 14, 12);

    // =========================================================================
    // ROOT
    // =========================================================================

    return Scaffold(
      AppBar("Overlay Test  -  Tooltip  Dropdown  ContextMenu  Dialog"),
      Container(
        Column({
          tooltipSection,
          Divider(),
          dropdownSection,
          Divider(),
          cardSection,
          Divider(),
          dialogSection,
          Divider(),
          logPanel
        })->setSpacing(20)
      )->setPaddingAll(24, 20, 24, 20)->setBackgroundColor(kBg)
    );
  }
};

// =============================================================================
// ENTRY POINT
// =============================================================================

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
  FluxUI app(hInst);
  app.build([&]() {
    return FluxApp("Overlay Test", BuildComponent<OverlayTestApp>(),
                   AppTheme::light());
  });
  app.createWindow("Overlay Test", 1000, 820);
  return app.run();
}