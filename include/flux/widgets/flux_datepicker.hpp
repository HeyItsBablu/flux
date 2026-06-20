#ifndef FLUX_DATE_PICKER_HPP
#define FLUX_DATE_PICKER_HPP

#include "../flux_core.hpp"
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

class DatePickerWidget : public Widget, public OverlayContent {
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

  void onDetach() override {
    if (isOpen)
      closeCalendar_();
    Widget::onDetach();
  }

  // ── OverlayContent ────────────────────────────────────────────────────────
  OverlayPolicy overlayPolicy() const override {
    // Same reasoning as DropdownWidget: modal so outside clicks close the
    // calendar and clicks inside it (nav arrows, day cells, year cells)
    // are fully consumed. blocksHoverBelow stays false — an open calendar
    // shouldn't pause hover/tooltips elsewhere in the app.
    // capturesKeyboard is true for consistency with the other popup
    // overlays, though note this widget currently implements no
    // onOverlayKeyDown — there's no keyboard navigation (arrow keys,
    // Enter, Escape) wired up here yet, same as in the pre-migration
    // code. Flagging as a gap, not something this migration adds or
    // silently fixes.
    return {/*modal=*/true, /*blocksHoverBelow=*/false, /*capturesKeyboard=*/true};
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

      std::wstring wlabel = toWideString(label);
      painter.drawText(wlabel, x + paddingLeft, y,
                       width - paddingLeft - paddingRight, height, font,
                       fieldTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {

      std::wstring wph = toWideString(placeholder);
      painter.drawText(wph, x + paddingLeft, y,
                       width - paddingLeft - paddingRight, height, font,
                       placeholderColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    _drawCalendarIcon(ctx, x + width - 26, y + height / 2 - 8);
    needsPaint = false;
  }

  // ── renderOverlay ─────────────────────────────────────────────────────────
  // Local coordinates — (0,0) is the popup's own top-left corner. Body is
  // unchanged from the old renderPopupContent: it was already drawing
  // entirely in popup-local space.
  void renderOverlay(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!isOpen)
      return;
    _computePopupSize();

    Painter painter(ctx);

    // Shadow — filled rounded rect offset by shadowOffset
    painter.fillRoundedRect(shadowOffset, shadowOffset, popupW_, popupH_,
                            calBorderRadius,
                            Color::fromRGBA(0, 0, 0,
                                            60)); // alpha 60 ≈ 24%

    // Background
    painter.fillRoundedRect(0, 0, popupW_, popupH_, calBorderRadius,
                            calBgColor);
    painter.drawBorder(0, 0, popupW_, popupH_, calBorderRadius, calBorderColor,
                       1);

    if (showingYears)
      _renderYearPicker(ctx, fontCache);
    else
      _renderCalendarGrid(ctx, fontCache);
  }

  // ── OverlayContent input handlers (popup-local coordinates) ─────────────
  // No more screenToClient(popupScreenX_, popupScreenY_) round-trip —
  // OverlayManager already delivers coordinates relative to the popup's
  // own top-left corner.

  bool onOverlayMouseDown(int localX, int localY) override {
    if (!isOpen)
      return false;

    if (localX >= 0 && localX < popupW_ && localY >= 0 && localY < popupH_) {
      if (showingYears)
        _handleYearPickerClick(localX, localY);
      else
        _handleCalendarClick(localX, localY);
      return true;
    }

    closeCalendar_();
    return true;
  }

  bool onOverlayMouseMove(int localX, int localY) override {
    if (!isOpen)
      return false;

    int newHover = -1;
    if (localX >= 0 && localX < popupW_ && localY >= 0 && localY < popupH_) {
      if (showingYears)
        newHover = _yearIndexAt(localX, localY);
      else
        newHover = _dayIndexAt(localX, localY);
    }

    if (newHover != hoveredCell_) {
      hoveredCell_ = newHover;
      refresh_();
      return true;
    }
    return false;
  }

  void onOverlayOutsideClick() override { closeCalendar_(); }

  // ── Bar/trigger mouse events (normal widget-tree dispatch) ──────────────
  // Only ever sees the trigger field now — popup hit-testing moved to
  // onOverlayMouseDown/onOverlayMouseMove above. While the popup is open,
  // OverlayManager routes clicks/moves directly to those handlers instead
  // of here (matches the modal entries in DropdownWidget/ContextMenuWidget).

  bool handleMouseDown(int mx, int my) override {
    if (mx >= x && mx < x + width && my >= y && my < y + height) {
      if (isOpen)
        closeCalendar_();
      else
        openCalendar_();
      return true;
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
  std::shared_ptr<DatePickerWidget> setAccentColor(Color c) {
    headerBgColor = c;
    daySelectedBg = c;
    todayBorderColor = c;
    fieldFocusBorder = c;
    yearSelectedBg = c;
    markNeedsPaint();
    return self_();
  }

private:
  State<FluxDate> *boundState_ = nullptr;

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
    if (isOpen)
      return;

    auto *ui = FluxUI::getCurrentInstance();
    if (!ui)
      return;

    if (!selectedDate.isValid()) {
      FluxDate td = FluxDate::today();
      viewYear = td.year;
      viewMonth = td.month;
    }
    isOpen = true;
    showingYears = false;
    hoveredCell_ = -1;

    _computePopupSize(); // size only — positioning/clamping is the manager's job now

    // Position below the field, in CLIENT coordinates. No more manual
    // clientToScreen + #ifdef _WIN32 monitor clamping — OverlayManager::
    // show() does all of that internally.
    ui->overlays().show(this, x, y + height + 2,
                        popupW_ + shadowOffset, popupH_ + shadowOffset,
                        100, ui->getFontCache());

    markNeedsPaint();
  }

  void closeCalendar_() {
    if (!isOpen)
      return;
    isOpen = false;
    showingYears = false;
    hoveredCell_ = -1;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().hide(this);
    markNeedsPaint();
  }

  void refresh_() {
    if (!isOpen)
      return;
    if (auto *ui = FluxUI::getCurrentInstance())
      ui->overlays().refresh(this, ui->getFontCache());
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

      std::wstring wlabel = toWideString(label);
      NativeFont font = fontCache.getFont(14, FontWeight::Bold);
      painter.drawText(wlabel, calPadH + 24, 0, popupW_ - (calPadH + 24) * 2,
                       calHeaderH, font, headerTextColor,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ── Weekday row ───────────────────────────────────────────────────────
    NativeFont wdFont = fontCache.getFont(11, FontWeight::Normal);
    for (int col = 0; col < 7; col++) {
      int cx = calPadH + col * calCellSize;

      std::wstring wwd = toWideString(_weekdayShort(col));
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
      // NOTE: this was commented out in the pre-migration source, which
      // left `isHovered` undefined below it (almost certainly a
      // pre-existing bug / non-compiling line, not something this
      // migration introduced). Restored here so hover highlighting on
      // day cells actually works.
      bool isHovered = (cell == hoveredCell_) && thisMonth;
      bool isDisabled = _isDisabled(dYear, dMonth, dayNum);

      // Cell background
      if (isSelected)
        painter.fillRoundedRect(cx + 2, cy + 2, calCellSize - 4,
                                calCellSize - 4, 4, daySelectedBg);
      else if (isHovered && !isDisabled)
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

  void _renderYearPicker(GraphicsContext &ctx, FontCache &fontCache) {
    Painter painter(ctx);

    painter.fillRect(0, 0, popupW_, calHeaderH, headerBgColor);
    _drawNavArrow(ctx, calPadH + 8, calHeaderH / 2, false);
    _drawNavArrow(ctx, popupW_ - calPadH - 8, calHeaderH / 2, true);

    {
      std::string range = std::to_string(yearRangeStart_) + " \xe2\x80\x93 " +
                          std::to_string(yearRangeStart_ + 11);

      std::wstring wrange = toWideString(range);
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

  // ── Hit testing (popup-local coordinates) ────────────────────────────────

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