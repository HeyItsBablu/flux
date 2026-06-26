#ifndef FLUX_APP_HPP
#define FLUX_APP_HPP

#include "widgets/widgets.hpp"
#include <algorithm>
#include <string>

// ============================================================================
// APP THEME
// ============================================================================

struct AppTheme
{
  Color primaryColor = Color::fromRGB(33, 150, 243);
  Color accentColor = Color::fromRGB(255, 193, 7);
  Color backgroundColor = Color::fromRGB(250, 250, 250);
  Color surfaceColor = Color::fromRGB(255, 255, 255);
  Color errorColor = Color::fromRGB(244, 67, 54);
  Color textColor = Color::fromRGB(0, 0, 0);
  Color secondaryTextColor = Color::fromRGB(128, 128, 128);

  int titleFontSize = 20;
  int bodyFontSize = 14;
  int captionFontSize = 12;
  FontWeight titleFontWeight = FontWeight::Bold;
  FontWeight bodyFontWeight = FontWeight::Normal;

  int defaultPadding = 16;
  int defaultSpacing = 8;
  int defaultBorderRadius = 8;

  Color appBarColor = Color::fromRGB(33, 150, 243);
  Color appBarTextColor = Color::fromRGB(255, 255, 255);
  int appBarHeight = 56;

  Color cardColor = Color::fromRGB(255, 255, 255);
  Color cardBorderColor = Color::fromRGB(224, 224, 224);
  int cardBorderRadius = 8;
  int cardPadding = 16;

  Color buttonColor = Color::fromRGB(33, 150, 243);
  Color buttonTextColor = Color::fromRGB(255, 255, 255);
  int buttonBorderRadius = 4;
  int buttonPaddingH = 20;
  int buttonPaddingV = 10;

  static AppTheme light() { return AppTheme(); }

  static AppTheme dark()
  {
    AppTheme t;
    t.primaryColor = Color::fromRGB(33, 150, 243);
    t.backgroundColor = Color::fromRGB(18, 18, 18);
    t.surfaceColor = Color::fromRGB(30, 30, 30);
    t.textColor = Color::fromRGB(255, 255, 255);
    t.secondaryTextColor = Color::fromRGB(180, 180, 180);
    t.cardColor = Color::fromRGB(30, 30, 30);
    t.cardBorderColor = Color::fromRGB(60, 60, 60);
    return t;
  }

  static AppTheme materialBlue()
  {
    AppTheme t;
    t.primaryColor = Color::fromRGB(33, 150, 243);
    t.accentColor = Color::fromRGB(255, 193, 7);
    return t;
  }
  static AppTheme materialRed()
  {
    AppTheme t;
    t.primaryColor = Color::fromRGB(244, 67, 54);
    t.accentColor = Color::fromRGB(255, 235, 59);
    return t;
  }
  static AppTheme materialGreen()
  {
    AppTheme t;
    t.primaryColor = Color::fromRGB(76, 175, 80);
    t.accentColor = Color::fromRGB(255, 193, 7);
    return t;
  }
};

class ThemeProvider
{
public:
  static void bind(AppTheme *theme) { current_ = theme; }
  static void unbind(AppTheme *theme)
  {
    if (current_ == theme)
      current_ = nullptr;
  }

  static AppTheme &getTheme()
  {
    if (current_)
      return *current_;
    static AppTheme sDefault;
    return sDefault;
  }

  static void setTheme(const AppTheme &t)
  {
    if (current_)
      *current_ = t;
  }

private:
  static AppTheme *current_;
};

inline AppTheme *ThemeProvider::current_ = nullptr;

// ============================================================================
// THEMED WIDGET FACTORIES
// ============================================================================

