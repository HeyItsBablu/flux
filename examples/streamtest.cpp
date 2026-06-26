#include "flux/flux.hpp"

class MyApp : public Widget
{

public:
    WidgetPtr build() override
    {
        auto header = Flex({Text("My App")->setFontSize(20)})
                          ->setAlignItems(AlignItems::Center)
                          ->setJustifyContent(JustifyContent::Center)
                          ->setHeight(56)
                          ->setHeightMode(SizeMode::Fixed)
                          ->setWidthMode(SizeMode::Full);

        auto body = Flex({StreamBuilder(
                             "wss://echo.websocket.org",
                             [](const StreamSnapshot<std::string> &snap) -> WidgetPtr
                             {
                                 std::cout << "[StreamBuilder] state=";
                                 switch (snap.state)
                                 {
                                 case StreamState::None:
                                     std::cout << "None";
                                     break;
                                 case StreamState::Connecting:
                                     std::cout << "Connecting";
                                     break;
                                 case StreamState::Active:
                                     std::cout << "Active";
                                     break;
                                 case StreamState::Done:
                                     std::cout << "Done";
                                     break;
                                 case StreamState::Error:
                                     std::cout << "Error: " << snap.error;
                                     break;
                                 }
                                 std::cout << std::endl;

                                 if (snap.isConnecting())
                                     return Center(Text("Connecting..."));
                                 if (snap.hasError())
                                     return Center(Text("Error: " + snap.error));
                                 if (snap.isDone())
                                     return Center(Text("Connection closed."));
                                 if (!snap.hasData())
                                     return Center(Text("Connected. Waiting..."));

                                 return Center(Text("Received: " + snap.data));
                             })})
                        ->setAlignItems(AlignItems::Center)
                        ->setJustifyContent(JustifyContent::Center)
                        ->setAlignContent(AlignContent::Center)
                        ->setDirection(FlexDirection::Column)
                        ->setGap(8)
                        ->setWidthMode(SizeMode::Full)
                        ->setHeightMode(SizeMode::Full);

        auto footer = Flex({Text("v1.0.0")->setFontSize(12)})
                          ->setAlignItems(AlignItems::Center)
                          ->setJustifyContent(JustifyContent::Center)
                          ->setHeight(32)
                          ->setHeightMode(SizeMode::Fixed)
                          ->setWidthMode(SizeMode::Full);

        return Page()
            ->setHeader(header)
            ->setBody(body)
            ->setFooter(footer)
            ->setHeaderBackgroundColor(Color::fromRGB(240, 240, 245))
            ->setHeaderElevation(4)
            ->setBodyBackgroundColor(Color::fromRGB(255, 180, 180))
            ->setFooterBackgroundColor(Color::fromRGB(240, 240, 245));
    }
};

// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("Wrap App")
        .setTheme(AppTheme::light())
        .setFullscreenMode(true)
        .build(std::make_shared<MyApp>());
}