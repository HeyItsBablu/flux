#ifndef FLUX_TEXTINPUT_HPP
#define FLUX_TEXTINPUT_HPP

#include "flux_widget.hpp"
#include "flux_font.hpp"
#include <string>
#include <functional>

// ============================================================================
// TEXTINPUT WIDGET - Similar to Flutter's TextField
// ============================================================================

class TextInputWidget : public Widget
{
public:
    std::string value; // Make value public for direct access

private:
    std::string placeholder;
    bool isFocused = false;
    bool isEnabled = true;
    bool obscureText = false; // For password fields
    int cursorPosition = 0;
    int selectionStart = -1;
    int selectionEnd = -1;
    int maxLength = -1; // -1 means no limit

    // Scroll offset for text that's wider than the input box
    int scrollOffset = 0;

    // Callbacks
    std::function<void(const std::string &)> onChanged;
    std::function<void(const std::string &)> onSubmitted;
    std::function<void()> onEditingComplete;
    std::function<void(bool)> onFocusChanged;

    // Visual properties
    COLORREF focusedBorderColor = RGB(33, 150, 243);
    COLORREF enabledBorderColor = RGB(189, 189, 189);
    COLORREF disabledBorderColor = RGB(224, 224, 224);
    COLORREF cursorColor = RGB(33, 150, 243);
    COLORREF selectionColor = RGB(173, 216, 230);
    COLORREF placeholderColor = RGB(158, 158, 158);

    // Blink cursor animation
    DWORD lastBlinkTime = 0;
    bool cursorVisible = true;
    static const DWORD CURSOR_BLINK_MS = 500;

public:
    TextInputWidget()
    {
        // Default styling
        hasBackground = true;
        backgroundColor = RGB(255, 255, 255);
        hasBorder = true;
        borderColor = enabledBorderColor;
        borderWidth = 1;
        borderRadius = 4;
        paddingLeft = paddingRight = 12;
        paddingTop = paddingBottom = 10;
        height = 40;
        autoHeight = false;
        textColor = RGB(0, 0, 0);
        fontSize = 14;
    }

    // ========================================================================
    // BUILDER METHODS
    // ========================================================================

