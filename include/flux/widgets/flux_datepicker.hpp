#ifndef FLUX_DATE_PICKER_HPP
#define FLUX_DATE_PICKER_HPP

#include "flux_core.hpp"
#include "flux_overlays.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

// ============================================================================
// DATE STRUCT
// ============================================================================

struct FluxDate {
    int year  = 0;
    int month = 0; // 1-12
    int day   = 0; // 1-31

    bool isValid() const {
        return year > 0 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
    }

    bool operator==(const FluxDate &o) const {
        return year == o.year && month == o.month && day == o.day;
    }
    bool operator!=(const FluxDate &o) const { return !(*this == o); }
    bool operator<(const FluxDate &o) const {
        if (year  != o.year)  return year  < o.year;
        if (month != o.month) return month < o.month;
        return day < o.day;
    }

    std::string toString(const std::string &fmt = "%Y-%m-%d") const {
        if (!isValid()) return "";
        std::tm t{};
        t.tm_year = year - 1900;
        t.tm_mon  = month - 1;
        t.tm_mday = day;
        std::ostringstream oss;
        oss << std::put_time(&t, fmt.c_str());
        return oss.str();
    }

    static FluxDate today() {
        std::time_t now = std::time(nullptr);
        std::tm *lt = std::localtime(&now);
        return {lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday};
    }
};

// ============================================================================
// DATE PICKER WIDGET
// ============================================================================
//
// A text-field-style trigger that opens a calendar popup when clicked.
// Follows the same OverlayHost pattern as DropdownWidget.
//
// Usage:
//   auto dp = DatePicker()
//                 ->setDate(FluxDate::today())
//                 ->setOnDateChanged([](FluxDate d) {
//                     std::cout << d.toString() << std::endl;
//                 });
//
// Reactive binding:
//   State<FluxDate> selectedDate(FluxDate::today(), app);
//   auto dp = DatePicker()->setDate(selectedDate);
// ============================================================================

class DatePickerWidget : public Widget, public OverlayHost {
public:
    // ── Selection & navigation state ─────────────────────────────────────────
    FluxDate selectedDate;          // currently selected date (may be invalid)
    int      viewYear  = 0;         // calendar grid is showing this year/month
    int      viewMonth = 0;

    bool isOpen       = false;
    bool showingYears = false;      // year-picker overlay inside the popup

    // ── Appearance — trigger field ────────────────────────────────────────────
    std::string placeholder     = "Select a date...";
    std::string dateFormat      = "%d / %m / %Y";

    COLORREF fieldBgColor       = RGB(255, 255, 255);
    COLORREF fieldBorderColor   = RGB(180, 180, 180);
    COLORREF fieldFocusBorder   = RGB(33,  150, 243);
    COLORREF fieldTextColor     = RGB(30,  30,  30);
    COLORREF placeholderColor   = RGB(160, 160, 160);
    int      fieldFontSize      = 13;

    // ── Appearance — calendar popup ───────────────────────────────────────────
    int      calWidth           = 280;
    int      calHeight          = 300;  // auto-computed; exposed for override
    int      calCellSize        = 34;
    int      calHeaderH         = 40;
    int      calWeekRowH        = 24;
    int      calPadH            = 10;
    int      calPadV            = 8;
    int      calBorderRadius    = 8;
    int      shadowOffset       = 3;

    COLORREF calBgColor         = RGB(255, 255, 255);
    COLORREF calBorderColor     = RGB(200, 200, 200);
    COLORREF headerBgColor      = RGB(33,  150, 243);
    COLORREF headerTextColor    = RGB(255, 255, 255);
    COLORREF weekdayTextColor   = RGB(120, 120, 120);
    COLORREF dayTextColor       = RGB(30,  30,  30);
    COLORREF dayHoverBg         = RGB(232, 245, 255);
    COLORREF daySelectedBg      = RGB(33,  150, 243);
    COLORREF daySelectedText    = RGB(255, 255, 255);
    COLORREF todayBorderColor   = RGB(33,  150, 243);
    COLORREF otherMonthText     = RGB(190, 190, 190);
    COLORREF navArrowColor      = RGB(255, 255, 255);
    COLORREF yearHoverBg        = RGB(232, 245, 255);
    COLORREF yearSelectedBg     = RGB(33,  150, 243);
    COLORREF yearSelectedText   = RGB(255, 255, 255);

