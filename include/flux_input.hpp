#ifndef FLUX_INPUT_HPP
#define FLUX_INPUT_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include <iostream>

template <typename T>
class State;

class Widget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

class TextInputWidget : public Widget
{
public:
    std::string inputValue;
    std::string placeholder;
    int cursorPos = 0;
    bool cursorVisible = true;
    UINT cursorTimerId = 1;
    int scrollOffset = 0;

    COLORREF focusedBorderColor = RGB(33, 150, 243);
    COLORREF unfocusedBorderColor = RGB(180, 180, 180);
    COLORREF placeholderColor = RGB(180, 180, 180);
    COLORREF inputTextColor = RGB(30, 30, 30);

    TextInputWidget()
    {
        isFocusable = true;
        hasBorder = true;
        hasBackground = true;
        backgroundColor = RGB(255, 255, 255);
        borderColor = unfocusedBorderColor;
        borderWidth = 1;
        borderRadius = 4;
        paddingLeft = paddingRight = 10;
        paddingTop = paddingBottom = 8;
        height = 36;
        autoHeight = false;
    }

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;

        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
        drawRoundedRectangle(hdc);

        HFONT hFont = fontCache.getFont(fontSize, fontWeight);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        int textX = x + paddingLeft;
        int textY = y + paddingTop;
        int textW = width - paddingLeft - paddingRight;
        int textH = height - paddingTop - paddingBottom;

        RECT clipRect = {x + paddingLeft, y + paddingTop,
                         x + width - paddingRight, y + height - paddingBottom};

        HRGN clipRgn = CreateRectRgn(clipRect.left, clipRect.top,
                                     clipRect.right, clipRect.bottom);
        SelectClipRgn(hdc, clipRgn);

        SetBkMode(hdc, TRANSPARENT);

