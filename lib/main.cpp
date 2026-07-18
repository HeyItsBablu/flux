#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
  WidgetPtr build() override
  {
    // Only visible below the "md" breakpoint — CSS equivalent: `block md:hidden`
    auto mobileMenuButton =
        Box({Text("☰ Menu")->setPadding(8)})
            ->setBackgroundColor(Color::fromRGB(230, 230, 250))
            ->setPadding(8)
            ->hideAbove(Breakpoint::Md);

    // Only visible at "md" and up — CSS equivalent: `hidden md:block`
    auto sidebar =
        Box({
                Text("Sidebar")->setPadding(8),
                Text("Link 1")->setPadding(8),
                Text("Link 2")->setPadding(8),
            })
            ->setDisplay(Display::Flex)
            ->setDirection(FlexDirection::Column)
            ->setWidth(200)
            ->setBackgroundColor(Color::fromRGB(240, 240, 240))
            ->hideBelow(Breakpoint::Md);

    // Manually toggled — not breakpoint-driven at all
    auto banner =
        Box({Text("Heads up: maintenance tonight")->setPadding(8)})
            ->setBackgroundColor(Color::fromRGB(255, 245, 200))
            ->setId("banner")
            ->setHidden(!showBanner_);

    return Box({
                   mobileMenuButton,
                   Box({
                           sidebar,
                           Text("Main content area")->setPadding(16),
                       })
                       ->setDisplay(Display::Flex)
                       ->setDirection(FlexDirection::Row)
                       ->setGap(12),
                   banner,
               })
        ->setDisplay(Display::Flex)
        ->setDirection(FlexDirection::Column)
        ->setGap(12)
        ->setPadding(16);
  }

private:
  bool showBanner_ = true;
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp().setTheme(AppTheme::light()).build(std::make_shared<MyApp>());
}