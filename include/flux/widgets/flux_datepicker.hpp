#ifndef FLUX_DATE_PICKER_HPP
#define FLUX_DATE_PICKER_HPP

#include "flux/flux_core.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>


#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
#include "flux/flux_dom_adapter.hpp"
// Declared in flux_painter_dom.cpp — CalendarSurface is a long-lived
// overlay widget (registered once via showOverlay/hideOverlay, never
// addChild'd into any parent), so Widget::onDetach() never fires for it.
// closeCalendar_() below calls this directly instead — same reasoning as
// DropdownWidget::closeDropdown().
extern void fluxDomEvictWidget(Widget *owner);
#endif


// ============================================================================
// DATE STRUCT
// ============================================================================

struct FluxDate
{
    int year = 0;
    int month = 0; // 1-12
    int day = 0;   // 1-31

    bool isValid() const
    {
        return year > 0 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
    }

    bool operator==(const FluxDate &o) const
    {
        return year == o.year && month == o.month && day == o.day;
    }
    bool operator!=(const FluxDate &o) const { return !(*this == o); }
    bool operator<(const FluxDate &o) const
    {
        if (year != o.year)
            return year < o.year;
        if (month != o.month)
            return month < o.month;
        return day < o.day;
    }

    std::string toString(const std::string &fmt = "%Y-%m-%d") const
    {
        if (!isValid())
            return "";
        std::tm t{};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        std::ostringstream oss;
        oss << std::put_time(&t, fmt.c_str());
        return oss.str();
    }

    static FluxDate today()
    {
        std::time_t now = std::time(nullptr);
#ifdef _WIN32
        std::tm lt{};
        localtime_s(&lt, &now);
        return {lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday};
#else
        std::tm *lt = std::localtime(&now);
        return {lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday};
#endif
    }
};

// ============================================================================
// DATE PICKER WIDGET
// ============================================================================
//
// A text-field-style trigger that opens a calendar popup when clicked.
// Follows the same OverlayContent pattern as DropdownWidget — the trigger
// field is a normal Widget in the tree; the calendar popup is owned and
// positioned by OverlayManager and rendered/hit-tested entirely in
// coordinates LOCAL to the popup's own rect (0,0 = popup top-left).
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

class DatePickerWidget : public Widget
{
public:
    // ── Popup-body surface ─────────────────────────────────────────────────
    // The calendar/year-picker popup. render()/handle* offset every draw and
    // hit-test by the surface's own absolute x/y — replacing the translation
    // OverlayManager::renderAll() used to bake in per-platform.
    class CalendarSurface : public Widget
    {
    public:
        DatePickerWidget *owner = nullptr;

        void render(GraphicsContext &ctx, FontCache &fontCache) override
        {
            if (!owner || !owner->isOpen)
                return;
            owner->_computePopupSize();

#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
      if (IDomAdapter *adapter = getActiveDomAdapter())
      {
        owner->_renderCalendarDom(adapter, this);
        needsPaint = false;
        return;
      }
#endif


            Painter painter(ctx, this);
            int ox = x, oy = y;

            painter.fillRoundedRect(ox + owner->shadowOffset, oy + owner->shadowOffset,
                                    owner->popupW_, owner->popupH_,
                                    owner->calBorderRadius, Color::fromRGBA(0, 0, 0, 60));
            painter.fillRoundedRect(ox, oy, owner->popupW_, owner->popupH_,
                                    owner->calBorderRadius, owner->calBgColor);
            painter.drawBorder(ox, oy, owner->popupW_, owner->popupH_,
                               owner->calBorderRadius, owner->calBorderColor, 1);

            if (owner->showingYears)
                owner->_renderYearPicker(ctx, fontCache, ox, oy);
            else
                owner->_renderCalendarGrid(ctx, fontCache, ox, oy);
            needsPaint = false;
        }

        bool handleMouseDown(int mx, int my) override
        {
            if (!owner || !owner->isOpen)
                return false;
            int rx = mx - x, ry = my - y;

            if (rx >= 0 && rx < owner->popupW_ && ry >= 0 && ry < owner->popupH_)
            {
                if (owner->showingYears)
                    owner->_handleYearPickerClick(rx, ry);
                else
                    owner->_handleCalendarClick(rx, ry);
                return true;
            }
            owner->closeCalendar_();
            return true;
        }

        bool handleMouseMove(int mx, int my) override
        {
            if (!owner || !owner->isOpen)
                return false;
            int rx = mx - x, ry = my - y;

            int newHover = -1;
            if (rx >= 0 && rx < owner->popupW_ && ry >= 0 && ry < owner->popupH_)
            {
                newHover = owner->showingYears ? owner->_yearIndexAt(rx, ry)
                                               : owner->_dayIndexAt(rx, ry);
            }

            if (newHover != owner->hoveredCell_)
            {
                owner->hoveredCell_ = newHover;
                owner->refresh_();
                return true;
            }
            return false;
        }

        void onOverlayOutsideClick() override
        {
            if (owner)
                owner->closeCalendar_();
        }
    };

    std::shared_ptr<CalendarSurface> calendarSurface_;

    // ── Selection & navigation state ─────────────────────────────────────────
    FluxDate selectedDate; // currently selected date (may be invalid)
    int viewYear = 0;      // calendar grid is showing this year/month
    int viewMonth = 0;

    bool isOpen = false;
    bool showingYears = false; // year-picker overlay inside the popup