    // ── Constraints ───────────────────────────────────────────────────────────
    FluxDate minDate;   // invalid = no minimum
    FluxDate maxDate;   // invalid = no maximum

    // ── Callback ─────────────────────────────────────────────────────────────
    std::function<void(FluxDate)> onDateChanged;

    // ─────────────────────────────────────────────────────────────────────────

    DatePickerWidget() {
        isFocusable    = true;
        hasBorder      = true;
        hasBackground  = true;
        backgroundColor = fieldBgColor;
        borderColor    = fieldBorderColor;
        borderWidth    = 1;
        borderRadius   = 4;
        height         = 36;
        autoHeight     = false;
        paddingLeft    = 12;
        paddingRight   = 36; // room for calendar icon
        paddingTop = paddingBottom = 8;

        FluxDate td = FluxDate::today();
        viewYear  = td.year;
        viewMonth = td.month;
    }

    void setScaffold(ScaffoldWidget *s) override { scaffold_ = s; }

    void onDetach() override {
        if (isOpen) closeCalendar_();
        Widget::onDetach();
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext &/*ctx*/, const BoxConstraints &constraints,
                       FontCache &) override {
        if (autoWidth) width = constraints.maxWidth;
        applyConstraints();
        needsLayout = false;
    }

    // ── Render the trigger field ──────────────────────────────────────────────
    void render(GraphicsContext &ctx, FontCache &fontCache) override {
        if (!visible) return;

        borderColor = isFocused ? fieldFocusBorder : fieldBorderColor;
        drawRoundedRectangle(ctx);

        // Date text or placeholder
        HFONT hFont    = fontCache.getFont(fieldFontSize, FontWeight::Normal);
        HFONT hOldFont = (HFONT)SelectObject(ctx.hdc, hFont);
        SetBkMode(ctx.hdc, TRANSPARENT);

        RECT tr = {x + paddingLeft, y, x + width - paddingRight, y + height};

        if (selectedDate.isValid()) {
            SetTextColor(ctx.hdc, fieldTextColor);
            std::string label = selectedDate.toString(dateFormat);
            DrawTextA(ctx.hdc, label.c_str(), -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetTextColor(ctx.hdc, placeholderColor);
            DrawTextA(ctx.hdc, placeholder.c_str(), -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // Calendar icon (simple grid of dots)
        _drawCalendarIcon(ctx, x + width - 26, y + height / 2 - 8);

        SelectObject(ctx.hdc, hOldFont);
        needsPaint = false;
    }

    // ── renderPopupContent ────────────────────────────────────────────────────
    void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
        if (!isOpen) return;
        _computePopupSize();

        // Shadow
        {
            HBRUSH sb = CreateSolidBrush(RGB(0, 0, 0));
            HRGN   sr = CreateRoundRectRgn(shadowOffset, shadowOffset,
                                           popupW_ + shadowOffset,
                                           popupH_ + shadowOffset,
                                           calBorderRadius * 2, calBorderRadius * 2);
            FillRgn(ctx.hdc, sr, sb);
            DeleteObject(sr); DeleteObject(sb);
        }

        // Background
        {
            HPEN   pen   = CreatePen(PS_SOLID, 1, calBorderColor);
            HBRUSH brush = CreateSolidBrush(calBgColor);
            HPEN   op    = (HPEN)  SelectObject(ctx.hdc, pen);
            HBRUSH ob    = (HBRUSH)SelectObject(ctx.hdc, brush);
            RoundRect(ctx.hdc, 0, 0, popupW_, popupH_,
                      calBorderRadius * 2, calBorderRadius * 2);
            SelectObject(ctx.hdc, ob); SelectObject(ctx.hdc, op);
            DeleteObject(brush); DeleteObject(pen);
        }

        if (showingYears)
            _renderYearPicker(ctx, fontCache);
        else
            _renderCalendarGrid(ctx, fontCache);
    }

    // ── Mouse events ──────────────────────────────────────────────────────────

    bool handleMouseDown(int mx, int my) override {
        // Click on the field → open/close
        if (mx >= x && mx < x + width && my >= y && my < y + height) {
            if (isOpen) closeCalendar_();
            else        openCalendar_();
            return true;
        }

        if (!isOpen) return false;

        // Click inside the popup
        HWND hw = getFluxTopLevel();
        POINT origin = {popupScreenX_, popupScreenY_};
        if (hw) ScreenToClient(hw, &origin);

        int rx = mx - origin.x;
        int ry = my - origin.y;

        if (rx >= 0 && rx < popupW_ && ry >= 0 && ry < popupH_) {
            if (showingYears)
                _handleYearPickerClick(rx, ry);
            else
                _handleCalendarClick(rx, ry);
            return true;
        }

        // Click outside → close
        closeCalendar_();
        return true;
    }

    bool handleMouseMove(int mx, int my) override {
        if (!isOpen) return false;

        HWND hw = getFluxTopLevel();
        POINT origin = {popupScreenX_, popupScreenY_};
        if (hw) ScreenToClient(hw, &origin);

        int rx = mx - origin.x;
        int ry = my - origin.y;

        int newHover = -1;
        if (rx >= 0 && rx < popupW_ && ry >= 0 && ry < popupH_) {
            if (showingYears)
                newHover = _yearIndexAt(rx, ry);
            else
                newHover = _dayIndexAt(rx, ry);
        }

        if (newHover != hoveredCell_) {
            hoveredCell_ = newHover;
            refresh_();
        }
        return false;
    }

    bool handleFocus(bool focused) override {
        isFocused = focused;
        if (!focused && isOpen) closeCalendar_();
        markNeedsPaint();
        return true;
    }

    // ── Fluent setters ────────────────────────────────────────────────────────

    std::shared_ptr<DatePickerWidget> setDate(const FluxDate &d) {
        selectedDate = d;
        if (d.isValid()) { viewYear = d.year; viewMonth = d.month; }
        markNeedsPaint();
        return self_();
    }

    std::shared_ptr<DatePickerWidget> setDate(State<FluxDate> &state) {
        setDate(state.get());
        state.bindProperty(shared_from_this(),
            [](Widget *w, const FluxDate &d) {
                auto *dp = static_cast<DatePickerWidget *>(w);
                dp->selectedDate = d;
                if (d.isValid()) { dp->viewYear = d.year; dp->viewMonth = d.month; }
                dp->markNeedsPaint();
            }, false);
        boundState_ = &state;
        return self_();
    }

    std::shared_ptr<DatePickerWidget> setOnDateChanged(std::function<void(FluxDate)> cb) {
        onDateChanged = std::move(cb); return self_();
    }
    std::shared_ptr<DatePickerWidget> setPlaceholder(const std::string &p) {
        placeholder = p; markNeedsPaint(); return self_();
    }
    std::shared_ptr<DatePickerWidget> setDateFormat(const std::string &f) {
        dateFormat = f; markNeedsPaint(); return self_();
    }
    std::shared_ptr<DatePickerWidget> setMinDate(const FluxDate &d) {
        minDate = d; return self_();
    }
    std::shared_ptr<DatePickerWidget> setMaxDate(const FluxDate &d) {
        maxDate = d; return self_();
    }
    std::shared_ptr<DatePickerWidget> setWidth(int w) {
        width = w; autoWidth = false; markNeedsLayout(); return self_();
    }
    std::shared_ptr<DatePickerWidget> setAccentColor(COLORREF c) {
        headerBgColor    = c;
        daySelectedBg    = c;
        todayBorderColor = c;
        fieldFocusBorder = c;
        yearSelectedBg   = c;
        markNeedsPaint();
        return self_();
    }

private:
    ScaffoldWidget   *scaffold_     = nullptr;
    State<FluxDate>  *boundState_   = nullptr;

    int popupScreenX_ = 0, popupScreenY_ = 0;
    int popupW_       = 0, popupH_       = 0;
    int hoveredCell_  = -1; // day index (0-41) or year index

    // ── Helpers ───────────────────────────────────────────────────────────────

    std::shared_ptr<DatePickerWidget> self_() {
        return std::static_pointer_cast<DatePickerWidget>(shared_from_this());
    }

    static int _daysInMonth(int year, int month) {
        static const int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        if (month == 2) {
            bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
            return leap ? 29 : 28;
        }
        return days[month];
    }

    // 0=Sun,1=Mon,...,6=Sat for first day of viewYear/viewMonth
    static int _firstWeekday(int year, int month) {
        std::tm t{};
        t.tm_year = year - 1900;
        t.tm_mon  = month - 1;
        t.tm_mday = 1;
        std::mktime(&t);
        return t.tm_wday; // 0=Sun
    }

    void _computePopupSize() {
        int rows  = 6; // max weeks per month
        popupW_ = calPadH * 2 + calCellSize * 7;
        popupH_ = calHeaderH + calWeekRowH + rows * calCellSize + calPadV * 2;
        calWidth  = popupW_;
        calHeight = popupH_;
    }

    static const char *_monthName(int m) {
        static const char *names[] = {
            "","January","February","March","April","May","June",
            "July","August","September","October","November","December"};
        return (m >= 1 && m <= 12) ? names[m] : "";
    }

    static const char *_weekdayShort(int d) {
        static const char *names[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
        return names[d % 7];
    }

    bool _isDisabled(int year, int month, int day) const {
        FluxDate d{year, month, day};
        if (minDate.isValid() && d < minDate) return true;
        if (maxDate.isValid() && maxDate < d)  return true;
        return false;
    }

    // ── Popup open/close ──────────────────────────────────────────────────────

    void openCalendar_() {
        if (isOpen) return;
        if (!selectedDate.isValid()) {
            FluxDate td = FluxDate::today();
            viewYear = td.year; viewMonth = td.month;
        }
        isOpen        = true;
        showingYears  = false;
        hoveredCell_  = -1;

        _computePopupSize();

        HWND hw = getFluxTopLevel();
        if (hw) {
            // Position below the field
            POINT sc = fluxClientToScreen(x, y + height + 2);
            popupScreenX_ = sc.x;
            popupScreenY_ = sc.y;

            // Clamp to monitor
            HMONITOR mon = MonitorFromPoint(sc, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{}; mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(mon, &mi)) {
                if (popupScreenX_ + popupW_ > mi.rcWork.right)
                    popupScreenX_ = mi.rcWork.right - popupW_;
                if (popupScreenY_ + popupH_ > mi.rcWork.bottom)
                    popupScreenY_ = fluxClientToScreen(x, y - popupH_ - 2).y;
            }

            FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
            showPopup(hw, popupScreenX_, popupScreenY_,
                      popupW_ + shadowOffset, popupH_ + shadowOffset, fc);
        }

        if (scaffold_)
            scaffold_->addOverlay(this, [](HDC, FontCache &) {}, 100);

        markNeedsPaint();
    }

    void closeCalendar_() {
        if (!isOpen) return;
        isOpen       = false;
        showingYears = false;
        hoveredCell_ = -1;
        hidePopup();
        if (scaffold_) scaffold_->removeOverlay(this);
        markNeedsPaint();
    }

    void refresh_() {
        if (!isOpen || !popupVisible()) return;
        if (auto *ui = FluxUI::getCurrentInstance())
            refreshPopup(ui->getFontCache());
    }

    // ── Calendar rendering ────────────────────────────────────────────────────

    void _renderCalendarGrid(GraphicsContext &ctx, FontCache &fontCache) {
        FluxDate today = FluxDate::today();
        int firstWD    = _firstWeekday(viewYear, viewMonth);
        int daysInMon  = _daysInMonth(viewYear, viewMonth);

        // ── Header ────────────────────────────────────────────────────────────
        {
            HBRUSH hb = CreateSolidBrush(headerBgColor);
            RECT   hr = {0, 0, popupW_, calHeaderH};
            FillRect(ctx.hdc, &hr, hb);
            DeleteObject(hb);
        }

        // Left arrow  ◀
        _drawNavArrow(ctx, calPadH + 8, calHeaderH / 2, false);
        // Right arrow ▶
        _drawNavArrow(ctx, popupW_ - calPadH - 8, calHeaderH / 2, true);

        // Month + Year label (clickable → year picker)
        {
            std::string label = std::string(_monthName(viewMonth)) +
                                "  " + std::to_string(viewYear);
            HFONT hf  = fontCache.getFont(14, FontWeight::Bold);
            HFONT old = (HFONT)SelectObject(ctx.hdc, hf);
            SetBkMode(ctx.hdc, TRANSPARENT);
            SetTextColor(ctx.hdc, headerTextColor);
            RECT lr = {calPadH + 24, 0, popupW_ - calPadH - 24, calHeaderH};
            DrawTextA(ctx.hdc, label.c_str(), -1, &lr,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(ctx.hdc, old);
        }

        // ── Weekday row ───────────────────────────────────────────────────────
        {
            HFONT hf  = fontCache.getFont(11, FontWeight::Normal);
            HFONT old = (HFONT)SelectObject(ctx.hdc, hf);
            SetBkMode(ctx.hdc, TRANSPARENT);
            SetTextColor(ctx.hdc, weekdayTextColor);
            for (int col = 0; col < 7; col++) {
                int cx = calPadH + col * calCellSize;
                RECT cr = {cx, calHeaderH + calPadV,
                           cx + calCellSize, calHeaderH + calPadV + calWeekRowH};
                DrawTextA(ctx.hdc, _weekdayShort(col), -1, &cr,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(ctx.hdc, old);
        }

        // ── Day cells ─────────────────────────────────────────────────────────
        int gridTop = calHeaderH + calPadV + calWeekRowH;
        HFONT hf  = fontCache.getFont(12, FontWeight::Normal);
        HFONT old = (HFONT)SelectObject(ctx.hdc, hf);
        SetBkMode(ctx.hdc, TRANSPARENT);

        // Previous month tail
        int prevDays = _daysInMonth(
            viewMonth == 1 ? viewYear - 1 : viewYear,
            viewMonth == 1 ? 12           : viewMonth - 1);

        for (int cell = 0; cell < 42; cell++) {
            int col = cell % 7;
            int row = cell / 7;
            int cx  = calPadH + col * calCellSize;
            int cy  = gridTop + row * calCellSize;

            int dayNum, dYear, dMonth;
            bool thisMonth;

            if (cell < firstWD) {
                dayNum    = prevDays - firstWD + cell + 1;
                dMonth    = viewMonth == 1 ? 12       : viewMonth - 1;
                dYear     = viewMonth == 1 ? viewYear - 1 : viewYear;
                thisMonth = false;
            } else if (cell - firstWD < daysInMon) {
                dayNum    = cell - firstWD + 1;
                dMonth    = viewMonth;
                dYear     = viewYear;
                thisMonth = true;
            } else {
                dayNum    = cell - firstWD - daysInMon + 1;
                dMonth    = viewMonth == 12 ? 1        : viewMonth + 1;
                dYear     = viewMonth == 12 ? viewYear + 1 : viewYear;
                thisMonth = false;
            }

            bool isSelected = selectedDate.isValid() &&
                              selectedDate.year  == dYear  &&
                              selectedDate.month == dMonth &&
                              selectedDate.day   == dayNum;
            bool isToday    = today.year  == dYear  &&
                              today.month == dMonth &&
                              today.day   == dayNum;
            bool isHovered  = (cell == hoveredCell_) && thisMonth;
            bool isDisabled = _isDisabled(dYear, dMonth, dayNum);

            RECT cr = {cx + 1, cy + 1, cx + calCellSize - 1, cy + calCellSize - 1};

            // Cell background
            if (isSelected) {
                HBRUSH sb = CreateSolidBrush(daySelectedBg);
                HRGN   rg = CreateRoundRectRgn(cx+2, cy+2,
                                               cx+calCellSize-2, cy+calCellSize-2,
                                               8, 8);
                FillRgn(ctx.hdc, rg, sb);
                DeleteObject(rg); DeleteObject(sb);
            } else if (isHovered && !isDisabled) {
                HBRUSH hb = CreateSolidBrush(dayHoverBg);
                HRGN   rg = CreateRoundRectRgn(cx+2, cy+2,
                                               cx+calCellSize-2, cy+calCellSize-2,
                                               8, 8);
                FillRgn(ctx.hdc, rg, hb);
                DeleteObject(rg); DeleteObject(hb);
            }

            // Today border ring
            if (isToday && !isSelected) {
                HPEN tp  = CreatePen(PS_SOLID, 1, todayBorderColor);
                HPEN old2 = (HPEN)SelectObject(ctx.hdc, tp);
                HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
                HBRUSH ob = (HBRUSH)SelectObject(ctx.hdc, nb);
                RoundRect(ctx.hdc, cx+2, cy+2, cx+calCellSize-2, cy+calCellSize-2, 8, 8);
                SelectObject(ctx.hdc, old2); SelectObject(ctx.hdc, ob);
                DeleteObject(tp);
            }

            // Day number
            COLORREF textCol;
            if (isSelected)       textCol = daySelectedText;
            else if (!thisMonth)  textCol = otherMonthText;
            else if (isDisabled)  textCol = otherMonthText;
            else                  textCol = dayTextColor;

            SetTextColor(ctx.hdc, textCol);
            std::string ds = std::to_string(dayNum);
            DrawTextA(ctx.hdc, ds.c_str(), -1, &cr,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(ctx.hdc, old);
    }

    // ── Year picker ───────────────────────────────────────────────────────────

    void _renderYearPicker(GraphicsContext &ctx, FontCache &fontCache) {
        // Header
        {
            HBRUSH hb = CreateSolidBrush(headerBgColor);
            RECT   hr = {0, 0, popupW_, calHeaderH};
            FillRect(ctx.hdc, &hr, hb);
            DeleteObject(hb);
        }
        // Back arrow
        _drawNavArrow(ctx, calPadH + 8, calHeaderH / 2, false);
        _drawNavArrow(ctx, popupW_ - calPadH - 8, calHeaderH / 2, true);

        HFONT hf  = fontCache.getFont(14, FontWeight::Bold);
        HFONT old = (HFONT)SelectObject(ctx.hdc, hf);
        SetBkMode(ctx.hdc, TRANSPARENT);
        SetTextColor(ctx.hdc, headerTextColor);
        std::string range = std::to_string(yearRangeStart_) + " – " +
                            std::to_string(yearRangeStart_ + 11);
        RECT lr = {calPadH + 24, 0, popupW_ - calPadH - 24, calHeaderH};
        DrawTextA(ctx.hdc, range.c_str(), -1, &lr,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(ctx.hdc, old);

        // 4×3 year grid
        hf  = fontCache.getFont(12, FontWeight::Normal);
        old = (HFONT)SelectObject(ctx.hdc, hf);
        SetBkMode(ctx.hdc, TRANSPARENT);

        int cellW = popupW_ / 4;
        int cellH = (popupH_ - calHeaderH) / 3;

        for (int i = 0; i < 12; i++) {
            int yr  = yearRangeStart_ + i;
            int col = i % 4;
            int row = i / 4;
            int cx  = col * cellW;
            int cy  = calHeaderH + row * cellH;

            bool isSel   = (yr == viewYear);
            bool isHov   = (i == hoveredCell_);

            if (isSel) {
                HBRUSH sb = CreateSolidBrush(yearSelectedBg);
                HRGN   rg = CreateRoundRectRgn(cx+4, cy+4,
                                               cx+cellW-4, cy+cellH-4, 8, 8);
                FillRgn(ctx.hdc, rg, sb);
                DeleteObject(rg); DeleteObject(sb);
            } else if (isHov) {
                HBRUSH hb = CreateSolidBrush(yearHoverBg);
                HRGN   rg = CreateRoundRectRgn(cx+4, cy+4,
                                               cx+cellW-4, cy+cellH-4, 8, 8);
                FillRgn(ctx.hdc, rg, hb);
                DeleteObject(rg); DeleteObject(hb);
            }

            SetTextColor(ctx.hdc, isSel ? yearSelectedText : dayTextColor);
            RECT cr = {cx, cy, cx + cellW, cy + cellH};
            std::string ys = std::to_string(yr);
            DrawTextA(ctx.hdc, ys.c_str(), -1, &cr,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(ctx.hdc, old);
    }

    // ── Click handlers ────────────────────────────────────────────────────────

    void _handleCalendarClick(int rx, int ry) {
        // Nav arrows
        bool leftArrow  = (rx >= calPadH && rx < calPadH + 24 &&
                           ry >= 0 && ry < calHeaderH);
        bool rightArrow = (rx >= popupW_ - calPadH - 24 && rx < popupW_ &&
                           ry >= 0 && ry < calHeaderH);
        bool headerClick = (!leftArrow && !rightArrow && ry < calHeaderH);

        if (leftArrow) {
            viewMonth--;
            if (viewMonth < 1) { viewMonth = 12; viewYear--; }
            hoveredCell_ = -1; refresh_(); return;
        }
        if (rightArrow) {
            viewMonth++;
            if (viewMonth > 12) { viewMonth = 1; viewYear++; }
            hoveredCell_ = -1; refresh_(); return;
        }
        if (headerClick) {
            // Open year picker
            yearRangeStart_ = (viewYear / 12) * 12;
            showingYears    = true;
            hoveredCell_    = -1;
            refresh_(); return;
        }

        // Day cell
        int cell = _dayIndexAt(rx, ry);
        if (cell < 0) return;

        int firstWD   = _firstWeekday(viewYear, viewMonth);
        int daysInMon = _daysInMonth(viewYear, viewMonth);

        int dayNum, dYear, dMonth;
        int prevDays = _daysInMonth(
            viewMonth == 1 ? viewYear - 1 : viewYear,
            viewMonth == 1 ? 12           : viewMonth - 1);

        if (cell < firstWD) {
            dayNum = prevDays - firstWD + cell + 1;
            dMonth = viewMonth == 1 ? 12 : viewMonth - 1;
            dYear  = viewMonth == 1 ? viewYear - 1 : viewYear;
        } else if (cell - firstWD < daysInMon) {
            dayNum = cell - firstWD + 1;
            dMonth = viewMonth; dYear = viewYear;
        } else {
            dayNum = cell - firstWD - daysInMon + 1;
            dMonth = viewMonth == 12 ? 1 : viewMonth + 1;
            dYear  = viewMonth == 12 ? viewYear + 1 : viewYear;
        }

        if (_isDisabled(dYear, dMonth, dayNum)) return;

        FluxDate newDate{dYear, dMonth, dayNum};
        selectedDate = newDate;
        viewYear     = dYear;
        viewMonth    = dMonth;

        if (boundState_) boundState_->set(newDate);
        if (onDateChanged) onDateChanged(newDate);

        markNeedsPaint();
        closeCalendar_();
    }

    void _handleYearPickerClick(int rx, int ry) {
        bool leftArrow  = (rx >= calPadH && rx < calPadH + 24 &&
                           ry >= 0 && ry < calHeaderH);
        bool rightArrow = (rx >= popupW_ - calPadH - 24 && rx < popupW_ &&
                           ry >= 0 && ry < calHeaderH);

        if (leftArrow)  { yearRangeStart_ -= 12; hoveredCell_ = -1; refresh_(); return; }
        if (rightArrow) { yearRangeStart_ += 12; hoveredCell_ = -1; refresh_(); return; }
        if (ry < calHeaderH) return;

        int i = _yearIndexAt(rx, ry);
        if (i < 0 || i >= 12) return;

        viewYear     = yearRangeStart_ + i;
        showingYears = false;
        hoveredCell_ = -1;
        refresh_();
    }

    // ── Hit testing ───────────────────────────────────────────────────────────

    // Returns 0-41 for the cell under (rx,ry) in the calendar grid, else -1
    int _dayIndexAt(int rx, int ry) const {
        int gridTop = calHeaderH + calPadV + calWeekRowH;
        if (ry < gridTop) return -1;
        int col = (rx - calPadH) / calCellSize;
        int row = (ry - gridTop) / calCellSize;
        if (col < 0 || col >= 7 || row < 0 || row >= 6) return -1;
        return row * 7 + col;
    }

    // Returns 0-11 for the year cell under (rx,ry), else -1
    int _yearIndexAt(int rx, int ry) const {
        if (ry < calHeaderH) return -1;
        int cellW = popupW_ / 4;
        int cellH = (popupH_ - calHeaderH) / 3;
        int col   = rx / cellW;
        int row   = (ry - calHeaderH) / cellH;
        if (col < 0 || col >= 4 || row < 0 || row >= 3) return -1;
        return row * 4 + col;
    }

    // ── Drawing helpers ───────────────────────────────────────────────────────

    void _drawNavArrow(GraphicsContext &ctx, int cx, int cy, bool right) const {
        HPEN pen = CreatePen(PS_SOLID, 2, navArrowColor);
        HPEN old = (HPEN)SelectObject(ctx.hdc, pen);
        int  s   = 5;
        if (right) {
            MoveToEx(ctx.hdc, cx - s, cy - s, nullptr);
            LineTo  (ctx.hdc, cx + s, cy);
            LineTo  (ctx.hdc, cx - s, cy + s);
        } else {
            MoveToEx(ctx.hdc, cx + s, cy - s, nullptr);
            LineTo  (ctx.hdc, cx - s, cy);
            LineTo  (ctx.hdc, cx + s, cy + s);
        }
        SelectObject(ctx.hdc, old);
        DeleteObject(pen);
    }

    void _drawCalendarIcon(GraphicsContext &ctx, int cx, int cy) const {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(140, 140, 140));
        HPEN old = (HPEN)SelectObject(ctx.hdc, pen);

        // Outer rect
        HBRUSH nb  = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH ob  = (HBRUSH)SelectObject(ctx.hdc, nb);
        Rectangle(ctx.hdc, cx, cy + 2, cx + 16, cy + 16);

        // Top bar
        HBRUSH fb = CreateSolidBrush(RGB(140, 140, 140));
        SelectObject(ctx.hdc, fb);
        RECT topBar = {cx, cy + 2, cx + 16, cy + 6};
        FillRect(ctx.hdc, &topBar, fb);
        DeleteObject(fb);

        // Grid dots (2×3)
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++) {
                int dx = cx + 3 + c * 4;
                int dy = cy + 8 + r * 4;
                SetPixel(ctx.hdc, dx, dy, RGB(140, 140, 140));
            }

        SelectObject(ctx.hdc, ob);
        SelectObject(ctx.hdc, old);
        DeleteObject(pen);
    }

    int yearRangeStart_ = 2020;
};

// ============================================================================
// FACTORY
// ============================================================================

using DatePickerWidgetPtr = std::shared_ptr<DatePickerWidget>;

inline DatePickerWidgetPtr DatePicker() {
    return std::make_shared<DatePickerWidget>();
}

#endif // FLUX_DATE_PICKER_HPP