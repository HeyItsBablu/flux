#ifndef FLUX_WIDGET_LIST_HPP
#define FLUX_WIDGET_LIST_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"

// ============================================================================
// CONCRETE WIDGET CLASSES
// ============================================================================

// --- Text Widget ---
class TextWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        measureText(hdc, fontCache);

        // Only add padding if we're auto-sizing
        if (autoWidth)
            width += paddingLeft + paddingRight;
        if (autoHeight)
            height += paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        if (hasBackground)
        {
            drawRoundedRectangle(hdc);
        }
        renderText(hdc, fontCache);
        needsPaint = false;
    }
};

// --- Button Widget ---
class ButtonWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        measureText(hdc, fontCache);

        // Only add padding if we're auto-sizing
        if (autoWidth)
            width += paddingLeft + paddingRight;
        if (autoHeight)
            height += paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        if (hasBackground)
        {
            drawRoundedRectangle(hdc);
        }
        renderText(hdc, fontCache, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        needsPaint = false;
    }
};

// --- Column Widget ---
class ColumnWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;

        int totalFlex = 0;
        int fixedHeight = 0;

        // First pass: compute fixed-size children
        for (auto &child : children)
        {
            if (child->isExpanded())
            {
                totalFlex += child->flex;
            }
            else
            {
                child->computeLayout(hdc, contentWidth, contentHeight, fontCache);
                fixedHeight += child->height;
            }
        }

        if (!children.empty())
        {
            fixedHeight += spacing * (children.size() - 1);
        }

        // Second pass: distribute remaining space to flex children
        int remainingHeight = contentHeight - fixedHeight;
        if (totalFlex > 0 && remainingHeight > 0)
        {
            for (auto &child : children)
            {
                if (child->isExpanded())
                {
                    int expandedHeight = (remainingHeight * child->flex) / totalFlex;
                    child->height = expandedHeight;
                    child->autoHeight = false;
                    child->computeLayout(hdc, contentWidth, expandedHeight, fontCache);
                }
            }
        }

        // Calculate final size
        int totalHeight = 0;
        int maxWidth = 0;

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];
            if (child->width > maxWidth)
                maxWidth = child->width;
            totalHeight += child->height;
            if (i < children.size() - 1)
                totalHeight += spacing;
        }

        if (autoWidth)
            width = maxWidth + paddingLeft + paddingRight;
        if (autoHeight)
            height = totalHeight + paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        int totalChildHeight = 0;
        for (auto &child : children)
        {
            totalChildHeight += child->height;
        }
        totalChildHeight += spacing * (children.empty() ? 0 : children.size() - 1);

        int currentY = contentY;

        if (mainAxisAlignment == MainAxisAlignment::Center)
        {
            currentY += (contentHeight - totalChildHeight) / 2;
        }
        else if (mainAxisAlignment == MainAxisAlignment::End)
        {
            currentY += contentHeight - totalChildHeight;
        }

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];
            int childX = contentX;

            if (crossAlignment == Alignment::Center)
            {
                childX = contentX + (contentWidth - child->width) / 2;
            }
            else if (crossAlignment == Alignment::End)
            {
                childX = contentX + contentWidth - child->width;
            }
            else if (crossAlignment == Alignment::Stretch)
            {
                child->width = contentWidth;
            }

            child->x = childX;
            child->y = currentY;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom);

            currentY += child->height + (i < children.size() - 1 ? spacing : 0);
        }
    }
};

// --- Row Widget ---
class RowWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;

        int totalFlex = 0;
        int fixedWidth = 0;

        // First pass: compute fixed-size children
        for (auto &child : children)
        {
            if (child->isExpanded())
            {
                totalFlex += child->flex;
            }
            else
            {
                child->computeLayout(hdc, contentWidth, contentHeight, fontCache);
                fixedWidth += child->width;
            }
        }

        if (!children.empty())
        {
            fixedWidth += spacing * (children.size() - 1);
        }

        int remainingWidth = contentWidth - fixedWidth;
        if (totalFlex > 0 && remainingWidth > 0)
        {
            for (auto &child : children)
            {
                if (child->isExpanded())
                {
                    int expandedWidth = (remainingWidth * child->flex) / totalFlex;
                    child->width = expandedWidth;
                    child->autoWidth = false;
                    child->computeLayout(hdc, expandedWidth, contentHeight, fontCache);
                }
            }
        }

        int totalWidth = 0;
        int maxHeight = 0;

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];
            totalWidth += child->width;
            if (child->height > maxHeight)
                maxHeight = child->height;
            if (i < children.size() - 1)
                totalWidth += spacing;
        }

        if (autoWidth)
            width = totalWidth + paddingLeft + paddingRight;
        if (autoHeight)
            height = maxHeight + paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        int totalChildWidth = 0;
        for (auto &child : children)
        {
            totalChildWidth += child->width;
        }
        totalChildWidth += spacing * (children.empty() ? 0 : children.size() - 1);

        int currentX = contentX;

        if (mainAxisAlignment == MainAxisAlignment::Center)
        {
            currentX += (contentWidth - totalChildWidth) / 2;
        }
        else if (mainAxisAlignment == MainAxisAlignment::End)
        {
            currentX += contentWidth - totalChildWidth;
        }

        for (size_t i = 0; i < children.size(); i++)
        {
            auto &child = children[i];
            int childY = contentY;

            if (crossAlignment == Alignment::Center)
            {
                childY = contentY + (contentHeight - child->height) / 2;
            }
            else if (crossAlignment == Alignment::End)
            {
                childY = contentY + contentHeight - child->height;
            }
            else if (crossAlignment == Alignment::Stretch)
            {
                child->height = contentHeight;
            }

            child->x = currentX;
            child->y = childY;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom);

            currentX += child->width + (i < children.size() - 1 ? spacing : 0);
        }
    }
};

