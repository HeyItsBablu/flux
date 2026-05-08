#include "flux/flux.hpp"
#include "flux/flux_secure_storage.hpp"

class MyApp : public Widget {

  State<std::string> displayed{"(nothing stored yet)"};

  std::shared_ptr<flux::FluxSecureStorage> storage =
      std::make_shared<flux::FluxSecureStorage>();

public:
  WidgetPtr build() override {
    return Scaffold(
        AppBar("Secure Storage Demo"),
        Expanded(Center(
            Container(
                Column({
                    Text(displayed),
                    Button("Store Secret", [this] {
                        storage->write("my_secret", "hello_flux_123",
                            [this](bool ok, flux::SecureStorageError err) { 
                                displayed.set(ok ? "Stored!" : "Error: " + err.message);
                            });
                    }),
                    Button("Load Secret", [this] {
                        storage->read("my_secret",
                            [this](std::optional<std::string> val, flux::SecureStorageError err) {
                                displayed.set(val ? *val : "Not found");
                            });
                    }),
                })
                ->setSpacing(12)
                ->setCrossAxisAlignment(CrossAxisAlignment::Center))
            ->setWidth(300)
            ->setHeight(200)
            ->setBorderRadius(10))),
        nullptr,nullptr);
  }
};

WidgetPtr createApp(FluxUI *app) {
  return FluxApp("Secure Storage", std::make_shared<MyApp>(), AppTheme::light(),
                 false, 900, 700, false, false);
}