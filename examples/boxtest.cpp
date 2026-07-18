#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
  WidgetPtr build() override
  {
    // ------------------------------------------------------------------
    // Test 1: NESTED Box responsiveness.
    //
    // The outer Box and the inner Box both read viewport width via
    // ctx.fluxViewportWidth. The inner one's computeLayout() is called
    // several stack frames deep (outer->computeLayoutBlock_ or
    // computeLayoutFlex_ -> child->computeLayout -> inner Box's own
    // computeLayout -> resolveProps(ctx)). If ctx weren't being threaded
    // through by reference correctly at every level, the inner box would
    // desync from the outer one — e.g. outer says "Lg" while inner still
    // says "Base" because it read a stale/zeroed viewport width.
    //
    // Expect: label text on BOTH boxes always matches, at every width.
    // ------------------------------------------------------------------
    auto innerLabel = Text("inner: ?")->setPadding(6)->setId("innerLabel");

    auto inner =
        Box({innerLabel})
            ->setPadding(12)
            ->setWidthMode(SizeMode::Full)
            ->setBackgroundColor(Color::fromRGB(230, 230, 230))
            ->responsive(Breakpoint::Base, [](BoxProps &p)
                         { p.backgroundColor = Color::fromRGB(255, 210, 210); })
            ->responsive(Breakpoint::Sm, [](BoxProps &p)
                         { p.backgroundColor = Color::fromRGB(255, 235, 200); })
            ->responsive(Breakpoint::Md, [](BoxProps &p)
                         { p.backgroundColor = Color::fromRGB(255, 255, 200); })
            ->responsive(Breakpoint::Lg, [](BoxProps &p)
                         { p.backgroundColor = Color::fromRGB(210, 255, 210); })
            ->responsive(Breakpoint::Xl, [](BoxProps &p)
                         { p.backgroundColor = Color::fromRGB(210, 225, 255); });

    auto outerLabel = Text("outer: ?")->setPadding(6)->setId("outerLabel");

    auto nestedTest =
        Box({
                outerLabel,
                // A spacer Box between outer and inner, itself with no
                // responsive() overrides — makes sure ctx propagates
                // through a layer that doesn't care about it too.
                Box({inner})->setPadding(10)->setWidthMode(SizeMode::Full),
            })
            ->setDisplay(Display::Flex)
            ->setDirection(FlexDirection::Column)
            ->setPadding(16)
            ->setWidthMode(SizeMode::Full)
            ->setBackgroundColor(Color::fromRGB(250, 250, 250))
            ->responsive(Breakpoint::Base, [](BoxProps &) {})
            ->responsive(Breakpoint::Sm, [](BoxProps &) {})
            ->responsive(Breakpoint::Md, [](BoxProps &) {})
            ->responsive(Breakpoint::Lg, [](BoxProps &) {})
            ->responsive(Breakpoint::Xl, [](BoxProps &) {});

    // ------------------------------------------------------------------
    // Test 2: Grid mode responsive column count.
    //
    // Exercises computeLayoutGrid_ specifically (a different code path
    // than Flex/Block) reading resolved_.columns after resolveProps(ctx)
    // ran. Expect: 1 column below sm, 2 at sm, 3 at md, 4 at lg+.
    // ------------------------------------------------------------------
    std::vector<BoxChild> tiles;
    for (int i = 1; i <= 8; i++)
      tiles.push_back(
          Box({Text(std::to_string(i))->setPadding(8)})
              ->setBackgroundColor(Color::fromRGB(200, 210, 255))
              ->setBorderColor(Color::fromRGB(120, 130, 200))
              ->setBorderWidth(1));

    auto grid =
        Box(tiles)
            ->setDisplay(Display::Grid)
            ->setColumns({fr(1)})
            ->setColumnGap(6)
            ->setRowGap(6)
            ->setPadding(12)
            ->setBackgroundColor(Color::fromRGB(245, 245, 250))
            ->responsive(Breakpoint::Base, [](BoxProps &p)
                         { p.columns = {fr(1)}; })
            ->responsive(Breakpoint::Sm, [](BoxProps &p)
                         { p.columns = {fr(1), fr(1)}; })
            ->responsive(Breakpoint::Md, [](BoxProps &p)
                         { p.columns = {fr(1), fr(1), fr(1)}; })
            ->responsive(Breakpoint::Lg, [](BoxProps &p)
                         { p.columns = {fr(1), fr(1), fr(1), fr(1)}; });



    return Box({
                   Text("resize the window; all sections below should stay in sync")
                       ->setPadding(8),
                   nestedTest,
                   Text("grid: 1 col < sm, 2 at sm, 3 at md, 4 at lg+")
                       ->setPadding(4),
                   grid,

               })
        ->setDisplay(Display::Flex)
        ->setDirection(FlexDirection::Column)
        ->setGap(12)
        ->setPadding(16);
  }



private:
  int tickCount_ = 0;
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp().setTheme(AppTheme::light()).build(std::make_shared<MyApp>());
}