    // ── Appearance — trigger field ────────────────────────────────────────────
    std::string placeholder = "Select a date...";
    std::string dateFormat = "%d / %m / %Y";

    Color fieldBgColor = Color::fromRGB(255, 255, 255);
    Color fieldBorderColor = Color::fromRGB(180, 180, 180);
    Color fieldFocusBorder = Color::fromRGB(33, 150, 243);
    Color fieldTextColor = Color::fromRGB(30, 30, 30);
    Color placeholderColor = Color::fromRGB(160, 160, 160);
    int fieldFontSize = 13;

    // ── Appearance — calendar popup ───────────────────────────────────────────
    int calWidth = 280;
    int calHeight = 300; // auto-computed; exposed for override
    int calCellSize = 34;
    int calHeaderH = 40;
    int calWeekRowH = 24;
    int calPadH = 10;
    int calPadV = 8;
    int calBorderRadius = 8;
    int shadowOffset = 3;

    Color calBgColor = Color::fromRGB(255, 255, 255);
    Color calBorderColor = Color::fromRGB(200, 200, 200);
    Color headerBgColor = Color::fromRGB(33, 150, 243);
    Color headerTextColor = Color::fromRGB(255, 255, 255);
    Color weekdayTextColor = Color::fromRGB(120, 120, 120);
    Color dayTextColor = Color::fromRGB(30, 30, 30);
    Color dayHoverBg = Color::fromRGB(232, 245, 255);
    Color daySelectedBg = Color::fromRGB(33, 150, 243);
    Color daySelectedText = Color::fromRGB(255, 255, 255);
    Color todayBorderColor = Color::fromRGB(33, 150, 243);
    Color otherMonthText = Color::fromRGB(190, 190, 190);
    Color navArrowColor = Color::fromRGB(255, 255, 255);
    Color yearHoverBg = Color::fromRGB(232, 245, 255);
    Color yearSelectedBg = Color::fromRGB(33, 150, 243);
    Color yearSelectedText = Color::fromRGB(255, 255, 255);

    // ── Constraints ───────────────────────────────────────────────────────────
    FluxDate minDate; // invalid = no minimum
    FluxDate maxDate; // invalid = no maximum

    // ── Callback ─────────────────────────────────────────────────────────────
    std::function<void(FluxDate)> onDateChanged;

    // ─────────────────────────────────────────────────────────────────────────

    DatePickerWidget()
    {
        isFocusable = true;
        hasBorder = true;
        hasBackground = true;
        backgroundColor = fieldBgColor;
        borderColor = fieldBorderColor;
        borderWidth = 1;
        borderRadius = 4;
        height = 36;
        autoHeight = false;
        paddingLeft = 12;
        paddingRight = 36; // room for calendar icon
        paddingTop = paddingBottom = 8;

        FluxDate td = FluxDate::today();
        viewYear = td.year;
        viewMonth = td.month;

        calendarSurface_ = std::make_shared<CalendarSurface>();
        calendarSurface_->owner = this;
    }