        if (inputValue.empty() && !placeholder.empty())
        {
            SetTextColor(hdc, placeholderColor);
            RECT pr = clipRect;
            DrawText(hdc, placeholder.c_str(), -1, &pr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        else
        {
            SetTextColor(hdc, inputTextColor);
            RECT tr = clipRect;
            tr.left -= scrollOffset;
            DrawText(hdc, inputValue.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }

        if (isFocused && cursorVisible)
        {
            SIZE textSize = {0};
            if (cursorPos > 0)
            {
                GetTextExtentPoint32(hdc, inputValue.c_str(), cursorPos, &textSize);
            }

            int cursorX = textX + textSize.cx - scrollOffset;
            int cursorY1 = y + paddingTop + 2;
            int cursorY2 = y + height - paddingBottom - 2;

            HPEN cursorPen = CreatePen(PS_SOLID, 1, RGB(30, 30, 30));
            HPEN oldPen = (HPEN)SelectObject(hdc, cursorPen);

            MoveToEx(hdc, cursorX, cursorY1, nullptr);
            LineTo(hdc, cursorX, cursorY2);

            SelectObject(hdc, oldPen);
            DeleteObject(cursorPen);
        }

        SelectClipRgn(hdc, nullptr);
        DeleteObject(clipRgn);

        SelectObject(hdc, hOldFont);
        needsPaint = false;
    }

    bool handleFocus(bool focused) override
    {
        isFocused = focused;

        if (focused)
        {
            HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
            SetTimer(hwnd, cursorTimerId, 530, nullptr);
            cursorVisible = true;
        }
        else
        {
            HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
            KillTimer(hwnd, cursorTimerId);
            cursorVisible = false;
        }

        markNeedsPaint();
        return true;
    }

    bool handleTimer(UINT timerId) override
    {
        if (timerId == cursorTimerId)
        {
            cursorVisible = !cursorVisible;
            return true;
        }
        return false;
    }

    bool handleMouseDown(int mx, int my) override
    {
        if (mx >= x && mx < x + width &&
            my >= y && my < y + height)
        {
            cursorPos = getCursorPosFromX(mx - x - paddingLeft + scrollOffset);
            return true;
        }
        return false;
    }

    bool handleChar(wchar_t ch) override
    {
        if (ch < 32)
            return false;

        std::string charStr(1, (char)ch);
        inputValue.insert(cursorPos, charStr);
        cursorPos++;
        cursorVisible = true;

        updateScroll();
        notifyStateBinding();
        return true;
    }

    bool handleKeyDown(int keyCode) override
    {
        switch (keyCode)
        {
        case VK_BACK:
            if (cursorPos > 0)
            {
                inputValue.erase(cursorPos - 1, 1);
                cursorPos--;
                cursorVisible = true;
                updateScroll();
                notifyStateBinding();
                return true;
            }
            break;

        case VK_DELETE:
            if (cursorPos < (int)inputValue.size())
            {
                inputValue.erase(cursorPos, 1);
                cursorVisible = true;
                updateScroll();
                notifyStateBinding();
                return true;
            }
            break;

        case VK_LEFT:
            if (cursorPos > 0)
            {
                cursorPos--;
                cursorVisible = true;
                updateScroll();
                return true;
            }
            break;

        case VK_RIGHT:
            if (cursorPos < (int)inputValue.size())
            {
                cursorPos++;
                cursorVisible = true;
                updateScroll();
                return true;
            }
            break;

        case VK_HOME:
            cursorPos = 0;
            scrollOffset = 0;
            return true;

        case VK_END:
            cursorPos = (int)inputValue.size();
            updateScroll();
            return true;
        }
        return false;
    }

    std::shared_ptr<TextInputWidget> setInputValue(State<std::string> &state)
    {
        inputValue = state.get();
        cursorPos = (int)inputValue.size();
        scrollOffset = 0;

        state.bindProperty(
            shared_from_this(),
            [](Widget *w, const std::string &val)
            {
                auto *input = static_cast<TextInputWidget *>(w);
                input->inputValue = val;
                input->cursorPos = (int)val.size();
            },
            false);

        boundStringState = &state;

        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setPlaceholder(const std::string &ph)
    {
        placeholder = ph;
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

private:
    State<std::string> *boundStringState = nullptr;

    void notifyStateBinding()
    {
        if (boundStringState)
            boundStringState->set(inputValue);
    }

    int getCursorPosFromX(int pixelX)
    {
        if (inputValue.empty())
            return 0;

        HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
        HDC hdc = GetDC(hwnd);
        FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();

        HFONT hFont = fc.getFont(fontSize, fontWeight);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        int bestPos = 0;
        int bestDist = abs(pixelX);

        for (int i = 1; i <= (int)inputValue.size(); i++)
        {
            SIZE sz;
            GetTextExtentPoint32(hdc, inputValue.c_str(), i, &sz);
            int dist = abs(sz.cx - pixelX);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestPos = i;
            }
        }

        SelectObject(hdc, hOldFont);
        ReleaseDC(hwnd, hdc);
        return bestPos;
    }

    void updateScroll()
    {
        if (inputValue.empty())
        {
            scrollOffset = 0;
            return;
        }

        HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
        HDC hdc = GetDC(hwnd);
        FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();

        HFONT hFont = fc.getFont(fontSize, fontWeight);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        SIZE sz = {0};
        if (cursorPos > 0)
            GetTextExtentPoint32(hdc, inputValue.c_str(), cursorPos, &sz);

        SelectObject(hdc, hOldFont);
        ReleaseDC(hwnd, hdc);

        int textAreaWidth = width - paddingLeft - paddingRight;
        int cursorX = sz.cx - scrollOffset;

        if (cursorX < 10)
            scrollOffset = max(0, sz.cx - 10);
        else if (cursorX > textAreaWidth - 10)
            scrollOffset = sz.cx - textAreaWidth + 10;
    }
};

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

class SliderWidget : public Widget
{
public:
    double value = 0.0;
    double minValue = 0.0;
    double maxValue = 100.0;
    double step = 1.0;

    int trackHeight = 4;
    int thumbRadius = 10;

    COLORREF trackColor = RGB(200, 200, 200);
    COLORREF trackFillColor = RGB(33, 150, 243);
    COLORREF thumbColor = RGB(33, 150, 243);
    COLORREF thumbHoverColor = RGB(25, 118, 210);
    COLORREF thumbDragColor = RGB(13, 71, 161);

    bool isDragging = false;
    bool isThumbHovered = false;

    std::function<void(double)> onValueChanged;

    SliderWidget()
    {
        height = 40;
        autoHeight = false;
        paddingLeft = paddingRight = thumbRadius;
        paddingTop = paddingBottom = 10;
    }

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;

        applyConstraints();
        needsLayout = false;
    }

    void render(HDC hdc, FontCache &fontCache) override
    {
        int trackY = y + height / 2;
        int trackLeft = x + paddingLeft;
        int trackRight = x + width - paddingRight;
        int trackWidth = trackRight - trackLeft;

        double normalizedValue = (value - minValue) / (maxValue - minValue);
        int thumbX = trackLeft + (int)(normalizedValue * trackWidth);

        HBRUSH trackBrush = CreateSolidBrush(trackColor);
        RECT trackRect = {
            trackLeft,
            trackY - trackHeight / 2,
            trackRight,
            trackY + trackHeight / 2};
        FillRect(hdc, &trackRect, trackBrush);
        DeleteObject(trackBrush);

        HBRUSH fillBrush = CreateSolidBrush(trackFillColor);
        RECT fillRect = {
            trackLeft,
            trackY - trackHeight / 2,
            thumbX,
            trackY + trackHeight / 2};
        FillRect(hdc, &fillRect, fillBrush);
        DeleteObject(fillBrush);

        COLORREF currentThumbColor = thumbColor;
        if (isDragging)
            currentThumbColor = thumbDragColor;
        else if (isThumbHovered)
            currentThumbColor = thumbHoverColor;

        HBRUSH thumbBrush = CreateSolidBrush(currentThumbColor);
        HPEN thumbPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));

        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, thumbBrush);
        HPEN oldPen = (HPEN)SelectObject(hdc, thumbPen);

