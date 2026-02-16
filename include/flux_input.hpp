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
    std::string inputValue; // actual input text (separate from label text)
    std::string placeholder;
    int cursorPos = 0;         // character index
    bool cursorVisible = true; // blink state
    UINT cursorTimerId = 1;
    int scrollOffset = 0; // horizontal scroll for long text

    // Colors
    COLORREF focusedBorderColor = RGB(33, 150, 243);
    COLORREF unfocusedBorderColor = RGB(180, 180, 180);
    COLORREF placeholderColor = RGB(180, 180, 180);
    COLORREF inputTextColor = RGB(30, 30, 30);

    TextInputWidget()
    {
        isFocusable = true; // opt in to focus system
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

    // ----------------------------------------------------------------
    // Layout
    // ----------------------------------------------------------------
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;

        applyConstraints();
        needsLayout = false;
    }

    // ----------------------------------------------------------------
    // Render
    // ----------------------------------------------------------------
    void render(HDC hdc, FontCache &fontCache) override
    {
        // Box
        borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
        drawRoundedRectangle(hdc);

        HFONT hFont = fontCache.getFont(fontSize, fontWeight);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        int textX = x + paddingLeft;
        int textY = y + paddingTop;
        int textW = width - paddingLeft - paddingRight;
        int textH = height - paddingTop - paddingBottom;

        // Clip to text area
        RECT clipRect = {x + paddingLeft, y + paddingTop,
                         x + width - paddingRight, y + height - paddingBottom};

        // Set clip region so text doesn't overflow box
        HRGN clipRgn = CreateRectRgn(clipRect.left, clipRect.top,
                                     clipRect.right, clipRect.bottom);
        SelectClipRgn(hdc, clipRgn);

        SetBkMode(hdc, TRANSPARENT);

        if (inputValue.empty() && !placeholder.empty())
        {
            // Draw placeholder
            SetTextColor(hdc, placeholderColor);
            RECT pr = clipRect;
            DrawText(hdc, placeholder.c_str(), -1, &pr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        else
        {

            // Draw input text
            SetTextColor(hdc, inputTextColor);
            RECT tr = clipRect;
            tr.left -= scrollOffset; 
     
            DrawText(hdc, inputValue.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }

        // Draw cursor
        if (isFocused && cursorVisible)
        {
            // Measure text up to cursor position to get cursor X
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

        // Remove clip region
        SelectClipRgn(hdc, nullptr);
        DeleteObject(clipRgn);

        SelectObject(hdc, hOldFont);
        needsPaint = false;
    }

    // ----------------------------------------------------------------
    // Focus
    // ----------------------------------------------------------------
    bool handleFocus(bool focused) override
    {
        isFocused = focused;

        if (focused)
        {
            // Start cursor blink timer
            HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
            SetTimer(hwnd, cursorTimerId, 530, nullptr);
            cursorVisible = true;
        }
        else
        {
            // Stop cursor blink timer
            HWND hwnd = FluxUI::getCurrentInstance()->getWindow();
            KillTimer(hwnd, cursorTimerId);
            cursorVisible = false;
        }

        markNeedsPaint();
        return true;
    }

    // ----------------------------------------------------------------
    // Cursor blink
    // ----------------------------------------------------------------
    bool handleTimer(UINT timerId) override
    {
        if (timerId == cursorTimerId)
        {
            cursorVisible = !cursorVisible;
            return true; // needs repaint
        }
        return false;
    }

    // ----------------------------------------------------------------
    // Mouse - click to focus + position cursor
    // ----------------------------------------------------------------
    bool handleMouseDown(int mx, int my) override
    {
        if (mx >= x && mx < x + width &&
            my >= y && my < y + height)
        {
            // Position cursor at click point
            cursorPos = getCursorPosFromX(mx - x - paddingLeft + scrollOffset);
            return true;
        }
        return false;
    }

    // ----------------------------------------------------------------
    // Keyboard input
    // ----------------------------------------------------------------
    bool handleChar(wchar_t ch) override
    {
        // Ignore control characters (backspace handled in keydown)
        if (ch < 32)
            return false;

        // Insert character at cursor position
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
        case VK_BACK: // Backspace
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

    // ----------------------------------------------------------------
    // State binding
    // ----------------------------------------------------------------
    using CheckBoxWidgetPtr = std::shared_ptr<TextInputWidget>;

    std::shared_ptr<TextInputWidget> setInputValue(State<std::string> &state)
    {
        // State → Widget
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
            false // paint only
        );

        // Store reference for notifyStateBinding
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

    // Push current inputValue back to bound state
    void notifyStateBinding()
    {
        if (boundStringState)
            boundStringState->set(inputValue);
    }

    // Get cursor character index from pixel X offset
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

    // Scroll so cursor is always visible
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

        // Better approach:
        if (cursorX < 10) // Add margin
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
        // box + spacing + text
        if (!text.empty())
        {
            measureText(hdc, fontCache);
            width = boxSize + 8 + width; // box + gap + text
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
        // --- Draw box ---
        int boxX = x + paddingLeft;
        int boxY = y + paddingTop + (height - paddingTop - paddingBottom - boxSize) / 2;

        // Box background
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

        // --- Draw checkmark if checked ---
        if (checked)
        {
            HPEN checkPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HPEN oldCheckPen = (HPEN)SelectObject(hdc, checkPen);

            // Checkmark: two lines forming a tick
            int cx = boxX + 3;
            int cy = boxY + boxSize / 2;

            MoveToEx(hdc, cx, cy, nullptr);
            LineTo(hdc, cx + 4, cy + 4);
            LineTo(hdc, cx + 9, cy - 4);

            SelectObject(hdc, oldCheckPen);
            DeleteObject(checkPen);
        }

        // --- Draw label text ---
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
        // Hit test
        if (mx >= x && mx < x + width &&
            my >= y && my < y + height)
        {
            checked = !checked;

            // Fire onClick so state binding can pick it up
            if (onClick)
                onClick();

            return true;
        }
        return false;
    }

    WidgetPtr setInputValue(State<bool> &state)
    {
        // State → Widget
        checked = state.get();

        state.bindProperty(
            shared_from_this(),
            [](Widget *w, const bool &val)
            {
                auto *cb = static_cast<CheckBoxWidget *>(w);
                cb->checked = val;
            },
            false // paint only
        );

        // Widget → State
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
    double value = 0.0;      // Current value
    double minValue = 0.0;   // Minimum value
    double maxValue = 100.0; // Maximum value
    double step = 1.0;       // Step increment

    // Dimensions
    int trackHeight = 4;
    int thumbRadius = 10;

    // Colors
    COLORREF trackColor = RGB(200, 200, 200);
    COLORREF trackFillColor = RGB(33, 150, 243);
    COLORREF thumbColor = RGB(33, 150, 243);
    COLORREF thumbHoverColor = RGB(25, 118, 210);
    COLORREF thumbDragColor = RGB(13, 71, 161);

    // State
    bool isDragging = false;
    bool isThumbHovered = false;

    // Callbacks
    std::function<void(double)> onValueChanged;

    SliderWidget()
    {
        height = 40;
        autoHeight = false;
        paddingLeft = paddingRight = thumbRadius;
        paddingTop = paddingBottom = 10;
    }

    // ----------------------------------------------------------------
    // Layout
    // ----------------------------------------------------------------
    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        if (autoWidth)
            width = availableWidth;

        applyConstraints();
        needsLayout = false;
    }

    // ----------------------------------------------------------------
    // Render
    // ----------------------------------------------------------------
    void render(HDC hdc, FontCache &fontCache) override
    {
        // Calculate positions
        int trackY = y + height / 2;
        int trackLeft = x + paddingLeft;
        int trackRight = x + width - paddingRight;
        int trackWidth = trackRight - trackLeft;

        // Calculate thumb position based on value
        double normalizedValue = (value - minValue) / (maxValue - minValue);
        int thumbX = trackLeft + (int)(normalizedValue * trackWidth);

        // Draw track background
        HBRUSH trackBrush = CreateSolidBrush(trackColor);
        RECT trackRect = {
            trackLeft,
            trackY - trackHeight / 2,
            trackRight,
            trackY + trackHeight / 2
        };
        FillRect(hdc, &trackRect, trackBrush);
        DeleteObject(trackBrush);

        // Draw filled track (from start to thumb)
        HBRUSH fillBrush = CreateSolidBrush(trackFillColor);
        RECT fillRect = {
            trackLeft,
            trackY - trackHeight / 2,
            thumbX,
            trackY + trackHeight / 2
        };
        FillRect(hdc, &fillRect, fillBrush);
        DeleteObject(fillBrush);

        // Draw thumb
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

    // ----------------------------------------------------------------
    // Mouse Events
    // ----------------------------------------------------------------
    bool handleMouseDown(int mx, int my) override
    {
        if (mx >= x && mx < x + width &&
            my >= y && my < y + height)
        {
            isDragging = true;
            
            // Capture mouse so we get events even outside widget
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

        // Check if hovering over thumb
        int trackY = y + height / 2;
        int trackLeft = x + paddingLeft;
        int trackRight = x + paddingRight;
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

    // ----------------------------------------------------------------
    // Keyboard Events (for accessibility)
    // ----------------------------------------------------------------
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

        // Clamp value
        value = max(minValue, min(maxValue, value));

        if (value != oldValue)
        {
            notifyValueChanged();
            markNeedsPaint();
            return true;
        }

        return false;
    }

    // ----------------------------------------------------------------
    // Builder methods
    // ----------------------------------------------------------------
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

    // ----------------------------------------------------------------
    // State binding
    // ----------------------------------------------------------------
    std::shared_ptr<SliderWidget> setValue(State<double> &state)
    {
        // State → Widget
        value = state.get();
        
        // Clamp initial value
        value = max(minValue, min(maxValue, value));

        state.bindProperty(
            shared_from_this(),
            [](Widget *w, const double &val)
            {
                auto *slider = static_cast<SliderWidget *>(w);
                slider->value = max(slider->minValue, min(slider->maxValue, val));
            },
            false // paint only
        );

        // Widget → State
        boundDoubleState = &state;

        return std::static_pointer_cast<SliderWidget>(shared_from_this());
    }

    // Integer state binding
    std::shared_ptr<SliderWidget> setValue(State<int> &state)
    {
        // State → Widget
        value = (double)state.get();
        
        // Clamp initial value
        value = max(minValue, min(maxValue, value));

        state.bindProperty(
            shared_from_this(),
            [](Widget *w, const int &val)
            {
                auto *slider = static_cast<SliderWidget *>(w);
                slider->value = max(slider->minValue, min(slider->maxValue, (double)val));
            },
            false // paint only
        );

        // Widget → State
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

        // Clamp mouse position to track
        int clampedX = max(trackLeft, min(trackRight, mx));

        // Calculate normalized position (0.0 to 1.0)
        double normalizedPos = (double)(clampedX - trackLeft) / trackWidth;

        // Calculate new value
        double newValue = minValue + normalizedPos * (maxValue - minValue);

        // Apply step
        if (step > 0)
        {
            newValue = round(newValue / step) * step;
        }

        // Clamp to range
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
        // Notify callback
        if (onValueChanged)
            onValueChanged(value);

        // Update bound state
        if (boundDoubleState)
            boundDoubleState->set(value);
        
        if (boundIntState)
            boundIntState->set((int)round(value));
    }
};

// ----------------------------------------------------------------
// Factory
// ----------------------------------------------------------------
using SliderWidgetPtr = std::shared_ptr<SliderWidget>;

inline SliderWidgetPtr Slider(double minValue = 0.0, double maxValue = 100.0, double step = 1.0)
{
    auto w = std::make_shared<SliderWidget>();
    w->setMinValue(minValue);
    w->setMaxValue(maxValue);
    w->setStep(step);
    return w;
}


// ----------------------------------------------------------------
// Factory
// ----------------------------------------------------------------
using TextInputWidgetPtr = std::shared_ptr<TextInputWidget>;

inline TextInputWidgetPtr TextInput(const std::string &placeholder = "")
{
    auto w = std::make_shared<TextInputWidget>();
    if (!placeholder.empty())
        w->setPlaceholder(placeholder);
    return w;
}

// Change from returning WidgetPtr to CheckBoxWidgetPtr
using CheckBoxWidgetPtr = std::shared_ptr<CheckBoxWidget>;

inline CheckBoxWidgetPtr CheckBox(const std::string &label = "")
{
    auto w = std::make_shared<CheckBoxWidget>();
    w->text = label;
    w->textColor = RGB(30, 30, 30);
    w->paddingLeft = w->paddingRight = 4;
    w->paddingTop = w->paddingBottom = 4;
    return w; // returns CheckBoxWidgetPtr, not WidgetPtr
}

#endif