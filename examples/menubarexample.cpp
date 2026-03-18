#include "flux/flux.hpp"


class MenuBarExample : public Component {
  State<std::string> statusText;

public:
  MenuBarExample() : statusText("Ready.", context) {}

  WidgetPtr build() override {
    auto menuBar = MenuBar({

        MenuBarItem("File", {
            ContextMenuItem::Action("New",   [this]{ statusText.set("New file."); }),
            ContextMenuItem::Action("Open",  [this]{ statusText.set("Open file."); }),
            ContextMenuItem::Action("Save",  [this]{ statusText.set("Saved."); }),
            ContextMenuItem::Separator(),
            ContextMenuItem::Action("Exit",  []{ PostQuitMessage(0); }),
        }),

        MenuBarItem("Edit", {
            ContextMenuItem::Action("Cut",   [this]{ statusText.set("Cut."); }),
            ContextMenuItem::Action("Copy",  [this]{ statusText.set("Copied."); }),
            ContextMenuItem::Action("Paste", [this]{ statusText.set("Pasted."); }),
        }),

        MenuBarItem("View", {
            ContextMenuItem::Action("Zoom In",  [this]{ statusText.set("Zoom in."); }),
            ContextMenuItem::Action("Zoom Out", [this]{ statusText.set("Zoom out."); }),
            ContextMenuItem::Separator(),
            ContextMenuItem::Action("Full Screen", [this]{ statusText.set("Full screen."); }),
        }),

        MenuBarItem("Help", {
            ContextMenuItem::Action("About", [this]{ statusText.set("FluxUI MenuBar demo."); }),
        }),
    });

    return Scaffold(
        // ── AppBar with MenuBar embedded below the title ───────────────────
        Column({
            AppBar("MenuBar Demo"),
            menuBar,
        })
        ->setSpacing(0),

        // ── Body ──────────────────────────────────────────────────────────
        Center(
            Column({
                Text("Click a menu above.")
                    ->setFontSize(15)
                    ->setTextColor(RGB(100, 100, 100)),
                SizedBox(0, 16),
                Text(statusText)
                    ->setFontSize(20)
                    ->setFontWeight(FontWeight::Bold),
            })
            ->setSpacing(0)));
  }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() {
    return FluxApp("MenuBar Demo",
                   BuildComponent<MenuBarExample>(),
                   AppTheme::light());
  });
  app.createWindow("FluxUI - MenuBar Demo", 800, 500);
  return app.run();
}