    std::shared_ptr<TextInputWidget> setPlaceholder(const std::string &text)
    {
        placeholder = text;
        markNeedsPaint();
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setValue(const std::string &text)
    {
        if (maxLength >= 0 && text.length() > (size_t)maxLength)
        {
            value = text.substr(0, maxLength);
        }
        else
        {
            value = text;
        }
        cursorPosition = (int)value.length();
        clearSelection();
        markNeedsPaint();
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setObscureText(bool obscure)
    {
        obscureText = obscure;
        markNeedsPaint();
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setEnabled(bool enabled)
    {
        isEnabled = enabled;
        if (!enabled)
        {
            isFocused = false;
            borderColor = disabledBorderColor;
        }
        else
        {
            borderColor = enabledBorderColor;
        }
        markNeedsPaint();
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setMaxLength(int length)
    {
        maxLength = length;
        if (maxLength >= 0 && value.length() > (size_t)maxLength)
        {
            value = value.substr(0, maxLength);
            if (cursorPosition > maxLength)
                cursorPosition = maxLength;
        }
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setOnChanged(std::function<void(const std::string &)> callback)
    {
        onChanged = callback;
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setOnSubmitted(std::function<void(const std::string &)> callback)
    {
        onSubmitted = callback;
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setOnEditingComplete(std::function<void()> callback)
    {
        onEditingComplete = callback;
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setOnFocusChanged(std::function<void(bool)> callback)
    {
        onFocusChanged = callback;
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setFocusedBorderColor(COLORREF color)
    {
        focusedBorderColor = color;
        if (isFocused)
        {
            borderColor = color;
            markNeedsPaint();
        }
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setCursorColor(COLORREF color)
    {
        cursorColor = color;
        markNeedsPaint();
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setSelectionColor(COLORREF color)
    {
        selectionColor = color;
        markNeedsPaint();
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    std::shared_ptr<TextInputWidget> setPlaceholderColor(COLORREF color)
    {
        placeholderColor = color;
        markNeedsPaint();
        return std::static_pointer_cast<TextInputWidget>(shared_from_this());
    }

    // ========================================================================
    // GETTERS
    // ========================================================================

    std::string getValue() const { return value; }
    bool getIsFocused() const { return isFocused; }
    bool getIsEnabled() const { return isEnabled; }

    // ========================================================================
    // LAYOUT
    // ========================================================================

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache &fontCache) override
    {
        // TextInput has fixed height but can expand width
        if (autoWidth)
        {
            width = availableWidth;
        }

        applyConstraints();
        needsLayout = false;
    }

    // ========================================================================
    // RENDERING
    // ========================================================================

    void render(HDC hdc, FontCache &fontCache) override
    {
        // Update cursor blink
        if (isFocused)
        {
            DWORD currentTime = GetTickCount();
            if (currentTime - lastBlinkTime >= CURSOR_BLINK_MS)
            {
                cursorVisible = !cursorVisible;
                lastBlinkTime = currentTime;
            }
        }

        // Draw background and border
        drawRoundedRectangle(hdc);

        // Setup text rendering
        SetBkMode(hdc, TRANSPARENT);
        HFONT hFont = fontCache.getFont(fontSize, fontWeight);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        // Get text metrics
        TEXTMETRIC tm;
        GetTextMetrics(hdc, &tm);
        int textHeight = tm.tmHeight;

        // Calculate text area
        int textAreaX = x + paddingLeft;
        int textAreaY = y + (height - textHeight) / 2;
        int textAreaWidth = width - paddingLeft - paddingRight;
        int textAreaHeight = textHeight;

        // Display text or placeholder
        std::string displayText = value.empty() ? placeholder : value;
        bool isPlaceholder = value.empty();

        // Obscure text for passwords
        if (obscureText && !isPlaceholder)
        {
            displayText = std::string(value.length(), '*');
        }

        if (!displayText.empty())
        {
            // Set text color
            COLORREF displayColor = isPlaceholder ? placeholderColor : getCurrentTextColor();
            SetTextColor(hdc, displayColor);

            // Calculate text width
            SIZE textSize;
            GetTextExtentPoint32(hdc, displayText.c_str(), (int)displayText.length(), &textSize);

            // Adjust scroll offset to keep cursor visible
            if (isFocused && !isPlaceholder)
            {
                SIZE cursorSize;
                GetTextExtentPoint32(hdc, displayText.c_str(), cursorPosition, &cursorSize);
                int cursorX = cursorSize.cx - scrollOffset;

                // Scroll right if cursor is past right edge
                if (cursorX > textAreaWidth - 5)
                {
                    scrollOffset = cursorSize.cx - textAreaWidth + 20;
                }
                // Scroll left if cursor is past left edge
                else if (cursorX < 5)
                {
                    scrollOffset = cursorSize.cx - 20;
                    if (scrollOffset < 0)
                        scrollOffset = 0;
                }
            }

            // Create clipping region
            HRGN clipRegion = CreateRectRgn(textAreaX, textAreaY,
                                            textAreaX + textAreaWidth,
                                            textAreaY + textAreaHeight);
            SelectClipRgn(hdc, clipRegion);

            // Draw selection background if any
            if (hasSelection() && isFocused)
            {
                int selStart = min(selectionStart, selectionEnd);
                int selEnd = max(selectionStart, selectionEnd);

                SIZE startSize, endSize;
                GetTextExtentPoint32(hdc, displayText.c_str(), selStart, &startSize);
                GetTextExtentPoint32(hdc, displayText.c_str(), selEnd, &endSize);

                RECT selRect = {
                    textAreaX + startSize.cx - scrollOffset,
                    textAreaY,
                    textAreaX + endSize.cx - scrollOffset,
                    textAreaY + textAreaHeight};

                HBRUSH selBrush = CreateSolidBrush(selectionColor); 
                FillRect(hdc, &selRect, selBrush);
                DeleteObject(selBrush);
            }

            // Draw text
            TextOut(hdc, textAreaX - scrollOffset, textAreaY,
                    displayText.c_str(), (int)displayText.length());

            // Draw cursor if focused
            if (isFocused && cursorVisible && !isPlaceholder)
            {
                SIZE cursorSize;
                GetTextExtentPoint32(hdc, displayText.c_str(), cursorPosition, &cursorSize);
                int cursorX = textAreaX + cursorSize.cx - scrollOffset;

                HPEN cursorPen = CreatePen(PS_SOLID, 1, cursorColor);
                HPEN oldPen = (HPEN)SelectObject(hdc, cursorPen);

                MoveToEx(hdc, cursorX, textAreaY, NULL);
                LineTo(hdc, cursorX, textAreaY + textAreaHeight);

                SelectObject(hdc, oldPen);
                DeleteObject(cursorPen);
            }

            // Remove clipping region
            SelectClipRgn(hdc, NULL);
            DeleteObject(clipRegion);
        }

        SelectObject(hdc, hOldFont);
        needsPaint = false;
    }

    // ========================================================================
    // MOUSE EVENTS
    // ========================================================================

    bool handleMouseDown(int mx, int my) override
    {
        if (!isEnabled)
            return false;

        // Check if click is within bounds
        if (mx >= x && mx < x + width && my >= y && my < y + height)
        {
            setFocus(true);

            // Calculate cursor position from click
            int clickX = mx - (x + paddingLeft) + scrollOffset;
            cursorPosition = calculateCursorPosition(clickX);
            clearSelection();

            // Capture mouse for text selection
            if (GetCapture() != GetActiveWindow())
            {
                SetCapture(GetActiveWindow());
            }

            return true;
        }
        else if (isFocused)
        {
            // Clicked outside - lose focus
            setFocus(false);
        }

        return false;
    }

    bool handleMouseUp(int mx, int my) override
    {
        if (GetCapture() == GetActiveWindow())
        {
            ReleaseCapture();
        }
        return false;
    }

    bool handleMouseMove(int mx, int my) override
    {
        if (!isFocused || !isEnabled)
            return false;

        // If mouse is captured, we're selecting text
        if (GetCapture() == GetActiveWindow())
        {
            int clickX = mx - (x + paddingLeft) + scrollOffset;
            int newPosition = calculateCursorPosition(clickX);

            if (selectionStart == -1)
            {
                selectionStart = cursorPosition;
            }

            cursorPosition = newPosition;
            selectionEnd = cursorPosition;

            markNeedsPaint();
            return true;
        }

        return false;
    }

    // ========================================================================
    // KEYBOARD INPUT (to be called from WM_CHAR handler)
    // ========================================================================

    bool handleChar(wchar_t ch)
    {
        if (!isFocused || !isEnabled)
            return false;

        // Handle Enter key
        if (ch == '\r' || ch == '\n')
        {
            if (onSubmitted)
                onSubmitted(value);
            if (onEditingComplete)
                onEditingComplete();
            return true;
        }

        // Handle backspace
        if (ch == '\b')
        {
            if (hasSelection())
            {
                deleteSelection();
            }
            else if (cursorPosition > 0)
            {
                value.erase(cursorPosition - 1, 1);
                cursorPosition--;
            }

            if (onChanged)
                onChanged(value);

            markNeedsPaint();
            return true;
        }

        // Handle printable characters
        if (ch >= 32 && ch < 127)
        {
            // Check max length
            if (maxLength >= 0 && value.length() >= (size_t)maxLength)
                return true;

            if (hasSelection())
            {
                deleteSelection();
            }

            value.insert(cursorPosition, 1, (char)ch);
            cursorPosition++;

            if (onChanged)
                onChanged(value);

            markNeedsPaint();
            return true;
        }

        return false;
    }

    // Handle special keys (to be called from WM_KEYDOWN handler)
    bool handleKeyDown(WPARAM wParam, bool shiftPressed, bool ctrlPressed)
    {
        if (!isFocused || !isEnabled)
            return false;

        switch (wParam)
        {
        case VK_LEFT:
            if (cursorPosition > 0)
            {
                if (shiftPressed)
                {
                    if (selectionStart == -1)
                        selectionStart = cursorPosition;
                    cursorPosition--;
                    selectionEnd = cursorPosition;
                }
                else
                {
                    clearSelection();
                    cursorPosition--;
                }
                markNeedsPaint();
            }
            return true;

        case VK_RIGHT:
            if (cursorPosition < (int)value.length())
            {
                if (shiftPressed)
                {
                    if (selectionStart == -1)
                        selectionStart = cursorPosition;
                    cursorPosition++;
                    selectionEnd = cursorPosition;
                }
                else
                {
                    clearSelection();
                    cursorPosition++;
                }
                markNeedsPaint();
            }
            return true;

        case VK_HOME:
            if (shiftPressed)
            {
                if (selectionStart == -1)
                    selectionStart = cursorPosition;
                cursorPosition = 0;
                selectionEnd = cursorPosition;
            }
            else
            {
                clearSelection();
                cursorPosition = 0;
            }
            scrollOffset = 0;
            markNeedsPaint();
            return true;

        case VK_END:
            if (shiftPressed)
            {
                if (selectionStart == -1)
                    selectionStart = cursorPosition;
                cursorPosition = (int)value.length();
                selectionEnd = cursorPosition;
            }
            else
            {
                clearSelection();
                cursorPosition = (int)value.length();
            }
            markNeedsPaint();
            return true;

        case VK_DELETE:
            if (hasSelection())
            {
                deleteSelection();
            }
            else if (cursorPosition < (int)value.length())
            {
                value.erase(cursorPosition, 1);
            }

            if (onChanged)
                onChanged(value);

            markNeedsPaint();
            return true;

        case 'A':
            if (ctrlPressed)
            {
                // Select all
                selectionStart = 0;
                selectionEnd = (int)value.length();
                cursorPosition = selectionEnd;
                markNeedsPaint();
                return true;
            }
            break;

        case 'C':
            if (ctrlPressed && hasSelection())
            {
                copyToClipboard();
                return true;
            }
            break;

        case 'X':
            if (ctrlPressed && hasSelection())
            {
                copyToClipboard();
                deleteSelection();
                if (onChanged)
                    onChanged(value);
                markNeedsPaint();
                return true;
            }
            break;

        case 'V':
            if (ctrlPressed)
            {
                pasteFromClipboard();
                if (onChanged)
                    onChanged(value);
                markNeedsPaint();
                return true;
            }
            break;
        }

        return false;
    }

private:
    // ========================================================================
    // HELPER METHODS
    // ========================================================================

    void setFocus(bool focused)
    {
        if (isFocused != focused)
        {
            isFocused = focused;

            if (focused)
            {
                borderColor = focusedBorderColor;
                cursorVisible = true;
                lastBlinkTime = GetTickCount();
            }
            else
            {
                borderColor = enabledBorderColor;
                clearSelection();
            }

            if (onFocusChanged)
                onFocusChanged(focused);

            markNeedsPaint();
        }
    }

    int calculateCursorPosition(int clickX)
    {
        if (value.empty())
            return 0;

        std::string displayText = obscureText ? std::string(value.length(), '*') : value;

        HDC hdc = GetDC(GetActiveWindow());

        // Get font from cache instead
        FontCache tempCache; // Or pass fontCache as parameter
        HFONT hFont = tempCache.getFont(fontSize, fontWeight);
        HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

        int bestPos = 0;
        int minDist = INT_MAX;

        for (size_t i = 0; i <= displayText.length(); i++)
        {
            SIZE size;
            GetTextExtentPoint32(hdc, displayText.c_str(), (int)i, &size);
            int dist = abs(size.cx - clickX);

            if (dist < minDist)
            {
                minDist = dist;
                bestPos = (int)i;
            }
        }

        SelectObject(hdc, oldFont);
        ReleaseDC(GetActiveWindow(), hdc);

        return bestPos;
    }
    bool hasSelection() const
    {
        return selectionStart != -1 && selectionEnd != -1 && selectionStart != selectionEnd;
    }

    void clearSelection()
    {
        selectionStart = -1;
        selectionEnd = -1;
    }

    void deleteSelection()
    {
        if (!hasSelection())
            return;

        int start = min(selectionStart, selectionEnd);
        int end = max(selectionStart, selectionEnd);

        value.erase(start, end - start);
        cursorPosition = start;
        clearSelection();
    }

    std::string getSelectedText() const
    {
        if (!hasSelection())
            return "";

        int start = min(selectionStart, selectionEnd);
        int end = max(selectionStart, selectionEnd);

        return value.substr(start, end - start);
    }

    void copyToClipboard()
    {
        std::string selected = getSelectedText();
        if (selected.empty())
            return;

        if (OpenClipboard(GetActiveWindow()))
        {
            EmptyClipboard();

            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, selected.length() + 1);
            if (hMem)
            {
                char *pMem = (char *)GlobalLock(hMem);
                if (pMem)
                {
                    memcpy(pMem, selected.c_str(), selected.length() + 1);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_TEXT, hMem);
                }
            }

            CloseClipboard();
        }
    }

    void pasteFromClipboard()
    {
        if (!OpenClipboard(GetActiveWindow()))
            return;

        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData)
        {
            char *pData = (char *)GlobalLock(hData);
            if (pData)
            {
                std::string pasteText = pData;
                GlobalUnlock(hData);

                // Delete selection if any
                if (hasSelection())
                {
                    deleteSelection();
                }

                // Check max length
                if (maxLength >= 0)
                {
                    int remainingSpace = maxLength - (int)value.length();
                    if (remainingSpace < (int)pasteText.length())
                    {
                        pasteText = pasteText.substr(0, remainingSpace);
                    }
                }

                // Insert text
                value.insert(cursorPosition, pasteText);
                cursorPosition += (int)pasteText.length();
            }
        }

        CloseClipboard();
    }
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

inline WidgetPtr TextInput(const std::string &placeholder = "", const std::string &initialValue = "")
{
    auto w = std::make_shared<TextInputWidget>();
    if (!placeholder.empty())
        w->setPlaceholder(placeholder);
    if (!initialValue.empty())
        w->setValue(initialValue);
    return w;
}

#endif // FLUX_TEXTINPUT_HPP