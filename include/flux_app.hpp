#ifndef FLUX_APP_HPP
#define FLUX_APP_HPP

#include "flux_widget_list.hpp"
#include <string>

// ============================================================================
// APP THEME - Similar to Flutter's ThemeData
// ============================================================================

struct AppTheme
{
    // Colors
    COLORREF primaryColor = RGB(33, 150, 243);      // Material Blue
    COLORREF accentColor = RGB(255, 193, 7);        // Material Amber
    COLORREF backgroundColor = RGB(250, 250, 250);  // Light Gray
    COLORREF surfaceColor = RGB(255, 255, 255);     // White
    COLORREF errorColor = RGB(244, 67, 54);         // Material Red
    COLORREF textColor = RGB(0, 0, 0);              // Black
    COLORREF secondaryTextColor = RGB(128, 128, 128); // Gray

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
    COLORREF appBarColor = RGB(33, 150, 243);
    COLORREF appBarTextColor = RGB(255, 255, 255);
    int appBarHeight = 56;

    // Cards
    COLORREF cardColor = RGB(255, 255, 255);
    COLORREF cardBorderColor = RGB(224, 224, 224);
    int cardBorderRadius = 8;
    int cardPadding = 16;

    // Buttons
    COLORREF buttonColor = RGB(33, 150, 243);
    COLORREF buttonTextColor = RGB(255, 255, 255);
    int buttonBorderRadius = 4;
    int buttonPaddingH = 20;
    int buttonPaddingV = 10;

    // Static themes
    static AppTheme light()
    {
        return AppTheme(); // Default is light theme
    }

    static AppTheme dark()
    {
        AppTheme theme;
        theme.primaryColor = RGB(33, 150, 243);
        theme.backgroundColor = RGB(18, 18, 18);
        theme.surfaceColor = RGB(30, 30, 30);
        theme.textColor = RGB(255, 255, 255);
        theme.secondaryTextColor = RGB(180, 180, 180);
        theme.cardColor = RGB(30, 30, 30);
        theme.cardBorderColor = RGB(60, 60, 60);
        return theme;
    }

    static AppTheme materialBlue()
    {
        AppTheme theme;
        theme.primaryColor = RGB(33, 150, 243);
        theme.accentColor = RGB(255, 193, 7);
        return theme;
    }

    static AppTheme materialRed()
    {
        AppTheme theme;
        theme.primaryColor = RGB(244, 67, 54);
        theme.accentColor = RGB(255, 235, 59);
        return theme;
    }

    static AppTheme materialGreen()
    {
        AppTheme theme;
        theme.primaryColor = RGB(76, 175, 80);
        theme.accentColor = RGB(255, 193, 7);
        return theme;
    }
};

// ============================================================================
// THEME PROVIDER - Global theme access
// ============================================================================

class ThemeProvider
{
private:
    static AppTheme currentTheme;

public:
    static void setTheme(const AppTheme &theme)
    {
        currentTheme = theme;
    }

    static AppTheme &getTheme()
    {
        return currentTheme;
    }
};

// Initialize static member
inline AppTheme ThemeProvider::currentTheme = AppTheme::light();

// ============================================================================
// THEMED WIDGET FACTORIES (Use theme colors automatically)
// ============================================================================

inline WidgetPtr ThemedAppBar(const std::string &title)
{
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

inline WidgetPtr ThemedButton(const std::string &text, ClickHandler onClick = nullptr)
{
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

inline WidgetPtr ThemedCard(WidgetPtr child)
{
    auto theme = ThemeProvider::getTheme();
    auto w = std::make_shared<ContainerWidget>();
    w->hasBackground = true;
    w->backgroundColor = theme.cardColor;
    w->hasBorder = true;
    w->borderColor = theme.cardBorderColor;
    w->borderWidth = 1;
    w->borderRadius = theme.cardBorderRadius;
    w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = theme.cardPadding;
    if (child)
        w->addChild(child);
    return w;
}

// ============================================================================
// FLUX APP WIDGET - Similar to MaterialApp
// ============================================================================

class FluxAppWidget : public Widget
{
public:
    std::string title;
    AppTheme theme;
    bool debugShowWidgetBounds = false;
    WidgetPtr home;

    FluxAppWidget(const std::string &appTitle, WidgetPtr homeWidget)
        : title(appTitle), home(homeWidget), theme(AppTheme::light())
    {
        // Set global theme
        ThemeProvider::setTheme(theme);

        // Add home as child
        if (home)
        {
            addChild(home);
        }
    }

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;
        if (autoHeight)
            height = availableHeight;

        if (!children.empty())
        {
            children[0]->computeLayout(hdc, width, height, fontCache);
        }

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
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

    void render(HDC hdc, FontCache &fontCache) override
    {
        // Apply background color from theme
        HBRUSH bgBrush = CreateSolidBrush(theme.backgroundColor);
        RECT bgRect = {x, y, x + width, y + height};
        FillRect(hdc, &bgRect, bgBrush);
        DeleteObject(bgBrush);

        // Render children
        if (!children.empty())
        {
            children[0]->render(hdc, fontCache);
        }

        // Debug: Draw widget bounds
        if (debugShowWidgetBounds)
        {
            drawDebugBounds(hdc);
        }

        needsPaint = false;
    }

private:
    void drawDebugBounds(HDC hdc)
    {
        // Draw red outline around all widgets for debugging
        drawWidgetBounds(hdc, this);
    }

    void drawWidgetBounds(HDC hdc, Widget *w)
    {
        if (!w)
            return;

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

        Rectangle(hdc, w->x, w->y, w->x + w->width, w->y + w->height);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        for (auto &child : w->children)
        {
            drawWidgetBounds(hdc, child.get());
        }
    }
};

// ============================================================================
// FLUX APP FACTORY FUNCTION
// ============================================================================

/**
 * @brief Create a FluxApp (similar to MaterialApp in Flutter)
 * 
 * @param title Window title
 * @param home Root widget
 * @param theme Optional theme (defaults to light theme)
 * @param debugShowWidgetBounds Show red outlines around widgets for debugging
 * 
 * @example
 * return FluxApp("My App", 
 *     Scaffold(
 *         ThemedAppBar("Home"),
 *         Center(Text("Hello World"))
 *     ),
 *     AppTheme::dark(),
 *     false  // debugShowWidgetBounds
 * );
 */
inline WidgetPtr FluxApp(
    const std::string &title,
    WidgetPtr home,
    const AppTheme &theme = AppTheme::light(),
    bool debugShowWidgetBounds = false)
{
    auto app = std::make_shared<FluxAppWidget>(title, home);
    app->theme = theme;
    app->debugShowWidgetBounds = debugShowWidgetBounds;

    // Set global theme
    ThemeProvider::setTheme(theme);

    return app;
}

#endif // FLUX_APP_HPP