// --- Container/Padding/Card Widgets ---
class ContainerWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;

        if (!children.empty())
        {
            children[0]->computeLayout(hdc, contentWidth, contentHeight, fontCache);
            if (autoWidth)
                width = children[0]->width + paddingLeft + paddingRight;
            if (autoHeight)
                height = children[0]->height + paddingTop + paddingBottom;
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
};

// --- Center Widget ---
class CenterWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;

        if (!children.empty())
        {
            children[0]->computeLayout(hdc, contentWidth, contentHeight, fontCache);
        }

        if (autoWidth)
            width = availableWidth;
        if (autoHeight)
            height = availableHeight;

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        if (!children.empty())
        {
            auto &child = children[0];

            // Center the child within the content area
            int childX = contentX + (contentWidth - child->width) / 2;
            int childY = contentY + (contentHeight - child->height) / 2;

            child->x = childX;
            child->y = childY;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom);
        }
    }
};

// --- SizedBox Widget ---
class SizedBoxWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (!children.empty())
        {
            children[0]->computeLayout(hdc,
                                       width - paddingLeft - paddingRight,
                                       height - paddingTop - paddingBottom,
                                       fontCache);
        }
        applyConstraints();
        needsLayout = false;
    }
};

// --- Expanded Widget ---
class ExpandedWidget : public Widget
{
public:
    bool isExpanded() const override { return true; }

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (!children.empty())
        {
            children[0]->computeLayout(hdc,
                                       width - paddingLeft - paddingRight,
                                       height - paddingTop - paddingBottom,
                                       fontCache);
            if (autoWidth)
                width = children[0]->width + paddingLeft + paddingRight;
            if (autoHeight)
                height = children[0]->height + paddingTop + paddingBottom;
        }
        applyConstraints();
        needsLayout = false;
    }
};

// --- Divider Widget ---
class DividerWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;
        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        if (hasBackground)
        {
            drawRoundedRectangle(hdc);
        }
        needsPaint = false;
    }
};

// --- Scaffold Widget ---
class ScaffoldWidget : public Widget
{
public:
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
};

// --- AppBar Widget ---
class AppBarWidget : public Widget
{
public:
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;

        if (!children.empty())
        {
            auto &title = children[0];
            title->computeLayout(hdc,
                                 width - paddingLeft - paddingRight,
                                 height - paddingTop - paddingBottom,
                                 fontCache);
        }

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        if (!children.empty())
        {
            auto &child = children[0];

            // Position child at content origin (no centering or offset)
            child->x = contentX;
            child->y = contentY;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom);
        }
    }
};

// ============================================================================
// WIDGET FACTORY FUNCTIONS
// ============================================================================

/**
 * @brief Create a container widget.
 * @param child Optional child widget
 * @return WidgetPtr Container widget
 */
inline WidgetPtr Container(WidgetPtr child = nullptr)
{
    auto w = std::make_shared<ContainerWidget>();
    if (child)
        w->addChild(child);
    return w;
}

/**
 * @brief Create a text widget with static text.
 * @param text Text to display
 * @return WidgetPtr Text widget
 */
inline WidgetPtr Text(const std::string &text)
{
    auto w = std::make_shared<TextWidget>();
    w->text = text;
    return w;
}

/**
 * @brief Create a text widget bound to a State (reactive).
 * @tparam T Type of the state value
 * @param state State object to bind to
 * @return WidgetPtr Text widget that auto-updates
 * 
 * The text widget will automatically update when the state changes.
 * Uses the optimized toString() method from State for performance.
 * 
 * @example
 * State<int> counter(0, &app);
 * auto label = Text(counter);  // Auto-updates when counter changes
 */
template <typename T>
inline WidgetPtr Text(State<T> &state)
{
    auto w = std::make_shared<TextWidget>();
    w->text = state.toString();  // Use cached toString()
    state.addObserver(w);        // Bind to state
    return w;
}

/**
 * @brief Create a button widget.
 * @param text Button label
 * @param onClick Click handler (optional)
 * @return WidgetPtr Button widget
 */
