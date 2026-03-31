#ifndef FLUX_APP_HPP
#define FLUX_APP_HPP

#include "widgets/widgets.hpp"
#include <algorithm>
#include <string>

// ============================================================================
// APP THEME - Similar to Flutter's ThemeData
// ============================================================================

struct AppTheme {
  // Colors
  Color primaryColor = Color::fromRGB(33, 150, 243);        // Material Blue
  Color accentColor = Color::fromRGB(255, 193, 7);          // Material Amber
  Color backgroundColor = Color::fromRGB(250, 250, 250);    // Light Gray
  Color surfaceColor = Color::fromRGB(255, 255, 255);       // White
  Color errorColor = Color::fromRGB(244, 67, 54);           // Material Red
  Color textColor = Color::fromRGB(0, 0, 0);                // Black
  Color secondaryTextColor = Color::fromRGB(128, 128, 128); // Gray

  // Typography
  int titleFontSize = 20;
  int bodyFontSize = 14;
  int captionFontSize = 12;
  FontWeight titleFontWeight = FontWeight::Bold;
  FontWeight bodyFontWeight = FontWeight::Normal;

  // Spacing
  int defaultPadding = 16;
  int defaultSpacing = 8;
  int defaultBorderRadius = 8;

  // AppBar
  Color appBarColor = Color::fromRGB(33, 150, 243);
  Color appBarTextColor = Color::fromRGB(255, 255, 255);
  int appBarHeight = 56;

  // Cards
  Color cardColor = Color::fromRGB(255, 255, 255);
  Color cardBorderColor = Color::fromRGB(224, 224, 224);
  int cardBorderRadius = 8;
  int cardPadding = 16;

  // Buttons
  Color buttonColor = Color::fromRGB(33, 150, 243);
  Color buttonTextColor = Color::fromRGB(255, 255, 255);
  int buttonBorderRadius = 4;
  int buttonPaddingH = 20;
  int buttonPaddingV = 10;

  // Static themes
  static AppTheme light() { return AppTheme(); }

  static AppTheme dark() {
    AppTheme theme;
    theme.primaryColor = Color::fromRGB(33, 150, 243);
    theme.backgroundColor = Color::fromRGB(18, 18, 18);
    theme.surfaceColor = Color::fromRGB(30, 30, 30);
    theme.textColor = Color::fromRGB(255, 255, 255);
    theme.secondaryTextColor = Color::fromRGB(180, 180, 180);
    theme.cardColor = Color::fromRGB(30, 30, 30);
    theme.cardBorderColor = Color::fromRGB(60, 60, 60);
    return theme;
  }

  static AppTheme materialBlue() {
    AppTheme theme;
    theme.primaryColor = Color::fromRGB(33, 150, 243);
    theme.accentColor = Color::fromRGB(255, 193, 7);
    return theme;
  }

  static AppTheme materialRed() {
    AppTheme theme;
    theme.primaryColor = Color::fromRGB(244, 67, 54);
    theme.accentColor = Color::fromRGB(255, 235, 59);
    return theme;
  }

  static AppTheme materialGreen() {
    AppTheme theme;
    theme.primaryColor = Color::fromRGB(76, 175, 80);
    theme.accentColor = Color::fromRGB(255, 193, 7);
    return theme;
  }
};

// ============================================================================
// THEME PROVIDER - Global theme access
// ============================================================================

class ThemeProvider {
private:
  static AppTheme currentTheme;

public:
  static void setTheme(const AppTheme &theme) { currentTheme = theme; }
  static AppTheme &getTheme() { return currentTheme; }
};

inline AppTheme ThemeProvider::currentTheme = AppTheme::light();

// ============================================================================
// THEMED WIDGET FACTORIES
// ============================================================================

inline WidgetPtr ThemedAppBar(const std::string &title) {
  auto theme = ThemeProvider::getTheme();
  auto w = std::make_shared<AppBarWidget>();

  w->hasBackground = true;
  w->backgroundColor = theme.appBarColor;
  w->height = theme.appBarHeight;
  w->autoHeight = false;

  auto titleWidget = Text(title)
                         ->setFontSize(theme.titleFontSize)
                         ->setFontWeight(theme.titleFontWeight)
                         ->setTextColor(theme.appBarTextColor)
                         ->setPadding(theme.defaultPadding);

  w->addChild(titleWidget);
  return w;
}

inline WidgetPtr ThemedButton(const std::string &text,
                              ClickHandler onClick = nullptr) {
  auto theme = ThemeProvider::getTheme();
  auto w = std::make_shared<ButtonWidget>();
  w->text = text;
  w->onClick = onClick;

  w->hasBackground = true;
  w->backgroundColor = theme.buttonColor;
  w->textColor = theme.buttonTextColor;
  w->paddingLeft = w->paddingRight = theme.buttonPaddingH;
  w->paddingTop = w->paddingBottom = theme.buttonPaddingV;
  w->borderRadius = theme.buttonBorderRadius;
  w->fontWeight = FontWeight::Bold;

  return w;
}

