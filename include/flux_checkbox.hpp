#ifndef FLUX_CHECKBOX_HPP
#define FLUX_CHECKBOX_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <iostream>

template <typename T>
class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

class CheckBoxWidget : public Widget
{
public:
    bool checked = false;
    int boxSize = 16;

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (!text.empty())
        {
            measureText(hdc, fontCache);
            width = boxSize + 8 + width;
        }
        else
        {
            width = boxSize;
        }

        height = max(boxSize, height);

        width += paddingLeft + paddingRight;
        height += paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        int boxX = x + paddingLeft;
        int boxY = y + paddingTop + (height - paddingTop - paddingBottom - boxSize) / 2;

        HBRUSH boxBrush = CreateSolidBrush(
            checked ? RGB(76, 175, 80) : RGB(255, 255, 255));
        HPEN boxPen = CreatePen(PS_SOLID, 1,
                                checked ? RGB(56, 155, 60) : RGB(150, 150, 150));

        HPEN oldPen = (HPEN)SelectObject(hdc, boxPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, boxBrush);

        Rectangle(hdc, boxX, boxY, boxX + boxSize, boxY + boxSize);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(boxBrush);
        DeleteObject(boxPen);

        if (checked)
        {
            HPEN checkPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HPEN oldCheckPen = (HPEN)SelectObject(hdc, checkPen);

            int cx = boxX + 3;
            int cy = boxY + boxSize / 2;

            MoveToEx(hdc, cx, cy, nullptr);
            LineTo(hdc, cx + 4, cy + 4);
            LineTo(hdc, cx + 9, cy - 4);

            SelectObject(hdc, oldCheckPen);
            DeleteObject(checkPen);
        }

        if (!text.empty())
        {
            RECT textRect = {
                boxX + boxSize + 8,
                y + paddingTop,
                x + width - paddingRight,
                y + height - paddingBottom};

            SetTextColor(hdc, getCurrentTextColor());
            SetBkMode(hdc, TRANSPARENT);

            HFONT hFont = fontCache.getFont(fontSize, fontWeight);
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            DrawText(hdc, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
        }

        needsPaint = false;
    }

    bool handleMouseDown(int mx, int my) override
    {
        if (mx >= x && mx < x + width &&
            my >= y && my < y + height)
        {
            checked = !checked;

            if (onClick)
                onClick();

            return true;
        }
        return false;
    }

    WidgetPtr setInputValue(State<bool> &state)
    {
        checked = state.get();

        state.bindProperty(
            shared_from_this(),
            [](Widget *w, const bool &val)
            {
                auto *cb = static_cast<CheckBoxWidget *>(w);
                cb->checked = val;
            },
            false);

        onClick = [&state, this]()
        {
            state.set(checked);
        };

        return shared_from_this();
    }
};

// ----------------------------------------------------------------
// Factory Functions
// ----------------------------------------------------------------

using CheckBoxWidgetPtr = std::shared_ptr<CheckBoxWidget>;

inline CheckBoxWidgetPtr CheckBox(const std::string &label = "")
{
    auto w = std::make_shared<CheckBoxWidget>();
    w->text = label;
    w->textColor = RGB(30, 30, 30);
    w->paddingLeft = w->paddingRight = 4;
    w->paddingTop = w->paddingBottom = 4;
    return w;
}

#endif