inline WidgetPtr ThemedCard(WidgetPtr child)
{
  const AppTheme &theme = ThemeProvider::getTheme();
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
// FLUX APP WIDGET
// ============================================================================

class FluxAppWidget : public Widget
{
public:
  std::string title;
  AppTheme theme;
  bool debugShowWidgetBounds = false;
  WidgetPtr home;
  int windowWidth = 900;
  int windowHeight = 700;
  bool maximize = false;
  bool fullscreen = true;

  static std::shared_ptr<FluxAppWidget> getInstance()
  {
    return instance_.lock();
  }

  FluxAppWidget(const std::string &appTitle, WidgetPtr homeWidget)
      : title(appTitle), theme(AppTheme::light()), home(homeWidget)
  {
    assert(instance_.expired() &&
           "FluxAppWidget: second instance created while first is still alive. "
           "Only one FluxAppWidget per process is supported.");

    ThemeProvider::bind(&theme);

    if (home)
      addChild(home);
  }

  ~FluxAppWidget()
  {

    ThemeProvider::unbind(&theme);
  }

  void registerInstance(std::shared_ptr<FluxAppWidget> self)
  {
    instance_ = self;
  }

  void setTheme(const AppTheme &newTheme)
  {

    theme = newTheme;
    needsPaint = true;
  }

  void toggleTheme()
  {
    bool isDark = (theme.backgroundColor == AppTheme::dark().backgroundColor);
    setTheme(isDark ? AppTheme::light() : AppTheme::dark());
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    if (autoWidth)
      width = constraints.maxWidth;
    if (autoHeight)
      height = constraints.maxHeight;

    if (!children.empty())
      children[0]->computeLayout(
          ctx, BoxConstraints::tight(width, height), fontCache);

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY,
                        int /*contentWidth*/, int /*contentHeight*/) override
  {
    if (!children.empty())
    {
      auto &child = children[0];
      child->x = contentX;
      child->y = contentY;
      child->positionChildren(
          child->x + child->paddingLeft,
          child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  // ── Render ────────────────────────────────────────────────────────────────

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    Painter painter(ctx);
    painter.fillRect(x, y, width, height, theme.backgroundColor);

    if (!children.empty())
      children[0]->render(ctx, fontCache);

    if (debugShowWidgetBounds)
      drawDebugBounds(ctx);

    needsPaint = false;
  }

private:
  static std::weak_ptr<FluxAppWidget> instance_;

  void drawDebugBounds(GraphicsContext &ctx) { drawWidgetBounds(ctx, this); }

  void drawWidgetBounds(GraphicsContext &ctx, Widget *w)
  {
    if (!w)
      return;
    Painter(ctx).drawRectOutline(w->x, w->y, w->width, w->height,
                                 Color::fromRGB(255, 0, 0), 1);
    for (auto &child : w->children)
      drawWidgetBounds(ctx, child.get());
  }
};

inline std::weak_ptr<FluxAppWidget> FluxAppWidget::instance_;

// ============================================================================
// FLUX APP FACTORY
// ============================================================================



// ============================================================================
// FLUX APP FACTORY
// ============================================================================

class FluxAppBuilder
{
public:
  explicit FluxAppBuilder(std::string title)
  {
    cfg_.title = std::move(title);
  }

  FluxAppBuilder &setTheme(const AppTheme &theme)
  {
    cfg_.theme = theme;
    return *this;
  }

  FluxAppBuilder &setSize(int width, int height)
  {
    cfg_.width = width;
    cfg_.height = height;
    return *this;
  }

  FluxAppBuilder &setFullscreenMode(bool fullscreen = true)
  {
    cfg_.fullscreen = fullscreen;
    return *this;
  }

  FluxAppBuilder &setMaximized(bool maximize = true)
  {
    cfg_.maximize = maximize;
    return *this;
  }

  FluxAppBuilder &setDebugWidgetBounds(bool debug = true)
  {
    cfg_.debugWidgetBounds = debug;
    return *this;
  }

  WidgetPtr build(WidgetPtr home)
  {
    auto app = std::make_shared<FluxAppWidget>(cfg_.title, home);
    app->registerInstance(app);
    app->setTheme(cfg_.theme);
    app->debugShowWidgetBounds = cfg_.debugWidgetBounds;
    app->windowWidth = cfg_.width;
    app->windowHeight = cfg_.height;
    app->maximize = cfg_.maximize;
    app->fullscreen = cfg_.fullscreen;
    return app;
  }

private:
  struct Config
  {
    std::string title = "FluxUI App";
    AppTheme theme = AppTheme::light();
    int width = 900;
    int height = 700;
    bool maximize = false;
    bool fullscreen = false;
    bool debugWidgetBounds = false;
  } cfg_;
};

inline FluxAppBuilder FluxApp(std::string title)
{
  return FluxAppBuilder(std::move(title));
}

#endif // FLUX_APP_HPP