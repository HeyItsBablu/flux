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
  int year = 0;
  int month = 0; // 1-12
  int day = 0;   // 1-31

  bool isValid() const {
    return year > 0 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
  }

  bool operator==(const FluxDate &o) const {
    return year == o.year && month == o.month && day == o.day;
  }
  bool operator!=(const FluxDate &o) const { return !(*this == o); }
  bool operator<(const FluxDate &o) const {
    if (year != o.year)
      return year < o.year;
    if (month != o.month)
      return month < o.month;
    return day < o.day;
  }

  std::string toString(const std::string &fmt = "%Y-%m-%d") const {
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
  FluxDate selectedDate; // currently selected date (may be invalid)
  int viewYear = 0;      // calendar grid is showing this year/month
  int viewMonth = 0;

  bool isOpen = false;
  bool showingYears = false; // year-picker overlay inside the popup

  // ── Appearance — trigger field ────────────────────────────────────────────
  std::string placeholder = "Select a date...";
  std::string dateFormat = "%d / %m / %Y";

  COLORREF fieldBgColor = RGB(255, 255, 255);
  COLORREF fieldBorderColor = RGB(180, 180, 180);
  COLORREF fieldFocusBorder = RGB(33, 150, 243);
  COLORREF fieldTextColor = RGB(30, 30, 30);
  COLORREF placeholderColor = RGB(160, 160, 160);
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

  COLORREF calBgColor = RGB(255, 255, 255);
  COLORREF calBorderColor = RGB(200, 200, 200);
  COLORREF headerBgColor = RGB(33, 150, 243);
  COLORREF headerTextColor = RGB(255, 255, 255);
  COLORREF weekdayTextColor = RGB(120, 120, 120);
  COLORREF dayTextColor = RGB(30, 30, 30);
  COLORREF dayHoverBg = RGB(232, 245, 255);
  COLORREF daySelectedBg = RGB(33, 150, 243);
  COLORREF daySelectedText = RGB(255, 255, 255);
  COLORREF todayBorderColor = RGB(33, 150, 243);
  COLORREF otherMonthText = RGB(190, 190, 190);
  COLORREF navArrowColor = RGB(255, 255, 255);
  COLORREF yearHoverBg = RGB(232, 245, 255);
  COLORREF yearSelectedBg = RGB(33, 150, 243);
  COLORREF yearSelectedText = RGB(255, 255, 255);

  // ── Constraints ───────────────────────────────────────────────────────────
  FluxDate minDate; // invalid = no minimum
  FluxDate maxDate; // invalid = no maximum

  // ── Callback ─────────────────────────────────────────────────────────────
  std::function<void(FluxDate)> onDateChanged;

  // ─────────────────────────────────────────────────────────────────────────

  DatePickerWidget() {
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
  }

  void setScaffold(ScaffoldWidget *s) override { scaffold_ = s; }

  void onDetach() override {
    if (isOpen)
      closeCalendar_();
    Widget::onDetach();
  }

  // ── Layout ────────────────────────────────────────────────────────────────
  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints, FontCache &) override {
    if (autoWidth)
      width = constraints.maxWidth;
    applyConstraints();
    needsLayout = false;
  }

  // ── Render the trigger field ──────────────────────────────────────────────
  void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!visible)
      return;

    borderColor = isFocused ? fieldFocusBorder : fieldBorderColor;
    drawRoundedRectangle(ctx);

    Painter painter(ctx);
    NativeFont font = fontCache.getFont(fieldFontSize, FontWeight::Normal);

    if (selectedDate.isValid()) {
      std::string label = selectedDate.toString(dateFormat);
      int wlen = MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, nullptr, 0);
      std::wstring wlabel(wlen, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, wlabel.data(), wlen);
      painter.drawText(wlabel, x + paddingLeft, y,
                       width - paddingLeft - paddingRight, height, font,
                       fieldTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
      int wlen =
          MultiByteToWideChar(CP_UTF8, 0, placeholder.c_str(), -1, nullptr, 0);
      std::wstring wph(wlen, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, placeholder.c_str(), -1, wph.data(),
                          wlen);
      painter.drawText(wph, x + paddingLeft, y,
                       width - paddingLeft - paddingRight, height, font,
                       placeholderColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    _drawCalendarIcon(ctx, x + width - 26, y + height / 2 - 8);
    needsPaint = false;
  }

  // ── renderPopupContent ────────────────────────────────────────────────────
  void renderPopupContent(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isOpen)
      return;
    _computePopupSize();

    Painter painter(ctx);

    // Shadow — filled rounded rect offset by shadowOffset
    painter.fillRoundedRect(shadowOffset, shadowOffset, popupW_, popupH_,
                            calBorderRadius, RGB(0, 0, 0),
                            60); // alpha 60 ≈ 24%

    // Background
    painter.fillRoundedRect(0, 0, popupW_, popupH_, calBorderRadius, calBgColor,
                            255);
    painter.drawBorder(0, 0, popupW_, popupH_, calBorderRadius, calBorderColor,
                       1, 255);

    if (showingYears)
      _renderYearPicker(ctx, fontCache);
    else
      _renderCalendarGrid(ctx, fontCache);
  }

  // ── Mouse events ──────────────────────────────────────────────────────────

bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
        if (isOpen) closeCalendar_();
        else        openCalendar_();
        return true;
    }

    if (!isOpen) return false;

    // Convert stored screen origin to current client coords
    auto origin = FluxUI::getCurrentInstance()
                      ->screenToClient(popupScreenX_, popupScreenY_);
    int rx = mx - origin.x;
    int ry = my - origin.y;

    if (rx >= 0 && rx < popupW_ && ry >= 0 && ry < popupH_) {
        if (showingYears) _handleYearPickerClick(rx, ry);
        else              _handleCalendarClick(rx, ry);
        return true;
    }

    closeCalendar_();
    return true;
}

bool handleMouseMove(int mx, int my) override {
    if (!isOpen) return false;

    auto origin = FluxUI::getCurrentInstance()
                      ->screenToClient(popupScreenX_, popupScreenY_);
    int rx = mx - origin.x;
    int ry = my - origin.y;

    int newHover = -1;
    if (rx >= 0 && rx < popupW_ && ry >= 0 && ry < popupH_) {
        if (showingYears) newHover = _yearIndexAt(rx, ry);
        else              newHover = _dayIndexAt(rx, ry);
    }

    if (newHover != hoveredCell_) {
        hoveredCell_ = newHover;
        refresh_();
    }
    return false;
}

  bool handleFocus(bool focused) override {
    isFocused = focused;
    if (!focused && isOpen)
      closeCalendar_();
    markNeedsPaint();
    return true;
  }

  // ── Fluent setters ────────────────────────────────────────────────────────

  std::shared_ptr<DatePickerWidget> setDate(const FluxDate &d) {
    selectedDate = d;
    if (d.isValid()) {
      viewYear = d.year;
      viewMonth = d.month;
    }
    markNeedsPaint();
    return self_();
  }

  std::shared_ptr<DatePickerWidget> setDate(State<FluxDate> &state) {
    setDate(state.get());
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const FluxDate &d) {
          auto *dp = static_cast<DatePickerWidget *>(w);
          dp->selectedDate = d;
          if (d.isValid()) {
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
  setOnDateChanged(std::function<void(FluxDate)> cb) {
    onDateChanged = std::move(cb);
    return self_();
  }
  std::shared_ptr<DatePickerWidget> setPlaceholder(const std::string &p) {
    placeholder = p;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<DatePickerWidget> setDateFormat(const std::string &f) {
    dateFormat = f;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<DatePickerWidget> setMinDate(const FluxDate &d) {
    minDate = d;
    return self_();
  }
  std::shared_ptr<DatePickerWidget> setMaxDate(const FluxDate &d) {
    maxDate = d;
    return self_();
  }
  std::shared_ptr<DatePickerWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<DatePickerWidget> setAccentColor(COLORREF c) {
    headerBgColor = c;
    daySelectedBg = c;
    todayBorderColor = c;
    fieldFocusBorder = c;
    yearSelectedBg = c;
    markNeedsPaint();
    return self_();
  }

private:
  ScaffoldWidget *scaffold_ = nullptr;
  State<FluxDate> *boundState_ = nullptr;

  int popupScreenX_ = 0, popupScreenY_ = 0;
  int popupW_ = 0, popupH_ = 0;
  int hoveredCell_ = -1; // day index (0-41) or year index

  // ── Helpers ───────────────────────────────────────────────────────────────

  std::shared_ptr<DatePickerWidget> self_() {
    return std::static_pointer_cast<DatePickerWidget>(shared_from_this());
  }

  static int _daysInMonth(int year, int month) {
    static const int days[] = {0,  31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
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
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    std::mktime(&t);
    return t.tm_wday; // 0=Sun
  }

  void _computePopupSize() {
    int rows = 6; // max weeks per month
    popupW_ = calPadH * 2 + calCellSize * 7;
    popupH_ = calHeaderH + calWeekRowH + rows * calCellSize + calPadV * 2;
    calWidth = popupW_;
    calHeight = popupH_;
  }

  static const char *_monthName(int m) {
    static const char *names[] = {
        "",        "January",  "February", "March",  "April",
        "May",     "June",     "July",     "August", "September",
        "October", "November", "December"};
    return (m >= 1 && m <= 12) ? names[m] : "";
  }

  static const char *_weekdayShort(int d) {
    static const char *names[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    return names[d % 7];
  }

  bool _isDisabled(int year, int month, int day) const {
    FluxDate d{year, month, day};
    if (minDate.isValid() && d < minDate)
      return true;
    if (maxDate.isValid() && maxDate < d)
      return true;
    return false;
  }

  // ── Popup open/close ──────────────────────────────────────────────────────

void openCalendar_() {
    if (isOpen) return;
    if (!selectedDate.isValid()) {
        FluxDate td = FluxDate::today();
        viewYear  = td.year;
        viewMonth = td.month;
    }
    isOpen       = true;
    showingYears = false;
    hoveredCell_ = -1;

    _computePopupSize();

    NativeWindow hw = FluxUI::getCurrentInstance()->getWindow();
    if (hw) {
        // Position below the field using FluxUI::clientToScreen
        auto sc = FluxUI::getCurrentInstance()->clientToScreen(x, y + height + 2);
        popupScreenX_ = sc.x;
        popupScreenY_ = sc.y;

        // Clamp to monitor
        POINT pt = {popupScreenX_, popupScreenY_};
        HMONITOR    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            if (popupScreenX_ + popupW_ > mi.rcWork.right)
                popupScreenX_ = mi.rcWork.right - popupW_;
            if (popupScreenY_ + popupH_ > mi.rcWork.bottom) {
                auto above = FluxUI::getCurrentInstance()
                                 ->clientToScreen(x, y - popupH_ - 2);
                popupScreenY_ = above.y;
            }
        }

        FontCache &fc = FluxUI::getCurrentInstance()->getFontCache();
        showPopup(hw, popupScreenX_, popupScreenY_,
                  popupW_ + shadowOffset, popupH_ + shadowOffset, fc);
    }

    if (scaffold_)
        scaffold_->addOverlay(this,
            [](GraphicsContext &, FontCache &) {}, 100);  // fix HDC → GraphicsContext

    markNeedsPaint();
}

  void closeCalendar_() {
    if (!isOpen)
      return;
    isOpen = false;
    showingYears = false;
    hoveredCell_ = -1;
    hidePopup();
    if (scaffold_)
      scaffold_->removeOverlay(this);
    markNeedsPaint();
  }

  void refresh_() {
    if (!isOpen || !popupVisible())
      return;
    if (auto *ui = FluxUI::getCurrentInstance())
      refreshPopup(ui->getFontCache());
  }

  // ── Calendar rendering ────────────────────────────────────────────────────

  void _renderCalendarGrid(GraphicsContext &ctx, FontCache &fontCache) {
    FluxDate today = FluxDate::today();
    int firstWD = _firstWeekday(viewYear, viewMonth);
    int daysInMon = _daysInMonth(viewYear, viewMonth);
    int gridTop = calHeaderH + calPadV + calWeekRowH;
    Painter painter(ctx);

    // ── Header background ─────────────────────────────────────────────────
    painter.fillRect(0, 0, popupW_, calHeaderH, headerBgColor);

    // Nav arrows
    _drawNavArrow(ctx, calPadH + 8, calHeaderH / 2, false);
    _drawNavArrow(ctx, popupW_ - calPadH - 8, calHeaderH / 2, true);

    // Month + Year label
    {
      std::string label =
          std::string(_monthName(viewMonth)) + "  " + std::to_string(viewYear);
      int wlen = MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, nullptr, 0);
      std::wstring wlabel(wlen, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, wlabel.data(), wlen);
      NativeFont font = fontCache.getFont(14, FontWeight::Bold);
      painter.drawText(wlabel, calPadH + 24, 0, popupW_ - (calPadH + 24) * 2,
                       calHeaderH, font, headerTextColor,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ── Weekday row ───────────────────────────────────────────────────────
    NativeFont wdFont = fontCache.getFont(11, FontWeight::Normal);
    for (int col = 0; col < 7; col++) {
      int cx = calPadH + col * calCellSize;
      int wlen =
          MultiByteToWideChar(CP_UTF8, 0, _weekdayShort(col), -1, nullptr, 0);
      std::wstring wwd(wlen, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, _weekdayShort(col), -1, wwd.data(), wlen);
      painter.drawText(wwd, cx, calHeaderH + calPadV, calCellSize, calWeekRowH,
                       wdFont, weekdayTextColor,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ── Day cells ─────────────────────────────────────────────────────────
    NativeFont dayFont = fontCache.getFont(12, FontWeight::Normal);

    int prevDays = _daysInMonth(viewMonth == 1 ? viewYear - 1 : viewYear,
                                viewMonth == 1 ? 12 : viewMonth - 1);

    for (int cell = 0; cell < 42; cell++) {
      int col = cell % 7;
      int row = cell / 7;
      int cx = calPadH + col * calCellSize;
      int cy = gridTop + row * calCellSize;

      int dayNum, dYear, dMonth;
      bool thisMonth;

      if (cell < firstWD) {
        dayNum = prevDays - firstWD + cell + 1;
        dMonth = viewMonth == 1 ? 12 : viewMonth - 1;
        dYear = viewMonth == 1 ? viewYear - 1 : viewYear;
        thisMonth = false;
      } else if (cell - firstWD < daysInMon) {
        dayNum = cell - firstWD + 1;
        dMonth = viewMonth;
        dYear = viewYear;
        thisMonth = true;
      } else {
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
      bool isHovered = (cell == hoveredCell_) && thisMonth;
      bool isDisabled = _isDisabled(dYear, dMonth, dayNum);

      // Cell background
      if (isSelected)
        painter.fillRoundedRect(cx + 2, cy + 2, calCellSize - 4,
                                calCellSize - 4, 4, daySelectedBg, 255);
      else if (isHovered && !isDisabled)
        painter.fillRoundedRect(cx + 2, cy + 2, calCellSize - 4,
                                calCellSize - 4, 4, dayHoverBg, 255);

      // Today ring
      if (isToday && !isSelected)
        painter.drawBorder(cx + 2, cy + 2, calCellSize - 4, calCellSize - 4, 4,
                           todayBorderColor, 1, 255);

      // Day number text
      COLORREF textCol = isSelected   ? daySelectedText
                         : !thisMonth ? otherMonthText
                         : isDisabled ? otherMonthText
                                      : dayTextColor;

      std::string ds = std::to_string(dayNum);
      int wlen = MultiByteToWideChar(CP_UTF8, 0, ds.c_str(), -1, nullptr, 0);
      std::wstring wds(wlen, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, ds.c_str(), -1, wds.data(), wlen);
      painter.drawText(wds, cx + 1, cy + 1, calCellSize - 2, calCellSize - 2,
                       dayFont, textCol,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }

  // ── Year picker ───────────────────────────────────────────────────────────

  void _renderYearPicker(GraphicsContext &ctx, FontCache &fontCache) {
    Painter painter(ctx);

    painter.fillRect(0, 0, popupW_, calHeaderH, headerBgColor);
    _drawNavArrow(ctx, calPadH + 8, calHeaderH / 2, false);
    _drawNavArrow(ctx, popupW_ - calPadH - 8, calHeaderH / 2, true);

    {
      std::string range = std::to_string(yearRangeStart_) + " \xe2\x80\x93 " +
                          std::to_string(yearRangeStart_ + 11);
      int wlen = MultiByteToWideChar(CP_UTF8, 0, range.c_str(), -1, nullptr, 0);
      std::wstring wrange(wlen, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, range.c_str(), -1, wrange.data(), wlen);
      NativeFont font = fontCache.getFont(14, FontWeight::Bold);
      painter.drawText(wrange, calPadH + 24, 0, popupW_ - (calPadH + 24) * 2,
                       calHeaderH, font, headerTextColor,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    NativeFont yearFont = fontCache.getFont(12, FontWeight::Normal);
    int cellW = popupW_ / 4;
    int cellH = (popupH_ - calHeaderH) / 3;

    for (int i = 0; i < 12; i++) {
      int yr = yearRangeStart_ + i;
      int col = i % 4;
      int row = i / 4;
      int cx = col * cellW;
      int cy = calHeaderH + row * cellH;

      if (yr == viewYear)
        painter.fillRoundedRect(cx + 4, cy + 4, cellW - 8, cellH - 8, 4,
                                yearSelectedBg, 255);
      else if (i == hoveredCell_)
        painter.fillRoundedRect(cx + 4, cy + 4, cellW - 8, cellH - 8, 4,
                                yearHoverBg, 255);

      COLORREF textCol = (yr == viewYear) ? yearSelectedText : dayTextColor;
      std::string ys = std::to_string(yr);
      int wlen = MultiByteToWideChar(CP_UTF8, 0, ys.c_str(), -1, nullptr, 0);
      std::wstring wys(wlen, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, ys.c_str(), -1, wys.data(), wlen);
      painter.drawText(wys, cx, cy, cellW, cellH, yearFont, textCol,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }

  // ── Click handlers ────────────────────────────────────────────────────────

  void _handleCalendarClick(int rx, int ry) {
    // Nav arrows
    bool leftArrow =
        (rx >= calPadH && rx < calPadH + 24 && ry >= 0 && ry < calHeaderH);
    bool rightArrow = (rx >= popupW_ - calPadH - 24 && rx < popupW_ &&
                       ry >= 0 && ry < calHeaderH);
    bool headerClick = (!leftArrow && !rightArrow && ry < calHeaderH);

    if (leftArrow) {
      viewMonth--;
      if (viewMonth < 1) {
        viewMonth = 12;
        viewYear--;
      }
      hoveredCell_ = -1;
      refresh_();
      return;
    }
    if (rightArrow) {
      viewMonth++;
      if (viewMonth > 12) {
        viewMonth = 1;
        viewYear++;
      }
      hoveredCell_ = -1;
      refresh_();
      return;
    }
    if (headerClick) {
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

    if (cell < firstWD) {
      dayNum = prevDays - firstWD + cell + 1;
      dMonth = viewMonth == 1 ? 12 : viewMonth - 1;
      dYear = viewMonth == 1 ? viewYear - 1 : viewYear;
    } else if (cell - firstWD < daysInMon) {
      dayNum = cell - firstWD + 1;
      dMonth = viewMonth;
      dYear = viewYear;
    } else {
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

  void _handleYearPickerClick(int rx, int ry) {
    bool leftArrow =
        (rx >= calPadH && rx < calPadH + 24 && ry >= 0 && ry < calHeaderH);
    bool rightArrow = (rx >= popupW_ - calPadH - 24 && rx < popupW_ &&
                       ry >= 0 && ry < calHeaderH);

    if (leftArrow) {
      yearRangeStart_ -= 12;
      hoveredCell_ = -1;
      refresh_();
      return;
    }
    if (rightArrow) {
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

  // ── Hit testing ───────────────────────────────────────────────────────────

  // Returns 0-41 for the cell under (rx,ry) in the calendar grid, else -1
  int _dayIndexAt(int rx, int ry) const {
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
  int _yearIndexAt(int rx, int ry) const {
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

  void _drawNavArrow(GraphicsContext &ctx, int cx, int cy, bool right) const {
    Painter painter(ctx);
    int s = 5;
    if (right) {
      painter.drawLine(cx - s, cy - s, cx + s, cy, navArrowColor, 2);
      painter.drawLine(cx + s, cy, cx - s, cy + s, navArrowColor, 2);
    } else {
      painter.drawLine(cx + s, cy - s, cx - s, cy, navArrowColor, 2);
      painter.drawLine(cx - s, cy, cx + s, cy + s, navArrowColor, 2);
    }
  }

  void _drawCalendarIcon(GraphicsContext &ctx, int cx, int cy) const {
    Painter painter(ctx);
    COLORREF iconColor = RGB(140, 140, 140);

    // Outer rect outline
    painter.drawRectOutline(cx, cy + 2, 16, 14, iconColor, 1);

    // Top bar fill
    painter.fillRect(cx, cy + 2, 16, 4, iconColor);

    // 2×3 dot grid — each dot is a 1×1 fillRect
    for (int r = 0; r < 2; r++)
      for (int c = 0; c < 3; c++)
        painter.fillRect(cx + 3 + c * 4, cy + 8 + r * 4, 1, 1, iconColor);
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