    void onDetach() override
    {
        if (isOpen)
            closeCalendar_();
        Widget::onDetach();
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    void computeLayout(GraphicsContext & /*ctx*/,
                       const BoxConstraints &constraints, FontCache &) override
    {
        if (autoWidth)
            width = constraints.maxWidth;
        applyConstraints();
        needsLayout = false;
    }

    // ── Render the trigger field ──────────────────────────────────────────────
    void render(GraphicsContext &ctx, FontCache &fontCache) override
    {
        if (!visible)
            return;
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
      _renderFieldDom(adapter);
      needsPaint = false;
      return;
    }
#endif


        borderColor = isFocused ? fieldFocusBorder : fieldBorderColor;
        drawRoundedRectangle(ctx);

        Painter painter(ctx, this);
        NativeFont font = fontCache.getFont(fieldFontSize, FontWeight::Normal);

        if (selectedDate.isValid())
        {
            std::string label = selectedDate.toString(dateFormat);

            std::wstring wlabel = toWideString(label);
            painter.drawText(wlabel, x + paddingLeft, y,
                             width - paddingLeft - paddingRight, height, font,
                             fieldTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        else
        {

            std::wstring wph = toWideString(placeholder);
            painter.drawText(wph, x + paddingLeft, y,
                             width - paddingLeft - paddingRight, height, font,
                             placeholderColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        _drawCalendarIcon(ctx, x + width - 26, y + height / 2 - 8);
        needsPaint = false;
    }

    // ── Bar/trigger mouse events (normal widget-tree dispatch) ──────────────
    // Only ever sees the trigger field now — popup hit-testing moved to
    // CalendarSurface::handleMouseDown/handleMouseMove above. While the
    // popup is open, the overlay layer routes clicks/moves directly to
    // those handlers instead of here (modal entry, same as
    // DropdownWidget/ContextMenuWidget).

    bool handleMouseDown(int mx, int my) override
    {
        if (mx >= x && mx < x + width && my >= y && my < y + height)
        {
            if (isOpen)
                closeCalendar_();
            else
                openCalendar_();
            return true;
        }
        return false;
    }

    bool handleFocus(bool focused) override
    {
        isFocused = focused;
        if (!focused && isOpen)
            closeCalendar_();
        markNeedsPaint();
        return true;
    }

    // ── Fluent setters ────────────────────────────────────────────────────────

    std::shared_ptr<DatePickerWidget> setDate(const FluxDate &d)
    {
        selectedDate = d;
        if (d.isValid())
        {
            viewYear = d.year;
            viewMonth = d.month;
        }
        markNeedsPaint();
        return self_();
    }

    std::shared_ptr<DatePickerWidget> setDate(State<FluxDate> &state)
    {
        setDate(state.get());
        state.bindProperty(
            shared_from_this(),
            [](Widget *w, const FluxDate &d)
            {
                auto *dp = static_cast<DatePickerWidget *>(w);
                dp->selectedDate = d;
                if (d.isValid())
                {
                    dp->viewYear = d.year;
                    dp->viewMonth = d.month;
                }
                dp->markNeedsPaint();
            },
            false);
        boundState_ = &state;
        return self_();
    }

    std::shared_ptr<DatePickerWidget>
    setOnDateChanged(std::function<void(FluxDate)> cb)
    {
        onDateChanged = std::move(cb);
        return self_();
    }
    std::shared_ptr<DatePickerWidget> setPlaceholder(const std::string &p)
    {
        placeholder = p;
        markNeedsPaint();
        return self_();
    }
    std::shared_ptr<DatePickerWidget> setDateFormat(const std::string &f)
    {
        dateFormat = f;
        markNeedsPaint();
        return self_();
    }
    std::shared_ptr<DatePickerWidget> setMinDate(const FluxDate &d)
    {
        minDate = d;
        return self_();
    }
    std::shared_ptr<DatePickerWidget> setMaxDate(const FluxDate &d)
    {
        maxDate = d;
        return self_();
    }
    std::shared_ptr<DatePickerWidget> setWidth(int w)
    {
        width = w;
        autoWidth = false;
        markNeedsLayout();
        return self_();
    }
    std::shared_ptr<DatePickerWidget> setAccentColor(Color c)
    {
        headerBgColor = c;
        daySelectedBg = c;
        todayBorderColor = c;
        fieldFocusBorder = c;
        yearSelectedBg = c;
        markNeedsPaint();
        return self_();
    }

private:
    friend class CalendarSurface;
    State<FluxDate> *boundState_ = nullptr;

    int popupW_ = 0, popupH_ = 0;
    int hoveredCell_ = -1; // day index (0-41) or year index

    // ── Helpers ───────────────────────────────────────────────────────────────

    std::shared_ptr<DatePickerWidget> self_()
    {
        return std::static_pointer_cast<DatePickerWidget>(shared_from_this());
    }

    static int _daysInMonth(int year, int month)
    {
        static const int days[] = {0, 31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
        if (month == 2)
        {
            bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
            return leap ? 29 : 28;
        }
        return days[month];
    }

    // 0=Sun,1=Mon,...,6=Sat for first day of viewYear/viewMonth
    static int _firstWeekday(int year, int month)
    {
        std::tm t{};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = 1;
        std::mktime(&t);
        return t.tm_wday; // 0=Sun
    }

    void _computePopupSize()
    {
        int rows = 6; // max weeks per month
        popupW_ = calPadH * 2 + calCellSize * 7;
        popupH_ = calHeaderH + calWeekRowH + rows * calCellSize + calPadV * 2;
        calWidth = popupW_;
        calHeight = popupH_;
    }

    static const char *_monthName(int m)
    {
        static const char *names[] = {
            "", "January", "February", "March", "April",
            "May", "June", "July", "August", "September",
            "October", "November", "December"};
        return (m >= 1 && m <= 12) ? names[m] : "";
    }

    static const char *_weekdayShort(int d)
    {
        static const char *names[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
        return names[d % 7];
    }

    bool _isDisabled(int year, int month, int day) const
    {
        FluxDate d{year, month, day};
        if (minDate.isValid() && d < minDate)
            return true;
        if (maxDate.isValid() && maxDate < d)
            return true;
        return false;
    }

    // ── Popup open/close ──────────────────────────────────────────────────────

    void openCalendar_()
    {
        if (isOpen)
            return;

        auto *ui = FluxUI::getCurrentInstance();
        if (!ui)
            return;

        if (!selectedDate.isValid())
        {
            FluxDate td = FluxDate::today();
            viewYear = td.year;
            viewMonth = td.month;
        }
        isOpen = true;
        showingYears = false;
        hoveredCell_ = -1;

        _computePopupSize(); // size only — positioning/clamping is the manager's job now

        // Position below the field, in absolute client coordinates.
        calendarSurface_->x = x;
        calendarSurface_->y = y + height + 2;
        calendarSurface_->width = popupW_ + shadowOffset;
        calendarSurface_->height = popupH_ + shadowOffset;
        ui->showOverlay(calendarSurface_.get(), /*zIndex=*/100,
                        /*modal=*/true, /*blocksHoverBelow=*/false,
                        /*capturesKeyboard=*/true);

        markNeedsPaint();
    }

    void closeCalendar_()
    {
        if (!isOpen)
            return;
        isOpen = false;
        showingYears = false;
        hoveredCell_ = -1;
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->hideOverlay(calendarSurface_.get());
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    // Same reasoning as DropdownWidget::closeDropdown(): hideOverlay()
    // only stops FUTURE render() calls — it doesn't remove DOM nodes the
    // last successful render already attached under #flux-dom-root.
    fluxDomEvictWidget(calendarSurface_.get());
#endif

        markNeedsPaint();
    }

    void refresh_()
    {
        if (!isOpen)
            return;
        if (auto *ui = FluxUI::getCurrentInstance())
            ui->refreshOverlay(calendarSurface_.get());
    }

    // ── Calendar rendering ────────────────────────────────────────────────────

    void _renderCalendarGrid(GraphicsContext &ctx, FontCache &fontCache,
                             int ox, int oy)
    {

        FluxDate today = FluxDate::today();
        int firstWD = _firstWeekday(viewYear, viewMonth);
        int daysInMon = _daysInMonth(viewYear, viewMonth);
        int gridTop = calHeaderH + calPadV + calWeekRowH;
        Painter painter(ctx, this);

        // ── Header background ─────────────────────────────────────────────────
        painter.fillRect(ox, oy, popupW_, calHeaderH, headerBgColor);

        // Nav arrows
        _drawNavArrow(ctx, ox + calPadH + 8, oy + calHeaderH / 2, false);
        _drawNavArrow(ctx, ox + popupW_ - calPadH - 8, oy + calHeaderH / 2, true);

        // Month + Year label
        {
            std::string label =
                std::string(_monthName(viewMonth)) + "  " + std::to_string(viewYear);

            std::wstring wlabel = toWideString(label);
            NativeFont font = fontCache.getFont(14, FontWeight::Bold);
            painter.drawText(wlabel, ox + calPadH + 24, oy, popupW_ - (calPadH + 24) * 2,
                             calHeaderH, font, headerTextColor,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // ── Weekday row ───────────────────────────────────────────────────────
        NativeFont wdFont = fontCache.getFont(11, FontWeight::Normal);
        for (int col = 0; col < 7; col++)
        {
            int cx = ox + calPadH + col * calCellSize;

            std::wstring wwd = toWideString(_weekdayShort(col));
            painter.drawText(wwd, cx, oy + calHeaderH + calPadV, calCellSize, calWeekRowH,
                             wdFont, weekdayTextColor,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // ── Day cells ─────────────────────────────────────────────────────────
        NativeFont dayFont = fontCache.getFont(12, FontWeight::Normal);

        int prevDays = _daysInMonth(viewMonth == 1 ? viewYear - 1 : viewYear,
                                    viewMonth == 1 ? 12 : viewMonth - 1);

        for (int cell = 0; cell < 42; cell++)
        {
            int col = cell % 7;
            int row = cell / 7;
            int cx = ox + calPadH + col * calCellSize;
            int cy = oy + gridTop + row * calCellSize;

            int dayNum, dYear, dMonth;
            bool thisMonth;

            if (cell < firstWD)
            {
                dayNum = prevDays - firstWD + cell + 1;
                dMonth = viewMonth == 1 ? 12 : viewMonth - 1;
                dYear = viewMonth == 1 ? viewYear - 1 : viewYear;
                thisMonth = false;
            }
            else if (cell - firstWD < daysInMon)
            {
                dayNum = cell - firstWD + 1;
                dMonth = viewMonth;
                dYear = viewYear;
                thisMonth = true;
            }
            else
            {
                dayNum = cell - firstWD - daysInMon + 1;
                dMonth = viewMonth == 12 ? 1 : viewMonth + 1;
                dYear = viewMonth == 12 ? viewYear + 1 : viewYear;
                thisMonth = false;
            }

            bool isSelected = selectedDate.isValid() && selectedDate.year == dYear &&
                              selectedDate.month == dMonth &&
                              selectedDate.day == dayNum;
            bool isToday =
                today.year == dYear && today.month == dMonth && today.day == dayNum;
            // NOTE: this was commented out in the pre-migration source, which
            // left `isHovered` undefined below it (almost certainly a
            // pre-existing bug / non-compiling line, not something this
            // migration introduced). Restored here so hover highlighting on
            // day cells actually works.
            bool cellHovered = (cell == hoveredCell_) && thisMonth;
            bool isDisabled = _isDisabled(dYear, dMonth, dayNum);

            // Cell background
            if (isSelected)
                painter.fillRoundedRect(cx + 2, cy + 2, calCellSize - 4,
                                        calCellSize - 4, 4, daySelectedBg);
            else if (cellHovered && !isDisabled)
                painter.fillRoundedRect(cx + 2, cy + 2, calCellSize - 4,
                                        calCellSize - 4, 4, dayHoverBg);

            // Today ring
            if (isToday && !isSelected)
                painter.drawBorder(cx + 2, cy + 2, calCellSize - 4, calCellSize - 4, 4,
                                   todayBorderColor, 1);

            // Day number text
            Color textCol = isSelected   ? daySelectedText
                            : !thisMonth ? otherMonthText
                            : isDisabled ? otherMonthText
                                         : dayTextColor;

            std::string ds = std::to_string(dayNum);

            std::wstring wds = toWideString(ds);
            painter.drawText(wds, cx + 1, cy + 1, calCellSize - 2, calCellSize - 2,
                             dayFont, textCol,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // ── Year picker ───────────────────────────────────────────────────────────

    void _renderYearPicker(GraphicsContext &ctx, FontCache &fontCache,
                           int ox, int oy)
    {
        Painter painter(ctx, this);

        painter.fillRect(ox, oy, popupW_, calHeaderH, headerBgColor);
        _drawNavArrow(ctx, ox + calPadH + 8, oy + calHeaderH / 2, false);
        _drawNavArrow(ctx, ox + popupW_ - calPadH - 8, oy + calHeaderH / 2, true);

        {
            std::string range = std::to_string(yearRangeStart_) + " \xe2\x80\x93 " +
                                std::to_string(yearRangeStart_ + 11);

            std::wstring wrange = toWideString(range);
            NativeFont font = fontCache.getFont(14, FontWeight::Bold);
            painter.drawText(wrange, ox + calPadH + 24, oy, popupW_ - (calPadH + 24) * 2,
                             calHeaderH, font, headerTextColor,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        NativeFont yearFont = fontCache.getFont(12, FontWeight::Normal);
        int cellW = popupW_ / 4;
        int cellH = (popupH_ - calHeaderH) / 3;

        for (int i = 0; i < 12; i++)
        {
            int yr = yearRangeStart_ + i;
            int col = i % 4;
            int row = i / 4;
            int cx = ox + col * cellW;
            int cy = oy + calHeaderH + row * cellH;

            if (yr == viewYear)
                painter.fillRoundedRect(cx + 4, cy + 4, cellW - 8, cellH - 8, 4,
                                        yearSelectedBg);
            else if (i == hoveredCell_)
                painter.fillRoundedRect(cx + 4, cy + 4, cellW - 8, cellH - 8, 4,
                                        yearHoverBg);

            Color textCol = (yr == viewYear) ? yearSelectedText : dayTextColor;
            std::string ys = std::to_string(yr);

            std::wstring wys = toWideString(ys);
            painter.drawText(wys, cx, cy, cellW, cellH, yearFont, textCol,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // ── Click handlers (popup-local coordinates) ─────────────────────────────

    void _handleCalendarClick(int rx, int ry)
    {
        // Nav arrows
        bool leftArrow =
            (rx >= calPadH && rx < calPadH + 24 && ry >= 0 && ry < calHeaderH);
        bool rightArrow = (rx >= popupW_ - calPadH - 24 && rx < popupW_ &&
                           ry >= 0 && ry < calHeaderH);
        bool headerClick = (!leftArrow && !rightArrow && ry < calHeaderH);

        if (leftArrow)
        {
            viewMonth--;
            if (viewMonth < 1)
            {
                viewMonth = 12;
                viewYear--;
            }
            hoveredCell_ = -1;
            refresh_();
            return;
        }
        if (rightArrow)
        {
            viewMonth++;
            if (viewMonth > 12)
            {
                viewMonth = 1;
                viewYear++;
            }
            hoveredCell_ = -1;
            refresh_();
            return;
        }
        if (headerClick)
        {
            // Open year picker
            yearRangeStart_ = (viewYear / 12) * 12;
            showingYears = true;
            hoveredCell_ = -1;
            refresh_();
            return;
        }

        // Day cell
        int cell = _dayIndexAt(rx, ry);
        if (cell < 0)
            return;

        int firstWD = _firstWeekday(viewYear, viewMonth);
        int daysInMon = _daysInMonth(viewYear, viewMonth);

        int dayNum, dYear, dMonth;
        int prevDays = _daysInMonth(viewMonth == 1 ? viewYear - 1 : viewYear,
                                    viewMonth == 1 ? 12 : viewMonth - 1);

        if (cell < firstWD)
        {
            dayNum = prevDays - firstWD + cell + 1;
            dMonth = viewMonth == 1 ? 12 : viewMonth - 1;
            dYear = viewMonth == 1 ? viewYear - 1 : viewYear;
        }
        else if (cell - firstWD < daysInMon)
        {
            dayNum = cell - firstWD + 1;
            dMonth = viewMonth;
            dYear = viewYear;
        }
        else
        {
            dayNum = cell - firstWD - daysInMon + 1;
            dMonth = viewMonth == 12 ? 1 : viewMonth + 1;
            dYear = viewMonth == 12 ? viewYear + 1 : viewYear;
        }

        if (_isDisabled(dYear, dMonth, dayNum))
            return;

        FluxDate newDate{dYear, dMonth, dayNum};
        selectedDate = newDate;
        viewYear = dYear;
        viewMonth = dMonth;

        if (boundState_)
            boundState_->set(newDate);
        if (onDateChanged)
            onDateChanged(newDate);

        markNeedsPaint();
        closeCalendar_();
    }

    void _handleYearPickerClick(int rx, int ry)
    {
        bool leftArrow =
            (rx >= calPadH && rx < calPadH + 24 && ry >= 0 && ry < calHeaderH);
        bool rightArrow = (rx >= popupW_ - calPadH - 24 && rx < popupW_ &&
                           ry >= 0 && ry < calHeaderH);

        if (leftArrow)
        {
            yearRangeStart_ -= 12;
            hoveredCell_ = -1;
            refresh_();
            return;
        }
        if (rightArrow)
        {
            yearRangeStart_ += 12;
            hoveredCell_ = -1;
            refresh_();
            return;
        }
        if (ry < calHeaderH)
            return;

        int i = _yearIndexAt(rx, ry);
        if (i < 0 || i >= 12)
            return;

        viewYear = yearRangeStart_ + i;
        showingYears = false;
        hoveredCell_ = -1;
        refresh_();
    }

    // ── Hit testing (popup-local coordinates) ────────────────────────────────

    // Returns 0-41 for the cell under (rx,ry) in the calendar grid, else -1
    int _dayIndexAt(int rx, int ry) const
    {
        int gridTop = calHeaderH + calPadV + calWeekRowH;
        if (ry < gridTop)
            return -1;
        int col = (rx - calPadH) / calCellSize;
        int row = (ry - gridTop) / calCellSize;
        if (col < 0 || col >= 7 || row < 0 || row >= 6)
            return -1;
        return row * 7 + col;
    }

    // Returns 0-11 for the year cell under (rx,ry), else -1
    int _yearIndexAt(int rx, int ry) const
    {
        if (ry < calHeaderH)
            return -1;
        int cellW = popupW_ / 4;
        int cellH = (popupH_ - calHeaderH) / 3;
        int col = rx / cellW;
        int row = (ry - calHeaderH) / cellH;
        if (col < 0 || col >= 4 || row < 0 || row >= 3)
            return -1;
        return row * 4 + col;
    }

    // ── Drawing helpers ───────────────────────────────────────────────────────

    void _drawNavArrow(GraphicsContext &ctx, int cx, int cy, bool isRight)
    {
        Painter painter(ctx, this);
        int s = 5;
        if (isRight)
        {
            painter.drawLine(cx - s, cy - s, cx + s, cy, navArrowColor, 2);
            painter.drawLine(cx + s, cy, cx - s, cy + s, navArrowColor, 2);
        }
        else
        {
            painter.drawLine(cx + s, cy - s, cx - s, cy, navArrowColor, 2);
            painter.drawLine(cx - s, cy, cx + s, cy + s, navArrowColor, 2);
        }
    }

    void _drawCalendarIcon(GraphicsContext &ctx, int cx, int cy) 
    {
        Painter painter(ctx, this);
        Color iconColor = Color::fromRGB(140, 140, 140);
        // Outer rect outline
        painter.drawRectOutline(cx, cy + 2, 16, 14, iconColor, 1);
        // Top bar fill
        painter.fillRect(cx, cy + 2, 16, 4, iconColor);
        // 2×3 dot grid — each dot is a 1×1 fillRect
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++)
                painter.fillRect(cx + 3 + c * 4, cy + 8 + r * 4, 1, 1, iconColor);
    }
#if (defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)) || defined(FLUX_SSR)
    // ── Trigger field, DOM path ────────────────────────────────────────────
    // Three slots under one owner: the box (background+border), the label
    // text, and the calendar icon glyph — same "layers as slots" split
    // DropdownWidget's own _renderDom uses for its box/label/arrow.
    void _renderFieldDom(IDomAdapter *adapter)
    {
        char buf[24];
        auto px = [&](int v)
        { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
        char colbuf[32];
        auto rgb = [&](Color c)
        { snprintf(colbuf, sizeof(colbuf), "rgb(%d,%d,%d)", c.r, c.g, c.b); return std::string(colbuf); };

        // ── Box ─────────────────────────────────────────────────────────────
        DomNodeHandle box = fluxDomEnsureNode(this, "div", "box");
        fluxDomApplyRect(this, x, y, width, height, "box");
        adapter->setStyle(box, "background-color", rgb(fieldBgColor));
        adapter->setStyle(box, "border", "1px solid " + rgb(isFocused ? fieldFocusBorder : fieldBorderColor));
        adapter->setStyle(box, "border-radius", px(borderRadius));
        adapter->setStyle(box, "box-sizing", "border-box");
        adapter->setStyle(box, "pointer-events", "none");

        // ── Label ───────────────────────────────────────────────────────────
        bool hasDate = selectedDate.isValid();
        std::string label = hasDate ? selectedDate.toString(dateFormat) : placeholder;
        DomNodeHandle labelNode = fluxDomEnsureNode(this, "div", "label");
        fluxDomApplyRect(this, x + paddingLeft, y,
                         width - paddingLeft - paddingRight, height, "label");
        adapter->setStyle(labelNode, "display", "flex");
        adapter->setStyle(labelNode, "align-items", "center");
        adapter->setStyle(labelNode, "white-space", "nowrap");
        adapter->setStyle(labelNode, "overflow", "hidden");
        adapter->setStyle(labelNode, "text-overflow", "ellipsis");
        adapter->setStyle(labelNode, "font-size", px(fieldFontSize));
        adapter->setStyle(labelNode, "color", rgb(hasDate ? fieldTextColor : placeholderColor));
        adapter->setStyle(labelNode, "pointer-events", "none");
        adapter->setText(labelNode, label);

        // ── Calendar icon — unicode glyph, same trick DropdownWidget uses
        // for its arrow, in place of the hand-drawn rect+dot-grid path.
        DomNodeHandle icon = fluxDomEnsureNode(this, "div", "icon");
        fluxDomApplyRect(this, x + width - 26, y + height / 2 - 8, 16, 16, "icon");
        adapter->setStyle(icon, "display", "flex");
        adapter->setStyle(icon, "align-items", "center");
        adapter->setStyle(icon, "justify-content", "center");
        adapter->setStyle(icon, "font-size", px(14));
        adapter->setStyle(icon, "color", rgb(Color::fromRGB(140, 140, 140)));
        adapter->setStyle(icon, "pointer-events", "none");
        // "\xF0\x9F\x93\x85" = 📅 (U+1F4C5)
        adapter->setText(icon, "\xF0\x9F\x93\x85");
    }

    // ── Popup, DOM path ─────────────────────────────────────────────────────
    // Fixed slot budget, same pre-allocate-and-hide pattern as
    // DropdownWidget::ListSurface::_renderDom: 7 weekday cells + 42 day
    // cells for the calendar grid, 12 year cells for the year picker —
    // whichever grid ISN'T showing just gets display:none for this frame
    // instead of having its nodes created/evicted.
    void _renderCalendarDom(IDomAdapter *adapter, Widget *surface)
    {
        char buf[24];
        auto px = [&](int v)
        { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
        char colbuf[32];
        auto rgb = [&](Color c)
        { snprintf(colbuf, sizeof(colbuf), "rgb(%d,%d,%d)", c.r, c.g, c.b); return std::string(colbuf); };

        int ox = surface->x, oy = surface->y;

        // ── Popup background + border ────────────────────────────────────
        DomNodeHandle bg = fluxDomEnsureNode(surface, "div", "bg");
        fluxDomApplyRect(surface, ox, oy, popupW_, popupH_, "bg");
        adapter->setStyle(bg, "background-color", rgb(calBgColor));
        adapter->setStyle(bg, "border", "1px solid " + rgb(calBorderColor));
        adapter->setStyle(bg, "border-radius", px(calBorderRadius));
        adapter->setStyle(bg, "box-shadow",
                          px(shadowOffset) + " " + px(shadowOffset) + " 6px rgba(0,0,0,0.24)");
        adapter->setStyle(bg, "box-sizing", "border-box");
        adapter->setStyle(bg, "pointer-events", "none");
        adapter->setStyle(bg, "z-index", "10");

        // ── Header bar ────────────────────────────────────────────────────
        DomNodeHandle header = fluxDomEnsureNode(surface, "div", "header");
        fluxDomApplyRect(surface, ox, oy, popupW_, calHeaderH, "header");
        adapter->setStyle(header, "background-color", rgb(headerBgColor));
        adapter->setStyle(header, "border-top-left-radius", px(calBorderRadius));
        adapter->setStyle(header, "border-top-right-radius", px(calBorderRadius));
        adapter->setStyle(header, "pointer-events", "none");
        adapter->setStyle(header, "z-index", "11");

        auto styleArrow = [&](DomNodeHandle n, int cx, int cy, bool right, const char *slot)
        {
            fluxDomApplyRect(surface, cx - 10, cy - 10, 20, 20, slot);
            adapter->setStyle(n, "display", "flex");
            adapter->setStyle(n, "align-items", "center");
            adapter->setStyle(n, "justify-content", "center");
            adapter->setStyle(n, "font-size", px(14));
            adapter->setStyle(n, "color", rgb(navArrowColor));
            adapter->setStyle(n, "pointer-events", "none");
            adapter->setStyle(n, "z-index", "12");
            // "\xE2\x97\x80" = ◀   "\xE2\x96\xB6" = ▶
            adapter->setText(n, right ? "\xE2\x96\xB6" : "\xE2\x97\x80");
        };
        DomNodeHandle navLeft = fluxDomEnsureNode(surface, "div", "nav-left");
        styleArrow(navLeft, ox + calPadH + 8, oy + calHeaderH / 2, false, "nav-left");
        DomNodeHandle navRight = fluxDomEnsureNode(surface, "div", "nav-right");
        styleArrow(navRight, ox + popupW_ - calPadH - 8, oy + calHeaderH / 2, true, "nav-right");

        // ── Header label (month/year, or year-range) ──────────────────────
        DomNodeHandle headerLabel = fluxDomEnsureNode(surface, "div", "header-label");
        fluxDomApplyRect(surface, ox + calPadH + 24, oy,
                         popupW_ - (calPadH + 24) * 2, calHeaderH, "header-label");
        adapter->setStyle(headerLabel, "display", "flex");
        adapter->setStyle(headerLabel, "align-items", "center");
        adapter->setStyle(headerLabel, "justify-content", "center");
        adapter->setStyle(headerLabel, "font-size", px(14));
        adapter->setStyle(headerLabel, "font-weight", "bold");
        adapter->setStyle(headerLabel, "color", rgb(headerTextColor));
        adapter->setStyle(headerLabel, "white-space", "nowrap");
        adapter->setStyle(headerLabel, "pointer-events", "none");
        adapter->setStyle(headerLabel, "z-index", "12");

        if (showingYears)
        {
            adapter->setText(headerLabel,
                             std::to_string(yearRangeStart_) + " \xe2\x80\x93 " +
                                 std::to_string(yearRangeStart_ + 11));

            // Hide the day-grid slots (weekday row + 42 cells) this frame.
            for (int col = 0; col < 7; ++col)
                adapter->setStyle(fluxDomEnsureNode(surface, "div", ("wd" + std::to_string(col)).c_str()),
                                  "display", "none");
            for (int i = 0; i < 42; ++i)
                adapter->setStyle(fluxDomEnsureNode(surface, "div", ("day" + std::to_string(i)).c_str()),
                                  "display", "none");

            int cellW = popupW_ / 4;
            int cellH = (popupH_ - calHeaderH) / 3;
            for (int i = 0; i < 12; ++i)
            {
                std::string slot = "year" + std::to_string(i);
                DomNodeHandle cell = fluxDomEnsureNode(surface, "div", slot.c_str());
                int col = i % 4, row = i / 4;
                int cx = ox + col * cellW, cy = oy + calHeaderH + row * cellH;
                fluxDomApplyRect(surface, cx + 4, cy + 4, cellW - 8, cellH - 8, slot.c_str());

                int yr = yearRangeStart_ + i;
                adapter->setStyle(cell, "display", "flex");
                adapter->setStyle(cell, "align-items", "center");
                adapter->setStyle(cell, "justify-content", "center");
                adapter->setStyle(cell, "border-radius", "4px");
                if (yr == viewYear)
                    adapter->setStyle(cell, "background-color", rgb(yearSelectedBg));
                else if (i == hoveredCell_)
                    adapter->setStyle(cell, "background-color", rgb(yearHoverBg));
                else
                    adapter->setStyle(cell, "background-color", "transparent");
                adapter->setStyle(cell, "font-size", px(12));
                adapter->setStyle(cell, "color", rgb(yr == viewYear ? yearSelectedText : dayTextColor));
                adapter->setStyle(cell, "pointer-events", "none");
                adapter->setStyle(cell, "z-index", "11");
                adapter->setText(cell, std::to_string(yr));
            }
        }
        else
        {
            adapter->setText(headerLabel,
                             std::string(_monthName(viewMonth)) + "  " + std::to_string(viewYear));

            // Hide the 12 year-grid slots this frame.
            for (int i = 0; i < 12; ++i)
                adapter->setStyle(fluxDomEnsureNode(surface, "div", ("year" + std::to_string(i)).c_str()),
                                  "display", "none");

            // ── Weekday row ─────────────────────────────────────────────────
            for (int col = 0; col < 7; ++col)
            {
                std::string slot = "wd" + std::to_string(col);
                DomNodeHandle wd = fluxDomEnsureNode(surface, "div", slot.c_str());
                int cx = ox + calPadH + col * calCellSize;
                fluxDomApplyRect(surface, cx, oy + calHeaderH + calPadV, calCellSize, calWeekRowH, slot.c_str());
                adapter->setStyle(wd, "display", "flex");
                adapter->setStyle(wd, "align-items", "center");
                adapter->setStyle(wd, "justify-content", "center");
                adapter->setStyle(wd, "font-size", px(11));
                adapter->setStyle(wd, "color", rgb(weekdayTextColor));
                adapter->setStyle(wd, "pointer-events", "none");
                adapter->setStyle(wd, "z-index", "11");
                adapter->setText(wd, _weekdayShort(col));
            }

            // ── Day cells ───────────────────────────────────────────────────
            int firstWD = _firstWeekday(viewYear, viewMonth);
            int daysInMon = _daysInMonth(viewYear, viewMonth);
            int prevDays = _daysInMonth(viewMonth == 1 ? viewYear - 1 : viewYear,
                                        viewMonth == 1 ? 12 : viewMonth - 1);
            FluxDate today = FluxDate::today();
            int gridTop = calHeaderH + calPadV + calWeekRowH;

            for (int cell = 0; cell < 42; ++cell)
            {
                std::string slot = "day" + std::to_string(cell);
                DomNodeHandle d = fluxDomEnsureNode(surface, "div", slot.c_str());

                int col = cell % 7, row = cell / 7;
                int cx = ox + calPadH + col * calCellSize;
                int cy = oy + gridTop + row * calCellSize;
                fluxDomApplyRect(surface, cx + 2, cy + 2, calCellSize - 4, calCellSize - 4, slot.c_str());

                int dayNum, dYear, dMonth;
                bool thisMonth;
                if (cell < firstWD)
                {
                    dayNum = prevDays - firstWD + cell + 1;
                    dMonth = viewMonth == 1 ? 12 : viewMonth - 1;
                    dYear = viewMonth == 1 ? viewYear - 1 : viewYear;
                    thisMonth = false;
                }
                else if (cell - firstWD < daysInMon)
                {
                    dayNum = cell - firstWD + 1;
                    dMonth = viewMonth;
                    dYear = viewYear;
                    thisMonth = true;
                }
                else
                {
                    dayNum = cell - firstWD - daysInMon + 1;
                    dMonth = viewMonth == 12 ? 1 : viewMonth + 1;
                    dYear = viewMonth == 12 ? viewYear + 1 : viewYear;
                    thisMonth = false;
                }

                bool isSelected = selectedDate.isValid() && selectedDate.year == dYear &&
                                  selectedDate.month == dMonth && selectedDate.day == dayNum;
                bool isToday = today.year == dYear && today.month == dMonth && today.day == dayNum;
                bool isHovered = (cell == hoveredCell_) && thisMonth;
                bool isDisabled = _isDisabled(dYear, dMonth, dayNum);

                Color textCol = isSelected   ? daySelectedText
                                : !thisMonth ? otherMonthText
                                : isDisabled ? otherMonthText
                                             : dayTextColor;

                adapter->setStyle(d, "display", "flex");
                adapter->setStyle(d, "align-items", "center");
                adapter->setStyle(d, "justify-content", "center");
                adapter->setStyle(d, "border-radius", "4px");
                if (isSelected)
                    adapter->setStyle(d, "background-color", rgb(daySelectedBg));
                else if (isHovered && !isDisabled)
                    adapter->setStyle(d, "background-color", rgb(dayHoverBg));
                else
                    adapter->setStyle(d, "background-color", "transparent");
                adapter->setStyle(d, "border", (isToday && !isSelected) ? ("1px solid " + rgb(todayBorderColor)) : "none");
                adapter->setStyle(d, "box-sizing", "border-box");
                adapter->setStyle(d, "font-size", px(12));
                adapter->setStyle(d, "color", rgb(textCol));
                adapter->setStyle(d, "pointer-events", "none");
                adapter->setStyle(d, "z-index", "11");
                adapter->setText(d, std::to_string(dayNum));
            }
        }
    }
#endif

    int yearRangeStart_ = 2020;
};

// ============================================================================
// FACTORY
// ============================================================================

using DatePickerWidgetPtr = std::shared_ptr<DatePickerWidget>;

inline DatePickerWidgetPtr DatePicker()
{
    return std::make_shared<DatePickerWidget>();
}

#endif // FLUX_DATE_PICKER_HPP