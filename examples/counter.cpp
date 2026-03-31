#include "flux/flux.hpp"
#include <windows.h>

// Custom reusable widget - Stat Card
WidgetPtr StatCard(const std::string &label, State<int> &value, Color color) {
  return Card(Column({Text(label)->setFontSize(14)->setTextColor(
                          Color::fromRGB(120, 120, 120)),

                      Text(value)
                          ->setFontSize(36)
                          ->setFontWeight(FontWeight::Bold)
                          ->setTextColor(color)})
                  ->setSpacing(8))
      ->setMinWidth(150)
      ->setMinHeight(100);
}

// Main app using the custom widget
WidgetPtr simpleApp(FluxUI *app) {
  static State<int> likes(42, app);
  static State<int> views(1500, app);

  return Scaffold(

      Center(
          Column({// Use custom StatCard widgets
                  Row({StatCard("Likes", likes, Color::fromRGB(244, 67, 54)),
                       StatCard("Views", views, Color::fromRGB(33, 150, 243))})
                      ->setSpacing(20),

                  SizedBox(0, 20),

                  // Control buttons
                  Row({Button("+ Like", [&]() { likes.set(likes.get() + 1); }),
                       Button("+ View", [&]() { views.set(views.get() + 1); })})
                      ->setSpacing(10)})
              ->setSpacing(0)));
}



WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "FluxUI - Paint",
        simpleApp,
        AppTheme::dark(),
        false,   // debugShowWidgetBounds
        900,     // width
        700,     // height
        false,   // maximize
        true     // fullscreen
    );
}