inline WidgetPtr Button(const std::string &text, ClickHandler onClick = nullptr)
{
    auto w = std::make_shared<ButtonWidget>();
    w->text = text;
    w->onClick = onClick;

    // Default button styling
    w->hasBackground = true;
    w->backgroundColor = RGB(76, 175, 80);
    w->textColor = RGB(255, 255, 255);
    w->paddingLeft = w->paddingRight = 20;
    w->paddingTop = w->paddingBottom = 10;
    w->borderRadius = 4;
    w->fontWeight = FontWeight::Bold;

    return w;
}

/**
 * @brief Create a row layout widget.
 * @tparam Widgets Variadic widget types
 * @param widgets Child widgets
 * @return WidgetPtr Row widget
 */
template <typename... Widgets>
WidgetPtr Row(Widgets... widgets)
{
    auto w = std::make_shared<RowWidget>();
    (w->addChild(widgets), ...);
    return w;
}

/**
 * @brief Create a column layout widget.
 * @tparam Widgets Variadic widget types
 * @param widgets Child widgets
 * @return WidgetPtr Column widget
 */
template <typename... Widgets>
WidgetPtr Column(Widgets... widgets)
{
    auto w = std::make_shared<ColumnWidget>();
    (w->addChild(widgets), ...);
    return w;
}

/**
 * @brief Create a padding wrapper widget.
 * @param padding Padding amount (all sides)
 * @param child Child widget
 * @return WidgetPtr Padding widget
 */
inline WidgetPtr Padding(int padding, WidgetPtr child)
{
    auto w = std::make_shared<ContainerWidget>();
    w->padding = padding;
    w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = padding;
    if (child)
        w->addChild(child);
    return w;
}

/**
 * @brief Create a center alignment widget.
 * @param child Child widget to center
 * @return WidgetPtr Center widget
 */
inline WidgetPtr Center(WidgetPtr child)
{
    auto w = std::make_shared<CenterWidget>();
    w->alignment = Alignment::Center;
    if (child)
        w->addChild(child);
    return w;
}

/**
 * @brief Create a fixed-size box widget.
 * @param width Fixed width
 * @param height Fixed height
 * @param child Optional child widget
 * @return WidgetPtr SizedBox widget
 */
inline WidgetPtr SizedBox(int width, int height, WidgetPtr child = nullptr)
{
    auto w = std::make_shared<SizedBoxWidget>();
    w->width = width;
    w->height = height;
    w->autoWidth = false;
    w->autoHeight = false;
    if (child)
        w->addChild(child);
    return w;
}

/**
 * @brief Create a card widget (styled container).
 * @param child Child widget
 * @return WidgetPtr Card widget
 */
inline WidgetPtr Card(WidgetPtr child)
{
    auto w = std::make_shared<ContainerWidget>();
    w->hasBackground = true;
    w->backgroundColor = RGB(255, 255, 255);
    w->hasBorder = true;
    w->borderColor = RGB(224, 224, 224);
    w->borderWidth = 1;
    w->borderRadius = 8;
    w->paddingLeft = w->paddingRight = w->paddingTop = w->paddingBottom = 16;
    if (child)
        w->addChild(child);
    return w;
}

/**
 * @brief Create a horizontal divider.
 * @return WidgetPtr Divider widget
 */
inline WidgetPtr Divider()
{
    auto w = std::make_shared<DividerWidget>();
    w->height = 1;
    w->autoHeight = false;
    w->hasBackground = true;
    w->backgroundColor = RGB(224, 224, 224);
    return w;
}

/**
 * @brief Create an expanded widget (takes remaining space).
 * @param child Child widget
 * @param flex Flex factor (default 1)
 * @return WidgetPtr Expanded widget
 */
inline WidgetPtr Expanded(WidgetPtr child, int flex = 1)
{
    auto w = std::make_shared<ExpandedWidget>();
    w->flex = flex;
    if (child)
        w->addChild(child);
    return w;
}

/**
 * @brief Create an app bar widget.
 * @param title App bar title text
 * @return WidgetPtr AppBar widget
 */
inline WidgetPtr AppBar(const std::string &title)
{
    auto w = std::make_shared<AppBarWidget>();

    w->hasBackground = true;
    w->backgroundColor = RGB(33, 150, 243);
    w->height = 56;
    w->autoHeight = false;

    auto titleWidget = Text(title)
                           ->setFontSize(20)
                           ->setFontWeight(FontWeight::Bold)
                           ->setTextColor(RGB(255, 255, 255))
                           ->setPadding(16);

    w->addChild(titleWidget);

    return w;
}

/**
 * @brief Create a scaffold widget (full app structure).
 * @param appBar Optional app bar
 * @param body Optional body content
 * @return WidgetPtr Scaffold widget
 */
inline WidgetPtr Scaffold(WidgetPtr appBar = nullptr, WidgetPtr body = nullptr)
{
    auto w = std::make_shared<ScaffoldWidget>();

    w->hasBackground = true;
    w->backgroundColor = RGB(250, 250, 250);

    auto column = std::make_shared<ColumnWidget>();
    column->setSpacing(0);

    if (appBar)
    {
        column->addChild(appBar);
    }

    if (body)
    {
        column->addChild(Expanded(body));
    }

    w->addChild(column);

    return w;
}

#endif // FLUX_WIDGETS_HPP