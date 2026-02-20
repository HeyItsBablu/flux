#include "flux.hpp"

struct Product {
  std::string name;
  double price;
  bool inStock;
};

class TransformDemoComponent : public Component {
private:
  State<Product> product{Product{"Headphones", 49.99, true}, context};
  State<std::string> selectedSize{"", context};

  DialogWidgetPtr confirmDialog;
  ContextMenuWidgetPtr cardMenu;

public:
  WidgetPtr build() override {

    // --- Dialog ---
    confirmDialog = Dialog(
      Column(
        Text("Are you sure?"),
        Text("This will update the product."),
        Button(Text("Close"), [this] { confirmDialog->close(); })
      )
    );
    confirmDialog->setSize(320, 180);

    // --- Context Menu ---
    auto menuAnchor = Button(Text("Right-click me"));
    cardMenu = ContextMenu(menuAnchor, {
      {"Edit Product",   [this] { confirmDialog->open(); }},
      {"Toggle Stock",   [this] {
        auto p = product.get();
        p.inStock = !p.inStock;
        product.set(p);
      }},
      ContextMenuItem::Separator(),
      {"Disabled Option", [] {}, false}
    });

    // --- Dropdown ---
    auto sizeDropdown = Dropdown({"Small", "Medium", "Large", "XL"})
      ->setPlaceholder("Pick a size")
      ->setSelectedValue(selectedSize);

    return Scaffold(
      AppBar("Overlay Widgets Demo"),
      Center(
        Column(
          // Tooltip example
          Tooltip(
            Button(Text("Hover for info"), [] {}),
            "This button does something cool!"
          ),

          // Context menu example
          cardMenu,

          // Dropdown example
          sizeDropdown,

          // Dialog trigger
          Button(Text("Open Dialog"), [this] { confirmDialog->open(); }),

          // Dialog itself (zero-size, renders via overlay)
          confirmDialog
        )
        ->setSpacing(20)
        ->setCrossAlignment(Alignment::Center)
      )
    );
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Overlay Demo", BuildComponent<TransformDemoComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  AllocConsole();
  FILE *fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Overlay Demo", 1000, 700);
  return app.run();
}