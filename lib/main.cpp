#include "flux/flux.hpp"

class MyApp : public Widget
{
public:
  WidgetPtr build() override
  {
    return Box({
                   Text("Box + Image test")
                       ->setFontSize(20)
                       ->setFontWeight(FontWeight::Bold)
                       ->setPadding(8),

                   _sectionLabel("1. Responsive image grid (resize window: 1/2/3/4 cols)"),
                   _imageGrid(),

                   _sectionLabel("2. Absolute-positioned badge overlay on an image"),
                   _badgedImage(),

                   _sectionLabel("3. Flex row cards (image + text, Cover fit, radius)"),
                   _cardRow(),

                   _sectionLabel("4. Scrollable Block-mode image feed"),
                   _imageFeed(),
               })
        ->setDisplay(Display::Flex)
        ->setDirection(FlexDirection::Column)
        ->setGap(20)
        ->setPadding(16)
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full)
        ->setScrollable(true);
  }

private:
  static WidgetPtr _sectionLabel(const std::string &s)
  {
    return Text(s)
        ->setFontSize(14)
        ->setFontWeight(FontWeight::Bold)
        ->setTextColor(Color::fromRGB(80, 80, 80))
        ->setPadding(4);
  }

  // ------------------------------------------------------------------
  // 1. Box in Grid mode holding images, with responsive() changing
  //    BOTH p.columns and p.rows together — Grid's implicit row
  //    auto-sizing (autoTrack()) measures row height from child
  //    content, which doesn't play well with a Full-height child in
  //    a non-scrollable grid. Giving explicit px(120) row tracks per
  //    breakpoint sidesteps that entirely — same as CSS's
  //    grid-auto-rows: 120px instead of leaving rows as `auto`.
  // ------------------------------------------------------------------
  static WidgetPtr _imageGrid()
  {
    std::vector<BoxChild> tiles;
    for (int i = 1; i <= 8; i++)
    {
      tiles.push_back(
          Box({
                  Image("https://picsum.photos/seed/grid" + std::to_string(i) + "/300/300")
                      ->setFit(ImageFit::Cover)
                      ->setBorderRadius(10),
              })
              ->setWidthMode(SizeMode::Full)
              ->setHeightMode(SizeMode::Full)); // fills whatever row height Grid assigns
    }

    constexpr int kTileCount = 8;
    constexpr int kRowHeight = 120;

    auto rowsFor = [](int cols) -> std::vector<TrackDef>
    {
      int rows = (kTileCount + cols - 1) / cols; // ceil division
      return repeat(rows, px(kRowHeight));
    };

    return Box(tiles)
        ->setDisplay(Display::Grid)
        ->setColumnGap(10)
        ->setRowGap(10)
        ->setPadding(12)
        ->setBackgroundColor(Color::fromRGB(245, 245, 248))
        ->setBorderRadius(12)
        ->responsive(Breakpoint::Base, [rowsFor](BoxProps &p)
                     { p.columns = {fr(1)}; p.rows = rowsFor(1); })
        ->responsive(Breakpoint::Sm, [rowsFor](BoxProps &p)
                     { p.columns = {fr(1), fr(1)}; p.rows = rowsFor(2); })
        ->responsive(Breakpoint::Md, [rowsFor](BoxProps &p)
                     { p.columns = {fr(1), fr(1), fr(1)}; p.rows = rowsFor(3); })
        ->responsive(Breakpoint::Lg, [rowsFor](BoxProps &p)
                     { p.columns = {fr(1), fr(1), fr(1), fr(1)}; p.rows = rowsFor(4); });
  }

  // ------------------------------------------------------------------
  // 2. Cover image as a Box's only flow child, with a "NEW" badge as
  //    a Position::Absolute sibling — exercises layoutAbsoluteChildren()
  //    resolving against the Box's own just-finalized content box,
  //    independent of which display mode placed the flow children.
  // ------------------------------------------------------------------
  static WidgetPtr _badgedImage()
  {
    auto badge =
        Box({
                Text("NEW")
                    ->setFontSize(11)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(Color::fromRGB(255, 255, 255)),
            })
            ->setPaddingHV(8, 4)
            ->setBackgroundColor(Color::fromRGB(220, 50, 50))
            ->setBorderRadius(6)
            ->setPositionMode(Position::Absolute)
            ->setTopPx(10)
            ->setRightPx(10)
            ->setZIndexVal(1);

    return Box({
                   Image("https://picsum.photos/seed/badge/600/240")
                       ->setFit(ImageFit::Cover)
                       ->setBorderRadius(12),
                   badge,
               })
        ->setHeightMode(SizeMode::Fixed)
        ->setHeight(180)
        ->setWidthMode(SizeMode::Full);
  }

  // ------------------------------------------------------------------
  // 3. Flex-mode Box rows: thumbnail Box + text column, each row itself
  //    a Box in Flex/Row mode nested inside an outer Box in Flex/Column
  //    mode — nested display-mode dispatch through the same computeLayout
  //    override.
  // ------------------------------------------------------------------
  static WidgetPtr _cardRow()
  {
    auto makeCard = [](const std::string &seed, const std::string &title,
                       Color tint) -> WidgetPtr
    {
      return Box({
                     Box({
                             Image("https://picsum.photos/seed/" + seed + "/200/200")
                                 ->setFit(ImageFit::Cover)
                                 ->setBorderRadius(8)
                                 ->setTintColor(tint),
                         })
                         ->setWidthMode(SizeMode::Fixed)
                         ->setWidth(90)
                         ->setHeightMode(SizeMode::Fixed)
                         ->setHeight(90),

                     Box({
                             Text(title)
                                 ->setFontSize(15)
                                 ->setFontWeight(FontWeight::Bold),
                             Text("Cover fit, tinted overlay")
                                 ->setFontSize(12)
                                 ->setTextColor(Color::fromRGB(120, 120, 120)),
                         })
                         ->setDisplay(Display::Flex)
                         ->setDirection(FlexDirection::Column)
                         ->setGap(4)
                         ->setWidthMode(SizeMode::Full)
                         ->setAlignItems(AlignItems::Start),
                 })
          ->setDisplay(Display::Flex)
          ->setDirection(FlexDirection::Row)
          ->setGap(12)
          ->setPadding(10)
          ->setBackgroundColor(Color::fromRGB(250, 250, 250))
          ->setBorderColor(Color::fromRGB(230, 230, 230))
          ->setBorderWidth(1)
          ->setBorderRadius(10)
          ->setWidthMode(SizeMode::Full)
          ->setAlignItems(AlignItems::Center);
    };

    return Box({
                   makeCard("card1", "Sunset Ridge", Color::fromRGBA(255, 140, 0, 40)),
                   makeCard("card2", "Ocean Drive", Color::fromRGBA(0, 100, 200, 40)),
                   makeCard("card3", "Forest Path", Color::fromRGBA(20, 140, 60, 40)),
               })
        ->setDisplay(Display::Flex)
        ->setDirection(FlexDirection::Column)
        ->setGap(10)
        ->setWidthMode(SizeMode::Full);
  }

  // ------------------------------------------------------------------
  // 4. Block-mode Box (default display), scrollable, stacking full-width
  //    images top-to-bottom — exercises computeLayoutBlock_'s vertical
  //    scroll + shrink-to-fit width path with real image children
  //    instead of Text/synthetic widgets.
  // ------------------------------------------------------------------
  static WidgetPtr _imageFeed()
  {
    std::vector<BoxChild> items;
    for (int i = 1; i <= 5; i++)
    {
      items.push_back(
          Box({
                  Image("https://picsum.photos/seed/feed" + std::to_string(i) + "/800/300")
                      ->setFit(ImageFit::Cover)
                      ->setBorderRadius(10)
                      ->setHeight(140),
                  Text("Feed item " + std::to_string(i))
                      ->setFontSize(13)
                      ->setPadding(6),
              })
              ->setDisplay(Display::Flex)
              ->setDirection(FlexDirection::Column)
              ->setWidthMode(SizeMode::Full));
    }

    return Box(items)
        ->setDisplay(Display::Block)
        ->setGap(14)
        ->setPadding(12)
        ->setHeightMode(SizeMode::Fixed)
        ->setHeight(360)
        ->setWidthMode(SizeMode::Full)
        ->setScrollable(true)
        ->setBackgroundColor(Color::fromRGB(250, 250, 252))
        ->setBorderColor(Color::fromRGB(230, 230, 230))
        ->setBorderWidth(1)
        ->setBorderRadius(10);
  }
};

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp().setTheme(AppTheme::light()).build(std::make_shared<MyApp>());
}