inline WidgetPtr ThemedCard(WidgetPtr child) {
  auto theme = ThemeProvider::getTheme();
  auto w = std::make_shared<ContainerWidget>();
  w->hasBackground = true;
  w->backgroundColor = theme.cardColor;
  w->hasBorder = true;
  w->borderColor = theme.cardBorderColor;
  w->borderWidth = 1;
  w->borderRadius = theme.cardBorderRadius;
  w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom =
      theme.cardPadding;
  if (child)
    w->addChild(child);
  return w;
}

// ============================================================================
// OVERLAY ENTRY
// ============================================================================

// zIndex convention:
//   50  = Tooltip
//   100 = Dropdown list
//   150 = Context menu
//   200 = Dialog (backdrop + box)
//   300 = Dialog-internal dropdown / context menu

// ============================================================================
// FLUX APP WIDGET - Similar to MaterialApp
// ============================================================================

class FluxAppWidget : public Widget {
private:
  // Overlay stack is kept sorted ascending by zIndex at all times.
  // Render order:  front → back  (lowest zIndex first, highest last = on top)
  // Hit-test order: back → front (highest zIndex first = topmost widget wins)
  static FluxAppWidget *instance;

public:
  std::string title;
  AppTheme theme;
  bool debugShowWidgetBounds = false;
  WidgetPtr home;

  int windowWidth = 900;
  int windowHeight = 700;
  bool maximize = false;
  bool fullscreen = true;

  FluxAppWidget(const std::string &appTitle, WidgetPtr homeWidget)
      : title(appTitle), home(homeWidget), theme(AppTheme::light()) {
    ThemeProvider::setTheme(theme);
    instance = this;
    if (home) {
      addChild(home);
    }
  }

  // Get the global FluxAppWidget instance
  static FluxAppWidget *getInstance() { return instance; }

  // Set theme and trigger repaint
  void setTheme(const AppTheme &newTheme) {
    theme = newTheme;
    ThemeProvider::setTheme(newTheme);
    needsPaint = true;
  }

  // Toggle between light and dark
  void toggleTheme() {
    bool isDark = (theme.backgroundColor == AppTheme::dark().backgroundColor);
    setTheme(isDark ? AppTheme::light() : AppTheme::dark());
  }

  // ----------------------------------------------------------------
  // LAYOUT
  // ----------------------------------------------------------------

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;

    if (!children.empty()) {
      children[0]->computeLayout(
          ctx,
          BoxConstraints::tight(width - paddingLeft - paddingRight,
                                height - paddingTop - paddingBottom),
          fontCache);
    }

    applyConstraints();
    needsLayout = false;
  }
  void positionChildren(int contentX, int contentY, int /*contentWidth*/,
                        int /*contentHeight*/) override {
    if (!children.empty()) {
      auto &child = children[0];
      child->x = contentX;
      child->y = contentY;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  // ----------------------------------------------------------------
  // RENDER
  // ----------------------------------------------------------------

  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    Painter painter(ctx);

    // Background — was CreateSolidBrush / FillRect / DeleteObject
    painter.fillRect(x, y, width, height, theme.backgroundColor);

    if (!children.empty())
      children[0]->render(ctx, fontCache);

    if (debugShowWidgetBounds)
      drawDebugBounds(ctx);

    needsPaint = false;
  }

private:
  void drawDebugBounds(GraphicsContext &ctx) { drawWidgetBounds(ctx, this); }

  void drawWidgetBounds(GraphicsContext &ctx, Widget *w) {
    if (!w)
      return;

    Painter painter(ctx);

    // Outline only — no fill.
    // Was: CreatePen + SelectObject(NULL_BRUSH) + Rectangle + cleanup.
    // drawEllipse doesn't apply here; we need a rect outline.
    // Add Painter::drawRectOutline() for this — see note below.
    painter.drawRectOutline(w->x, w->y, w->width, w->height,
                            Color::fromRGB(255, 0, 0), 1);

    for (auto &child : w->children)
      drawWidgetBounds(ctx, child.get());
  }
};
inline FluxAppWidget *FluxAppWidget::instance = nullptr;

// ============================================================================
// FLUX APP FACTORY
// ============================================================================

inline WidgetPtr FluxApp(const std::string& title,
                         WidgetPtr           home,
                         const AppTheme&     theme                = AppTheme::light(),
                         bool                debugShowWidgetBounds = false,
                         int                 width                = 900,
                         int                 height               = 700,
                         bool                maximize             = false,
                         bool                fullscreen           = true)
{
    auto app = std::make_shared<FluxAppWidget>(title, home);
    app->theme                = theme;
    app->debugShowWidgetBounds = debugShowWidgetBounds;
    app->windowWidth          = width;
    app->windowHeight         = height;
    app->maximize             = maximize;
    app->fullscreen           = fullscreen;
    ThemeProvider::setTheme(theme);
    return app;
}

#endif // FLUX_APP_HPP