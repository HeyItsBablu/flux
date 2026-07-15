#include "flux/flux.hpp"
#include "flux/flux_secure_storage.hpp"

class MyApp : public Widget
{

    State<std::string> displayed{"(nothing stored yet)"};

    std::shared_ptr<flux::FluxSecureStorage> storage =
        std::make_shared<flux::FluxSecureStorage>();

public:
    WidgetPtr build() override
    {
        return

            Box({
                    Text(displayed),
                    Button("Store Secret", [this]
                           { storage->write("my_secret", "hello_flux_123",
                                            [this](bool ok, flux::SecureStorageError err)
                                            {
                                                displayed.set(ok ? "Stored!" : "Error: " + err.message);
                                            }); }),
                    Button("Load Secret", [this]
                           { storage->read("my_secret",
                                           [this](std::optional<std::string> val, flux::SecureStorageError err)
                                           {
                                               displayed.set(val ? *val : "Not found");
                                           }); }),
                })
                ->setDisplay(Display::Flex)
                ->setGap(12);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp()
        .setTheme(AppTheme::light())
        .build(std::make_shared<MyApp>());
}