#include "flux.hpp"

struct Product {
  std::string name;
  double price;
  bool inStock;
};

class TransformDemoComponent : public Component {
private:
  State<Product> product{Product{"Headphones", 49.99, true}, context};

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Text Transform Demo"),
        Center(Column(

                   Text(product, [](const Product &p) { return p.name; })
                       ->setFontSize(22)
                       ->setFontWeight(FontWeight::Bold),

                   Text(product,
                        [](const Product &p) {
                          return "$" + std::to_string(p.price).substr(0, 5);
                        })
                       ->setFontSize(18)
                       ->setTextColor(RGB(80, 160, 80)),

                   Text(product,
                        [](const Product &p) {
                          return p.inStock ? "In Stock" : "Out of Stock";
                        })
                       ->setFontSize(14)
                       ->setTextColor(RGB(130, 130, 130)),

                   // Toggle stock button
                   Button(Text("Toggle Stock"),
                          [this] {
                            auto p = product.get();
                            p.inStock = !p.inStock;
                            product.set(p);
                          }))
                   ->setSpacing(16)
                   ->setCrossAlignment(Alignment::Center)));
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Transform Demo", BuildComponent<TransformDemoComponent>(),
                 AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  FluxUI app(hInstance);
  app.build([&]() { return createApp(&app); });
  app.createWindow("FluxUI - Transform Demo", 400, 300);
  return app.run();
}