        Ellipse(hdc,
                thumbX - thumbRadius,
                trackY - thumbRadius,
                thumbX + thumbRadius,
                trackY + thumbRadius);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(thumbBrush);
        DeleteObject(thumbPen);

        needsPaint = false;
    }

    bool handleMouseDown(int mx, int my) override
    {
        if (mx >= x && mx < x + width &&
            my >= y && my < y + height)
        {
            isDragging = true;

            HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
            SetCapture(hwnd);

            updateValueFromMouseX(mx);
            return true;
        }
        return false;
    }

    bool handleMouseUp(int mx, int my) override
    {
        if (isDragging)
        {
            isDragging = false;
            ReleaseCapture();
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override
    {
        if (isDragging)
        {
            updateValueFromMouseX(mx);
            return true;
        }

        int trackY = y + height / 2;
        int trackLeft = x + paddingLeft;
        int trackRight = x + width - paddingRight;
        int trackWidth = trackRight - trackLeft;

        double normalizedValue = (value - minValue) / (maxValue - minValue);
        int thumbX = trackLeft + (int)(normalizedValue * trackWidth);

        bool nowHovered = (mx >= thumbX - thumbRadius && mx <= thumbX + thumbRadius &&
                           my >= trackY - thumbRadius && my <= trackY + thumbRadius);

        if (nowHovered != isThumbHovered)
        {
            isThumbHovered = nowHovered;
            markNeedsPaint();
            return true;
        }

        return false;
    }

    bool handleMouseLeave() override
    {
        if (isThumbHovered)
        {
            isThumbHovered = false;
            markNeedsPaint();
            return true;
        }
        return false;
    }

    bool handleKeyDown(int keyCode) override
    {
        double oldValue = value;

        switch (keyCode)
        {
        case VK_LEFT:
        case VK_DOWN:
            value -= step;
            break;

        case VK_RIGHT:
        case VK_UP:
            value += step;
            break;

        case VK_HOME:
            value = minValue;
            break;

        case VK_END:
            value = maxValue;
            break;

        default:
            return false;
        }

        value = max(minValue, min(maxValue, value));

        if (value != oldValue)
        {
            notifyValueChanged();
            markNeedsPaint();
            return true;
        }

        return false;
    }

    std::shared_ptr<SliderWidget> setMinValue(double min)
    {
        minValue = min;
        if (value < minValue)
            value = minValue;
        markNeedsPaint();
        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

    std::shared_ptr<SliderWidget> setMaxValue(double max)
    {
        maxValue = max;
        if (value > maxValue)
            value = maxValue;
        markNeedsPaint();
        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

    std::shared_ptr<SliderWidget> setStep(double s)
    {
        step = s;
        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

    std::shared_ptr<SliderWidget> setTrackColor(COLORREF color)
    {
        trackColor = color;
        markNeedsPaint();
        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

    std::shared_ptr<SliderWidget> setTrackFillColor(COLORREF color)
    {
        trackFillColor = color;
        markNeedsPaint();
        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

    std::shared_ptr<SliderWidget> setThumbColor(COLORREF color)
    {
        thumbColor = color;
        markNeedsPaint();
        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

    std::shared_ptr<SliderWidget> setOnValueChanged(std::function<void(double)> callback)
    {
        onValueChanged = callback;
        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

    std::shared_ptr<SliderWidget> setValue(State<double> &state)
    {
        value = state.get();
        value = max(minValue, min(maxValue, value));

        state.bindProperty(
            shared_from_this(),
            [](Widget *w, const double &val)
            {
                auto *slider = static_cast<SliderWidget *>(w);
                slider->value = max(slider->minValue, min(slider->maxValue, val));
            },
            false);

        boundDoubleState = &state;

        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

    std::shared_ptr<SliderWidget> setValue(State<int> &state)
    {
        value = (double)state.get();
        value = max(minValue, min(maxValue, value));

        state.bindProperty(
            shared_from_this(),
            [](Widget *w, const int &val)
            {
                auto *slider = static_cast<SliderWidget *>(w);
                slider->value = max(slider->minValue, min(slider->maxValue, (double)val));
            },
            false);

        boundIntState = &state;

        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

private:
    State<double> *boundDoubleState = nullptr;
    State<int> *boundIntState = nullptr;

    void updateValueFromMouseX(int mx)
    {
        int trackLeft = x + paddingLeft;
        int trackRight = x + width - paddingRight;
        int trackWidth = trackRight - trackLeft;

        int clampedX = max(trackLeft, min(trackRight, mx));

        double normalizedPos = (double)(clampedX - trackLeft) / trackWidth;

        double newValue = minValue + normalizedPos * (maxValue - minValue);

        if (step > 0)
        {
            newValue = round(newValue / step) * step;
        }

        newValue = max(minValue, min(maxValue, newValue));

        if (newValue != value)
        {
            value = newValue;
            notifyValueChanged();
            markNeedsPaint();
        }
    }

    void notifyValueChanged()
    {
        if (onValueChanged)
            onValueChanged(value);

        if (boundDoubleState)
            boundDoubleState->set(value);

        if (boundIntState)
            boundIntState->set((int)round(value));
    }
};


// ----------------------------------------------------------------
// Factory Functions
// ----------------------------------------------------------------
using TextInputWidgetPtr = std::shared_ptr<TextInputWidget>;
using CheckBoxWidgetPtr = std::shared_ptr<CheckBoxWidget>;
using SliderWidgetPtr = std::shared_ptr<SliderWidget>;


inline TextInputWidgetPtr TextInput(const std::string &placeholder = "")
{
    auto w = std::make_shared<TextInputWidget>();
    if (!placeholder.empty())
        w->setPlaceholder(placeholder);
    return w;
}

inline CheckBoxWidgetPtr CheckBox(const std::string &label = "")
{
    auto w = std::make_shared<CheckBoxWidget>();
    w->text = label;
    w->textColor = RGB(30, 30, 30);
    w->paddingLeft = w->paddingRight = 4;
    w->paddingTop = w->paddingBottom = 4;
    return w;
}

inline SliderWidgetPtr Slider(double minValue = 0.0, double maxValue = 100.0, double step = 1.0)
{
    auto w = std::make_shared<SliderWidget>();
    w->setMinValue(minValue);
    w->setMaxValue(maxValue);
    w->setStep(step);
    return w;
}



#endif