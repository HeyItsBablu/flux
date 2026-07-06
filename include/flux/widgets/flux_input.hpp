#ifndef FLUX_INPUT_HPP
#define FLUX_INPUT_HPP

#include "../flux_core.hpp"
#include "../flux_state.hpp"

#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
#include "../flux_dom_adapter.hpp"
// fluxDomEnsureNode / fluxDomApplyRect are declared once, with their
// optional "slot" parameter, in flux_dom_adapter.hpp — not redeclared
// here, to avoid a default-argument redefinition conflict now that
// CheckBoxWidget (below) also needs them with a non-default slot.
#endif

#include "flux_keyboard.hpp"

#include <algorithm>
#include <iostream>

template <typename T>
class State;

class Widget;
class RadioGroupWidget;

using WidgetPtr = std::shared_ptr<Widget>;
using ClickHandler = std::function<void()>;
using HoverHandler = std::function<void(bool)>;

// ============================================================================
// ToggleWidget
// ============================================================================

class ToggleWidget : public Widget
{
public:
  bool toggled = false;

  int toggleWidth = 44;
  int toggleHeight = 24;
  int thumbSize = 18;

  Color trackOffColor = Color::fromRGB(200, 200, 200);
  Color trackOnColor = Color::fromRGB(76, 175, 80);
  Color thumbColor = Color::fromRGB(255, 255, 255);
  Color thumbHoverColor = Color::fromRGB(245, 245, 245);
  Color thumbPressedColor = Color::fromRGB(235, 235, 235);
  Color shadowColor = Color::fromRGB(180, 180, 180);

  bool isThumbHovered = false;
  bool isPressed = false;
  double animationProgress = 0.0;

  std::function<void(bool)> onToggleChanged;

  ToggleWidget()
  {
    width = toggleWidth;
    height = toggleHeight;
    autoWidth = false;
    autoHeight = false;
    paddingLeft = paddingRight = 4;
    paddingTop = paddingBottom = 4;
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    if (!text.empty())
    {
      std::wstring wtext = toWideString(text);
      NativeFont font = fontCache.getFont(fontSize, fontWeight);
      int tw = 0, th = 0;
      Painter(ctx, this).measureText(wtext, font, tw, th);
      width = toggleWidth + 12 + tw;
      height = std::max(toggleHeight, th);
    }
    else
    {
      width = toggleWidth;
      height = toggleHeight;
    }
    width = constraints.clampWidth(width + paddingLeft + paddingRight);
    height = constraints.clampHeight(height + paddingTop + paddingBottom);
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {

#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
        _renderDom(adapter);
        needsPaint = false;
        return;
    }
#endif
    Painter painter(ctx, this);

    int toggleX = x + paddingLeft;
    int toggleY = y + paddingTop +
                  (height - paddingTop - paddingBottom - toggleHeight) / 2;

    animationProgress = toggled ? 1.0 : 0.0;

    Color trackColor = trackOffColor.interpolate(trackOnColor, animationProgress);
    painter.fillRoundedRectGDI(toggleX, toggleY, toggleWidth, toggleHeight,
                               toggleHeight, trackColor, trackColor, 0);

    int thumbPadding = (toggleHeight - thumbSize) / 2;
    int thumbOffX = toggleX + thumbPadding;
    int thumbOnX = toggleX + toggleWidth - thumbSize - thumbPadding;
    int thumbX = thumbOffX + (int)((thumbOnX - thumbOffX) * animationProgress);
    int thumbY = toggleY + thumbPadding;

    painter.drawEllipse(thumbX - 1, thumbY + 2, thumbSize + 2, thumbSize,
                        shadowColor, shadowColor, 0);

    Color currentThumbColor = isPressed        ? thumbPressedColor
                              : isThumbHovered ? thumbHoverColor
                                               : thumbColor;
    painter.drawEllipse(thumbX, thumbY, thumbSize, thumbSize, currentThumbColor,
                        Color::fromRGB(230, 230, 230), 1);

    if (!text.empty())
    {
      std::wstring wtext = toWideString(text);
      NativeFont font = fontCache.getFont(fontSize, fontWeight);
      int textX = toggleX + toggleWidth + 12;
      int textW = (x + width - paddingRight) - textX;
      painter.drawText(wtext, textX, y + paddingTop, textW,
                       height - paddingTop - paddingBottom, font,
                       getCurrentTextColor(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override
  {
    if (mx >= x && mx < x + width && my >= y && my < y + height)
    {
      isPressed = true;
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseUp(int mx, int my) override
  {
    if (isPressed)
    {
      isPressed = false;
      if (mx >= x && mx < x + width && my >= y && my < y + height)
      {
        toggled = !toggled;
        notifyToggleChanged();
      }
      markNeedsPaint();
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override
  {
    int toggleX = x + paddingLeft;
    int toggleY = y + paddingTop +
                  (height - paddingTop - paddingBottom - toggleHeight) / 2;
    bool nowHovered = (mx >= toggleX && mx < toggleX + toggleWidth &&
                       my >= toggleY && my < toggleY + toggleHeight);
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
    if (keyCode == Key::Space || keyCode == Key::Return)
    {
      toggled = !toggled;
      notifyToggleChanged();
      markNeedsPaint();
      return true;
    }
    return false;
  }

  std::shared_ptr<ToggleWidget> setToggled(bool value)
  {
    toggled = value;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setTrackOffColor(Color color)
  {
    trackOffColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setTrackOnColor(Color color)
  {
    trackOnColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setThumbColor(Color color)
  {
    thumbColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setShadowColor(Color color)
  {
    shadowColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setOnToggleChanged(std::function<void(bool)> cb)
  {
    onToggleChanged = cb;
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setValue(State<bool> &state)
  {
    toggled = state.get();
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const bool &val)
        {
          static_cast<ToggleWidget *>(w)->toggled = val;
        },
        false);
    boundBoolState = &state;
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }
  std::shared_ptr<ToggleWidget> setLabel(const std::string &label)
  {
    text = label;
    markNeedsLayout();
    return std::static_pointer_cast<ToggleWidget>(shared_from_this());
  }

private:
  State<bool> *boundBoolState = nullptr;

  void notifyToggleChanged()
  {
    if (onToggleChanged)
      onToggleChanged(toggled);
    if (boundBoolState)
      boundBoolState->set(toggled);
  }

#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
  // Four layers under one owner: track, thumb-shadow, thumb, label.
  // The thumb's LEFT position is animated via a real CSS transition
  // rather than driven by animationProgress/a JS timer — on canvas,
  // animationProgress snaps instantly to 0/1 (see render() above; there
  // is no interpolation loop anywhere in this widget), so the canvas
  // version's "animation" was really just an instant toggle rendered at
  // whatever moment repaint happened to catch it. CSS transition gives
  // an actual smooth slide for free, which is a strict visual upgrade,
  // not a behavior change anything else here depends on.
  void _renderDom(IDomAdapter *adapter)
  {
      char buf[24];
      auto px = [&](int v) { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
      char colbuf[32];
      auto rgb = [&](Color c) { snprintf(colbuf, sizeof(colbuf), "rgb(%d,%d,%d)", c.r, c.g, c.b); return std::string(colbuf); };

      int toggleX = x + paddingLeft;
      int toggleY = y + paddingTop +
                    (height - paddingTop - paddingBottom - toggleHeight) / 2;

      Color trackColor = toggled ? trackOnColor : trackOffColor;

      // ── Track ─────────────────────────────────────────────────────────
      DomNodeHandle track = fluxDomEnsureNode(this, "div", "track");
      fluxDomApplyRect(this, toggleX, toggleY, toggleWidth, toggleHeight, "track");
      adapter->setStyle(track, "border-radius", px(toggleHeight / 2));
      adapter->setStyle(track, "background-color", rgb(trackColor));
      adapter->setStyle(track, "transition", "background-color 0.15s ease");
      adapter->setStyle(track, "pointer-events", "none");

      int thumbPadding = (toggleHeight - thumbSize) / 2;
      int thumbOffX = toggleX + thumbPadding;
      int thumbOnX = toggleX + toggleWidth - thumbSize - thumbPadding;
      int thumbX = toggled ? thumbOnX : thumbOffX;
      int thumbY = toggleY + thumbPadding;

      // ── Thumb shadow — separate slot, sits directly behind the thumb.
      DomNodeHandle shadow = fluxDomEnsureNode(this, "div", "shadow");
      fluxDomApplyRect(this, thumbX - 1, thumbY + 2, thumbSize + 2, thumbSize, "shadow");
      adapter->setStyle(shadow, "border-radius", "50%");
      adapter->setStyle(shadow, "background-color", rgb(shadowColor));
      adapter->setStyle(shadow, "transition", "left 0.15s ease");
      adapter->setStyle(shadow, "pointer-events", "none");

      // ── Thumb ─────────────────────────────────────────────────────────
      Color currentThumbColor = isPressed        ? thumbPressedColor
                                : isThumbHovered ? thumbHoverColor
                                                 : thumbColor;
      DomNodeHandle thumb = fluxDomEnsureNode(this, "div", "thumb");
      fluxDomApplyRect(this, thumbX, thumbY, thumbSize, thumbSize, "thumb");
      adapter->setStyle(thumb, "border-radius", "50%");
      adapter->setStyle(thumb, "background-color", rgb(currentThumbColor));
      adapter->setStyle(thumb, "border", "1px solid rgb(230,230,230)");
      adapter->setStyle(thumb, "box-sizing", "border-box");
      adapter->setStyle(thumb, "transition", "left 0.15s ease, background-color 0.15s ease");
      adapter->setStyle(thumb, "pointer-events", "none");

      // ── Label — separate slot, same pattern as CheckBox/RadioButton.
      if (!text.empty())
      {
          int textX = toggleX + toggleWidth + 12;
          int textW = (x + width - paddingRight) - textX;
          DomNodeHandle label = fluxDomEnsureNode(this, "div", "label");
          fluxDomApplyRect(this, textX, y + paddingTop, textW,
                          height - paddingTop - paddingBottom, "label");
          adapter->setStyle(label, "display", "flex");
          adapter->setStyle(label, "align-items", "center");
          adapter->setStyle(label, "white-space", "nowrap");
          adapter->setStyle(label, "font-size", px(fontSize));
          adapter->setStyle(label, "color", rgb(getCurrentTextColor()));
          adapter->setStyle(label, "pointer-events", "none");
          adapter->setText(label, text);
      }
  }
#endif
};

using ToggleWidgetPtr = std::shared_ptr<ToggleWidget>;

inline ToggleWidgetPtr Toggle(const std::string &label = "")
{
  auto w = std::make_shared<ToggleWidget>();
  if (!label.empty())
    w->setLabel(label);
  return w;
}

// ============================================================================
// SliderWidget
// ============================================================================

class SliderWidget : public Widget
{
public:
  double value = 0.0;
  double minValue = 0.0;
  double maxValue = 100.0;
  double step = 1.0;

  int trackHeight = 4;
  int thumbRadius = 10;

  Color trackColor = Color::fromRGB(200, 200, 200);
  Color trackFillColor = Color::fromRGB(33, 150, 243);
  Color thumbColor = Color::fromRGB(33, 150, 243);
  Color thumbHoverColor = Color::fromRGB(25, 118, 210);
  Color thumbDragColor = Color::fromRGB(13, 71, 161);

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

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fontCache*/) override
  {
    if (autoWidth)
      width = constraints.maxWidth;
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache & /*fontCache*/) override
  {

#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
        _renderDom(adapter);
        needsPaint = false;
        return;
    }
#endif
    Painter painter(ctx, this);

    int trackY = y + height / 2;
    int trackLeft = x + paddingLeft;
    int trackRight = x + width - paddingRight;
    int trackWidth = trackRight - trackLeft;

    double normalizedValue = (value - minValue) / (maxValue - minValue);
    int thumbX = trackLeft + (int)(normalizedValue * trackWidth);

    painter.fillRect(trackLeft, trackY - trackHeight / 2,
                     trackWidth, trackHeight, trackColor);
    painter.fillRect(trackLeft, trackY - trackHeight / 2,
                     thumbX - trackLeft, trackHeight, trackFillColor);

    Color currentThumbColor = isDragging       ? thumbDragColor
                              : isThumbHovered ? thumbHoverColor
                                               : thumbColor;
    painter.drawEllipse(thumbX - thumbRadius, trackY - thumbRadius,
                        thumbRadius * 2, thumbRadius * 2,
                        currentThumbColor, Color::fromRGB(255, 255, 255), 1);

    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override
  {
    if (mx >= x && mx < x + width && my >= y && my < y + height)
    {
      isDragging = true;
      FluxUI::getCurrentInstance()->captureMouseInput();
      updateValueFromMouseX(mx);
      return true;
    }
    return false;
  }

  bool handleMouseUp(int /*mx*/, int /*my*/) override
  {
    if (isDragging)
    {
      isDragging = false;
      FluxUI::getCurrentInstance()->releaseMouseInput();
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
    double nv = (value - minValue) / (maxValue - minValue);
    int thumbX = trackLeft + (int)(nv * trackWidth);

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
    case Key::Left:
    case Key::Down:
      value -= step;
      break;
    case Key::Right:
    case Key::Up:
      value += step;
      break;
    case Key::Home:
      value = minValue;
      break;
    case Key::End:
      value = maxValue;
      break;
    default:
      return false;
    }
    value = std::max(minValue, std::min(maxValue, value));
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
  std::shared_ptr<SliderWidget> setTrackColor(Color color)
  {
    trackColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }
  std::shared_ptr<SliderWidget> setTrackFillColor(Color color)
  {
    trackFillColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }
  std::shared_ptr<SliderWidget> setThumbColor(Color color)
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
    value = std::max(minValue, std::min(maxValue, state.get()));
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const double &val)
        {
          auto *s = static_cast<SliderWidget *>(w);
          s->value = std::max(s->minValue, std::min(s->maxValue, val));
        },
        false);
    boundDoubleState = &state;
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }
  std::shared_ptr<SliderWidget> setValue(State<int> &state)
  {
    value = std::max(minValue, std::min(maxValue, (double)state.get()));
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const int &val)
        {
          auto *s = static_cast<SliderWidget *>(w);
          s->value = std::max(s->minValue, std::min(s->maxValue, (double)val));
        },
        false);
    boundIntState = &state;
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }
  template <typename T, typename F>
  std::shared_ptr<SliderWidget> setValue(State<T> &state, F transform)
  {
    std::function<double(const T &)> fn = transform;
    value = std::max(minValue, std::min(maxValue, fn(state.get())));
    state.bindProperty(
        shared_from_this(),
        [fn](Widget *w, const T &val)
        {
          auto *s = static_cast<SliderWidget *>(w);
          s->value = std::max(s->minValue, std::min(s->maxValue, fn(val)));
        },
        false);
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }
  std::shared_ptr<SliderWidget> setWidth(int w)
  {
    width = w;
    autoWidth = false;
    return std::static_pointer_cast<SliderWidget>(shared_from_this());
  }

private:
  State<double> *boundDoubleState = nullptr;
  State<int> *boundIntState = nullptr;


#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
  // Three layers under one owner: track, fill (progress), thumb.
  // Deliberately NO position transition on fill/thumb — this widget
  // updates continuously during a drag (see handleMouseMove ->
  // updateValueFromMouseX, called on every reported mouse-move pixel),
  // and a CSS transition would make the thumb visibly lag behind the
  // live cursor position instead of tracking it. Toggle/RadioButton's
  // transitions are safe because those are discrete, one-shot state
  // flips, not a value that changes every frame while the user drags —
  // that distinction is why the same technique isn't reused here for
  // position (color-only transitions on hover/drag state are still
  // fine and included below).
  void _renderDom(IDomAdapter *adapter)
  {
      char buf[24];
      auto px = [&](int v) { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
      char colbuf[32];
      auto rgb = [&](Color c) { snprintf(colbuf, sizeof(colbuf), "rgb(%d,%d,%d)", c.r, c.g, c.b); return std::string(colbuf); };

      int trackY = y + height / 2;
      int trackLeft = x + paddingLeft;
      int trackRight = x + width - paddingRight;
      int trackWidth = trackRight - trackLeft;

      double normalizedValue = (maxValue > minValue)
                                   ? (value - minValue) / (maxValue - minValue)
                                   : 0.0;
      int thumbX = trackLeft + (int)(normalizedValue * trackWidth);

      // ── Track (full-width background) ────────────────────────────────
      DomNodeHandle track = fluxDomEnsureNode(this, "div", "track");
      fluxDomApplyRect(this, trackLeft, trackY - trackHeight / 2,
                      trackWidth, trackHeight, "track");
      adapter->setStyle(track, "border-radius", px(trackHeight / 2));
      adapter->setStyle(track, "background-color", rgb(trackColor));
      adapter->setStyle(track, "pointer-events", "none");

      // ── Fill (progress up to the thumb) — separate slot, drawn on
      // top of the track. Zero-width when value == minValue; the node
      // is still created (not skipped) so later drags just widen it
      // rather than creating/destroying it.
      DomNodeHandle fill = fluxDomEnsureNode(this, "div", "fill");
      int fillWidth = std::max(0, thumbX - trackLeft);
      fluxDomApplyRect(this, trackLeft, trackY - trackHeight / 2,
                      fillWidth, trackHeight, "fill");
      adapter->setStyle(fill, "border-radius", px(trackHeight / 2));
      adapter->setStyle(fill, "background-color", rgb(trackFillColor));
      adapter->setStyle(fill, "pointer-events", "none");

      // ── Thumb ─────────────────────────────────────────────────────────
      Color currentThumbColor = isDragging       ? thumbDragColor
                                : isThumbHovered ? thumbHoverColor
                                                 : thumbColor;
      DomNodeHandle thumb = fluxDomEnsureNode(this, "div", "thumb");
      fluxDomApplyRect(this, thumbX - thumbRadius, trackY - thumbRadius,
                      thumbRadius * 2, thumbRadius * 2, "thumb");
      adapter->setStyle(thumb, "border-radius", "50%");
      adapter->setStyle(thumb, "background-color", rgb(currentThumbColor));
      adapter->setStyle(thumb, "border", "1px solid rgb(255,255,255)");
      adapter->setStyle(thumb, "box-sizing", "border-box");
      // Color-only transition is safe (doesn't fight live drag input,
      // unlike a position transition would) — smooths the hover/press
      // color swap the same way Toggle's track color transition does.
      adapter->setStyle(thumb, "transition", "background-color 0.1s ease");
      adapter->setStyle(thumb, "pointer-events", "none");
  }
#endif

  void updateValueFromMouseX(int mx)
  {
    int trackLeft = x + paddingLeft;
    int trackRight = x + width - paddingRight;
    int trackWidth = trackRight - trackLeft;
    int clampedX = std::max(trackLeft, std::min(trackRight, mx));
    double npos = (double)(clampedX - trackLeft) / trackWidth;
    double newValue = minValue + npos * (maxValue - minValue);
    if (step > 0)
      newValue = round(newValue / step) * step;
    newValue = std::max(minValue, std::min(maxValue, newValue));
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

using SliderWidgetPtr = std::shared_ptr<SliderWidget>;

inline SliderWidgetPtr Slider(double minValue = 0.0, double maxValue = 100.0,
                              double step = 1.0)
{
  auto w = std::make_shared<SliderWidget>();
  w->setMinValue(minValue);
  w->setMaxValue(maxValue);
  w->setStep(step);
  return w;
}

// ============================================================================
// CheckBoxWidget
// ============================================================================

class CheckBoxWidget : public Widget
{
public:
  bool checked = false;
  int boxSize = 16;

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    if (!text.empty())
    {
      std::wstring wtext = toWideString(text);
      NativeFont font = fontCache.getFont(fontSize, fontWeight);
      int tw = 0, th = 0;
      Painter(ctx, this).measureText(wtext, font, tw, th);
      width = boxSize + 8 + tw;
      height = std::max(boxSize, th);
    }
    else
    {
      width = boxSize;
      height = boxSize;
    }
    width = constraints.clampWidth(width + paddingLeft + paddingRight);
    height = constraints.clampHeight(height + paddingTop + paddingBottom);
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {

#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
        _renderDom(adapter);
        needsPaint = false;
        return;
    }
#endif
    Painter painter(ctx, this);

    int boxX = x + paddingLeft;
    int boxY = y + paddingTop + (height - paddingTop - paddingBottom - boxSize) / 2;

    Color fill = checked ? Color::fromRGB(76, 175, 80) : Color::fromRGB(255, 255, 255);
    Color stroke = checked ? Color::fromRGB(56, 155, 60) : Color::fromRGB(150, 150, 150);
    painter.drawRectOutline(boxX, boxY, boxSize, boxSize, stroke, 1);
    painter.fillRect(boxX + 1, boxY + 1, boxSize - 2, boxSize - 2, fill);

    if (checked)
    {
      int cx = boxX + 3;
      int cy = boxY + boxSize / 2;
      painter.drawLine(cx, cy, cx + 4, cy + 4, Color::fromRGB(255, 255, 255), 2);
      painter.drawLine(cx + 4, cy + 4, cx + 9, cy - 4, Color::fromRGB(255, 255, 255), 2);
    }

    if (!text.empty())
    {
      std::wstring wtext = toWideString(text);
      NativeFont font = fontCache.getFont(fontSize, fontWeight);
      int textX = boxX + boxSize + 8;
      painter.drawText(wtext, textX, y + paddingTop,
                       (x + width - paddingRight) - textX,
                       height - paddingTop - paddingBottom,
                       font, getCurrentTextColor(),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override
  {
    if (mx >= x && mx < x + width && my >= y && my < y + height)
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
          static_cast<CheckBoxWidget *>(w)->checked = val;
        },
        false);
    onClick = [&state, this]()
    { state.set(checked); };
    return shared_from_this();
  }


private:
#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
  // Hit-testing for clicks stays entirely in C++ (findAndHandleMouseEvent
  // via the input-capture div — see flux_window_dom.cpp); these nodes are
  // purely visual, hence pointer-events:none on both, matching every
  // other non-native-input widget on this backend.
  void _renderDom(IDomAdapter *adapter)
  {
      char buf[24];
      auto px = [&](int v) { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
      char colbuf[32];
      auto rgb = [&](Color c) { snprintf(colbuf, sizeof(colbuf), "rgb(%d,%d,%d)", c.r, c.g, c.b); return std::string(colbuf); };

      int boxX = x + paddingLeft;
      int boxY = y + paddingTop + (height - paddingTop - paddingBottom - boxSize) / 2;

      // ── Box + checkmark — ONE node. The checkmark is the node's own
      // text content rather than a second overlapping layer, so it
      // doesn't need its own slot.
      DomNodeHandle box = fluxDomEnsureNode(this, "div", "box");
      fluxDomApplyRect(this, boxX, boxY, boxSize, boxSize, "box");
      Color fill = checked ? Color::fromRGB(76, 175, 80) : Color::fromRGB(255, 255, 255);
      Color stroke = checked ? Color::fromRGB(56, 155, 60) : Color::fromRGB(150, 150, 150);
      adapter->setStyle(box, "background-color", rgb(fill));
      adapter->setStyle(box, "border", "1px solid " + rgb(stroke));
      adapter->setStyle(box, "box-sizing", "border-box");
      adapter->setStyle(box, "display", "flex");
      adapter->setStyle(box, "align-items", "center");
      adapter->setStyle(box, "justify-content", "center");
      adapter->setStyle(box, "color", "rgb(255,255,255)");
      adapter->setStyle(box, "font-size", px(std::max(8, boxSize - 4)));
      adapter->setStyle(box, "line-height", "1");
      adapter->setStyle(box, "pointer-events", "none");
      // Unicode checkmark approximates the two hand-drawn lines the
      // canvas/D2D/Cairo backends stroke — same cross-backend visual
      // tolerance already accepted elsewhere for text/decoration in this
      // file, not a pixel-exact match.
      adapter->setText(box, checked ? "\xE2\x9C\x93" : "");

      // ── Label — a SEPARATE node (different slot) under the SAME
      // owner. Without slots this call would silently overwrite the box
      // node above, since one Widget* used to map to exactly one node.
      if (!text.empty())
      {
          int textX = boxX + boxSize + 8;
          DomNodeHandle label = fluxDomEnsureNode(this, "div", "label");
          fluxDomApplyRect(this, textX, y + paddingTop,
                          (x + width - paddingRight) - textX,
                          height - paddingTop - paddingBottom, "label");
          adapter->setStyle(label, "display", "flex");
          adapter->setStyle(label, "align-items", "center");
          adapter->setStyle(label, "white-space", "nowrap");
          adapter->setStyle(label, "font-size", px(fontSize));
          adapter->setStyle(label, "color", rgb(getCurrentTextColor()));
          adapter->setStyle(label, "pointer-events", "none");
          adapter->setText(label, text);
      }
  }
#endif
};

using CheckBoxWidgetPtr = std::shared_ptr<CheckBoxWidget>;

inline CheckBoxWidgetPtr CheckBox(const std::string &label = "")
{
  auto w = std::make_shared<CheckBoxWidget>();
  w->text = label;
  w->textColor = Color::fromRGB(30, 30, 30);
  w->paddingLeft = w->paddingRight = 4;
  w->paddingTop = w->paddingBottom = 4;
  return w;
}

// ============================================================================
// RadioButtonWidget
// ============================================================================

class RadioButtonWidget : public Widget
{
public:
  bool selected = false;
  int circleSize = 16;
  int innerCircleSize = 8;
  std::string value;

  Color circleColor = Color::fromRGB(150, 150, 150);
  Color selectedCircleColor = Color::fromRGB(33, 150, 243);
  Color innerCircleColor = Color::fromRGB(33, 150, 243);
  Color hoverCircleColor = Color::fromRGB(100, 100, 100);

  RadioGroupWidget *parentGroup = nullptr;

  RadioButtonWidget(const std::string &val = "") : value(val)
  {
    paddingLeft = paddingRight = 4;
    paddingTop = paddingBottom = 4;
  }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    if (!text.empty())
    {
      std::wstring wtext = toWideString(text);
      NativeFont font = fontCache.getFont(fontSize, fontWeight);
      int tw = 0, th = 0;
      Painter(ctx, this).measureText(wtext, font, tw, th);
      width = circleSize + 8 + tw;
      height = std::max(circleSize, th);
    }
    else
    {
      width = circleSize;
      height = circleSize;
    }
    width = constraints.clampWidth(width + paddingLeft + paddingRight);
    height = constraints.clampHeight(height + paddingTop + paddingBottom);
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
        _renderDom(adapter);
        needsPaint = false;
        return;
    }
#endif
    Painter painter(ctx, this);

    int circleX = x + paddingLeft + circleSize / 2;
    int circleY = y + paddingTop + (height - paddingTop - paddingBottom) / 2;

    Color currentCircleColor = selected    ? selectedCircleColor
                               : isHovered ? hoverCircleColor
                                           : circleColor;

    painter.drawEllipse(circleX - circleSize / 2, circleY - circleSize / 2,
                        circleSize, circleSize,
                        Color::fromRGB(255, 255, 255), currentCircleColor, 2);

    if (selected)
      painter.drawEllipse(circleX - innerCircleSize / 2,
                          circleY - innerCircleSize / 2,
                          innerCircleSize, innerCircleSize,
                          innerCircleColor, innerCircleColor, 0);

    if (!text.empty())
    {
      std::wstring wtext = toWideString(text);
      NativeFont font = fontCache.getFont(fontSize, fontWeight);
      int textX = x + paddingLeft + circleSize + 8;
      painter.drawText(wtext, textX, y + paddingTop,
                       (x + width - paddingRight) - textX,
                       height - paddingTop - paddingBottom,
                       font, getCurrentTextColor(),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  bool handleMouseDown(int mx, int my) override
  {
    if (mx >= x && mx < x + width && my >= y && my < y + height)
    {
      selectThis();
      return true;
    }
    return false;
  }

  bool handleKeyDown(int keyCode) override
  {
    if (keyCode == Key::Space || keyCode == Key::Return)
    {
      selectThis();
      return true;
    }
    return false;
  }

  void selectThis();

  std::shared_ptr<RadioButtonWidget> setSelected(bool sel)
  {
    selected = sel;
    markNeedsPaint();
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }
  std::shared_ptr<RadioButtonWidget> setValue(const std::string &val)
  {
    value = val;
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }
  std::shared_ptr<RadioButtonWidget> setCircleColor(Color color)
  {
    circleColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }
  std::shared_ptr<RadioButtonWidget> setSelectedCircleColor(Color color)
  {
    selectedCircleColor = innerCircleColor = color;
    markNeedsPaint();
    return std::static_pointer_cast<RadioButtonWidget>(shared_from_this());
  }

private:
#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
  // Three independent visual layers under one owner: the outer ring, the
  // inner filled dot (only when selected), and the label. Unlike
  // CheckBoxWidget's checkmark — which could ride along as the box
  // node's own text content — a radio's inner dot is a genuinely
  // separate overlapping circle, so it needs its own slot rather than
  // being folded into the ring node.
  void _renderDom(IDomAdapter *adapter)
  {
      char buf[24];
      auto px = [&](int v) { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };
      char colbuf[32];
      auto rgb = [&](Color c) { snprintf(colbuf, sizeof(colbuf), "rgb(%d,%d,%d)", c.r, c.g, c.b); return std::string(colbuf); };

      int circleX = x + paddingLeft;
      int circleY = y + paddingTop + (height - paddingTop - paddingBottom - circleSize) / 2;

      Color currentCircleColor = selected    ? selectedCircleColor
                                : isHovered ? hoverCircleColor
                                            : circleColor;

      // ── Outer ring ────────────────────────────────────────────────────
      DomNodeHandle ring = fluxDomEnsureNode(this, "div", "circle");
      fluxDomApplyRect(this, circleX, circleY, circleSize, circleSize, "circle");
      adapter->setStyle(ring, "border-radius", "50%");
      adapter->setStyle(ring, "background-color", "rgb(255,255,255)");
      adapter->setStyle(ring, "border", "2px solid " + rgb(currentCircleColor));
      adapter->setStyle(ring, "box-sizing", "border-box");
      adapter->setStyle(ring, "pointer-events", "none");

      // ── Inner dot — separate slot, only shown when selected. Hidden
      // (rather than removed) via display:none so the node is reused,
      // not recreated, when selection toggles back and forth.
      DomNodeHandle dot = fluxDomEnsureNode(this, "div", "dot");
      int dotInset = (circleSize - innerCircleSize) / 2;
      fluxDomApplyRect(this, circleX + dotInset, circleY + dotInset,
                      innerCircleSize, innerCircleSize, "dot");
      adapter->setStyle(dot, "border-radius", "50%");
      adapter->setStyle(dot, "background-color", rgb(innerCircleColor));
      adapter->setStyle(dot, "display", selected ? "block" : "none");
      adapter->setStyle(dot, "pointer-events", "none");

      // ── Label — separate slot, same as CheckBoxWidget's pattern.
      if (!text.empty())
      {
          int textX = x + paddingLeft + circleSize + 8;
          DomNodeHandle label = fluxDomEnsureNode(this, "div", "label");
          fluxDomApplyRect(this, textX, y + paddingTop,
                          (x + width - paddingRight) - textX,
                          height - paddingTop - paddingBottom, "label");
          adapter->setStyle(label, "display", "flex");
          adapter->setStyle(label, "align-items", "center");
          adapter->setStyle(label, "white-space", "nowrap");
          adapter->setStyle(label, "font-size", px(fontSize));
          adapter->setStyle(label, "color", rgb(getCurrentTextColor()));
          adapter->setStyle(label, "pointer-events", "none");
          adapter->setText(label, text);
      }
  }
#endif
};

// ============================================================================
// RadioGroupWidget
// ============================================================================

class RadioGroupWidget : public Widget
{
public:
  std::string selectedValue;
  std::vector<RadioButtonWidget *> radioButtons;
  bool isVertical = true;
  std::function<void(const std::string &)> onSelectionChanged;

  RadioGroupWidget() { spacing = 8; }

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    int totalWidth = 0;
    int totalHeight = 0;
    int localMaxW = 0;
    int localMaxH = 0;

    for (auto &child : children)
    {
      child->computeLayout(ctx, constraints, fontCache);
      if (isVertical)
      {
        totalHeight += child->height + child->marginTop + child->marginBottom;
        localMaxW = std::max(localMaxW, child->width + child->marginLeft + child->marginRight);
      }
      else
      {
        totalWidth += child->width + child->marginLeft + child->marginRight;
        localMaxH = std::max(localMaxH, child->height + child->marginTop + child->marginBottom);
      }
    }

    int spacingTotal = children.empty() ? 0 : (int)(children.size() - 1) * spacing;

    if (isVertical)
    {
      totalHeight += spacingTotal;
      width = constraints.clampWidth(
          autoWidth ? localMaxW + paddingLeft + paddingRight : width);
      height = constraints.clampHeight(
          autoHeight ? totalHeight + paddingTop + paddingBottom : height);
    }
    else
    {
      totalWidth += spacingTotal;
      width = constraints.clampWidth(
          autoWidth ? totalWidth + paddingLeft + paddingRight : width);
      height = constraints.clampHeight(
          autoHeight ? localMaxH + paddingTop + paddingBottom : height);
    }

    applyConstraints();
    needsLayout = false;
  }

  void positionChildren(int contentX, int contentY,
                        int /*contentWidth*/, int /*contentHeight*/) override
  {
    int currentX = contentX;
    int currentY = contentY;
    for (auto &child : children)
    {
      child->x = currentX + child->marginLeft;
      child->y = currentY + child->marginTop;
      if (isVertical)
        currentY += child->height + child->marginTop + child->marginBottom + spacing;
      else
        currentX += child->width + child->marginLeft + child->marginRight + spacing;
      child->positionChildren(
          child->x + child->paddingLeft, child->y + child->paddingTop,
          child->width - child->paddingLeft - child->paddingRight,
          child->height - child->paddingTop - child->paddingBottom);
    }
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {
    if (hasBackground)
      drawRoundedRectangle(ctx);
    for (auto &child : children)
      child->render(ctx, fontCache);
    needsPaint = false;
  }

  void addRadioButton(std::shared_ptr<RadioButtonWidget> radio)
  {
    radio->parentGroup = this;
    radioButtons.push_back(radio.get());
    addChild(radio);
    if (radioButtons.size() == 1 || radio->value == selectedValue)
    {
      radio->selected = true;
      selectedValue = radio->value;
    }
  }

  void selectRadioButton(RadioButtonWidget *selectedRadio)
  {
    if (!selectedRadio)
      return;
    for (auto *radio : radioButtons)
    {
      if (radio != selectedRadio && radio->selected)
      {
        radio->selected = false;
        radio->markNeedsPaint();
      }
    }
    if (!selectedRadio->selected)
    {
      selectedRadio->selected = true;
      selectedRadio->markNeedsPaint();
    }
    selectedValue = selectedRadio->value;
    // Set the bound state FIRST.  State::set() now guards against
    // same-value re-notification, so if the app's onSelectionChanged
    // callback also calls state.set() with the same value, the second
    // call is a no-op — no double repaint, no flicker.
    if (boundStringState)
      boundStringState->set(selectedValue);
    if (onSelectionChanged)
      onSelectionChanged(selectedValue);
  }

  std::shared_ptr<RadioGroupWidget> setOrientation(bool vertical)
  {
    isVertical = vertical;
    markNeedsLayout();
    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }
  std::shared_ptr<RadioGroupWidget> setHorizontal() { return setOrientation(false); }
  std::shared_ptr<RadioGroupWidget> setVertical() { return setOrientation(true); }

  std::shared_ptr<RadioGroupWidget> setSelectedValue(const std::string &value)
  {
    selectedValue = value;
    for (auto *radio : radioButtons)
    {
      radio->selected = (radio->value == value);
      radio->markNeedsPaint();
    }
    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }
  std::shared_ptr<RadioGroupWidget>
  setOnSelectionChanged(std::function<void(const std::string &)> callback)
  {
    onSelectionChanged = callback;
    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }
  std::shared_ptr<RadioGroupWidget> bindValue(State<std::string> &state)
  {
    selectedValue = state.get();
    for (auto *radio : radioButtons)
    {
      radio->selected = (radio->value == selectedValue);
      radio->markNeedsPaint();
    }
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &val)
        {
          auto *g = static_cast<RadioGroupWidget *>(w);
          g->selectedValue = val;
          for (auto *radio : g->radioButtons)
          {
            radio->selected = (radio->value == val);
            radio->markNeedsPaint();
          }
        },
        false);
    boundStringState = &state;
    return std::static_pointer_cast<RadioGroupWidget>(shared_from_this());
  }

  std::string getSelectedValue() const { return selectedValue; }

private:
  State<std::string> *boundStringState = nullptr;
};

inline void RadioButtonWidget::selectThis()
{
  if (parentGroup)
  {
    parentGroup->selectRadioButton(this);
  }
  else
  {
    selected = !selected;
    markNeedsPaint();
    if (onClick)
      onClick();
  }
}

using RadioButtonWidgetPtr = std::shared_ptr<RadioButtonWidget>;
using RadioGroupWidgetPtr = std::shared_ptr<RadioGroupWidget>;

inline RadioButtonWidgetPtr RadioButton(const std::string &value,
                                        const std::string &label = "")
{
  auto w = std::make_shared<RadioButtonWidget>(value);
  w->text = label.empty() ? value : label;
  w->textColor = Color::fromRGB(30, 30, 30);
  return w;
}

inline RadioGroupWidgetPtr RadioGroup()
{
  return std::make_shared<RadioGroupWidget>();
}

struct RadioOption
{
  std::string value;
  std::string label;
  RadioOption(const std::string &v, const std::string &l) : value(v), label(l) {}
  RadioOption(const char *v, const char *l) : value(v), label(l) {}
};

inline RadioGroupWidgetPtr
RadioGroupWithOptions(const std::initializer_list<RadioOption> &options)
{
  auto group = std::make_shared<RadioGroupWidget>();
  for (const auto &o : options)
    group->addRadioButton(RadioButton(o.value, o.label));
  return group;
}

inline RadioGroupWidgetPtr
RadioGroupWithOptions(const std::initializer_list<std::string> &options)
{
  auto group = std::make_shared<RadioGroupWidget>();
  for (const auto &o : options)
    group->addRadioButton(RadioButton(o, o));
  return group;
}

// ============================================================================
// TextInputWidget
// ============================================================================

class TextInputWidget : public Widget
{
public:
  std::string inputValue;
  std::string placeholder;
  int cursorPos = 0;
  bool cursorVisible = true;
  TimerID cursorTimerId = 0;
  int scrollOffset = 0;

  Color focusedBorderColor = Color::fromRGB(33, 150, 243);
  Color unfocusedBorderColor = Color::fromRGB(180, 180, 180);
  Color placeholderColor = Color::fromRGB(180, 180, 180);
  Color inputTextColor = Color::fromRGB(30, 30, 30);

  TextInputWidget()
  {
    isFocusable = true;
    hasBorder = true;
    hasBackground = true;
    backgroundColor = Color::fromRGB(255, 255, 255);
    borderColor = unfocusedBorderColor;
    borderWidth = 1;
    borderRadius = 4;
    paddingLeft = paddingRight = 10;
    paddingTop = paddingBottom = 8;
    height = 36;
    autoHeight = false;
  }

  bool isTextInput() const override { return true; }

  void computeLayout(GraphicsContext &ctx,
                     const BoxConstraints &constraints,
                     FontCache &fontCache) override
  {
    if (autoWidth)
    {
      if (constraints.maxWidth >= kUnbounded)
      {
        const std::string &sample = !inputValue.empty()   ? inputValue
                                    : !placeholder.empty() ? placeholder
                                                           : std::string("Type something...");
        NativeFont font = fontCache.getFont(fontSize, fontWeight);
        int tw = 0, th = 0;
        Painter(ctx, this).measureText(toWideString(sample), font, tw, th);
        width = std::max(150, tw + paddingLeft + paddingRight + 24);
      }
      else
      {
        width = constraints.maxWidth;
      }
    }
    applyConstraints();
    needsLayout = false;
  }

  void render(GraphicsContext &ctx, FontCache &fontCache) override
  {

#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
    if (IDomAdapter *adapter = getActiveDomAdapter())
    {
        _renderDom(adapter);
        needsPaint = false;
        return;
    }
#endif
    borderColor = isFocused ? focusedBorderColor : unfocusedBorderColor;
    drawRoundedRectangle(ctx);

    Painter painter(ctx, this);

    int textX = x + paddingLeft;
    int clipW = width - paddingLeft - paddingRight;
    int clipH = height - paddingTop - paddingBottom;

    painter.pushClipRect(x + paddingLeft, y + paddingTop, clipW, clipH);

    NativeFont font = fontCache.getFont(fontSize, fontWeight);

    if (inputValue.empty() && !placeholder.empty())
    {
      std::wstring wph = toWideString(placeholder);
      painter.drawText(wph, textX, y + paddingTop, clipW, clipH, font,
                       placeholderColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {
      std::wstring winput = toWideString(inputValue);
      painter.drawText(winput, textX - scrollOffset, y + paddingTop,
                       clipW + scrollOffset, clipH, font, inputTextColor,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    if (isFocused && cursorVisible)
    {
      int tw = 0, th = 0;
      if (cursorPos > 0)
      {
        std::wstring wpre = toWideString(inputValue.c_str(), cursorPos);
        painter.measureText(wpre, font, tw, th);
      }
      int cursorX = textX + tw - scrollOffset;
      painter.drawLine(cursorX, y + paddingTop + 2,
                       cursorX, y + height - paddingBottom - 2,
                       inputTextColor, 1);
    }

    painter.popClipRect();
    needsPaint = false;
  }

  bool handleFocus(bool focused) override
  {

#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
    // Real <input> owns its own native caret/blink — no hand-rolled
    // cursor timer needed on this backend, and no VirtualKeyboard::
    // notify*() either (the real element triggers the OS's own on-screen
    // keyboard natively). Just keep isFocused in sync for anything else
    // that reads it (e.g. border color in _renderDom below).
    isFocused = focused;
    return true;
#else
    isFocused = focused;
    auto *ui = FluxUI::getCurrentInstance();

    if (focused)
    {
      cursorVisible = true;
      cursorTimerId = ui->setInterval(530, [this]()
                                      {
        cursorVisible = !cursorVisible;
        markNeedsPaint(); });
      VirtualKeyboard::notifyFocusGained(this);
    }
    else
    {
      if (cursorTimerId)
      {
        ui->clearInterval(cursorTimerId);
        cursorTimerId = 0;
      }
      cursorVisible = false;
      VirtualKeyboard::notifyFocusLost();
    }

    markNeedsPaint();
    return true;

#endif
  }

  bool handleMouseDown(int mx, int my) override
  {
    if (mx >= x && mx < x + width && my >= y && my < y + height)
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
    inputValue.insert(cursorPos, std::string(1, (char)ch));
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
    case Key::Backspace:
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
    case Key::Delete:
      if (cursorPos < (int)inputValue.size())
      {
        inputValue.erase(cursorPos, 1);
        cursorVisible = true;
        updateScroll();
        notifyStateBinding();
        return true;
      }
      break;
    case Key::Left:
      if (cursorPos > 0)
      {
        cursorPos--;
        cursorVisible = true;
        updateScroll();
        return true;
      }
      break;
    case Key::Right:
      if (cursorPos < (int)inputValue.size())
      {
        cursorPos++;
        cursorVisible = true;
        updateScroll();
        return true;
      }
      break;
    case Key::Home:
      cursorPos = 0;
      scrollOffset = 0;
      return true;
    case Key::End:
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
  std::shared_ptr<TextInputWidget> setWidth(int w)
  {
    width = w;
    autoWidth = false;
    return std::static_pointer_cast<TextInputWidget>(shared_from_this());
  }

private:


#if defined(__EMSCRIPTEN__) && defined(FLUX_WEB_RENDERER_DOM)
  void onDomInputChanged(const std::string &value) override
  {
      inputValue = value;
      // Real <input> owns real cursor position natively; this keeps our
      // own bookkeeping field roughly in sync in case anything else on
      // this widget still reads cursorPos (nothing does today, on this
      // backend, but cheap to keep coherent).
      cursorPos = (int)inputValue.size();
      notifyStateBinding();
  }

  void onDomFocusChanged(bool focused) override
  {
      if (auto *ui = FluxUI::getCurrentInstance())
          ui->setFocus(focused ? this : nullptr);
      else
          isFocused = focused;
  }

  void _renderDom(IDomAdapter *adapter)
  {
      DomNodeHandle node = fluxDomEnsureNode(this, "input");
      char buf[24];
      auto px = [&](int v) { snprintf(buf, sizeof(buf), "%dpx", v); return std::string(buf); };

      fluxDomApplyRect(this, x, y, width, height);
      adapter->setStyle(node, "box-sizing", "border-box");
      adapter->setStyle(node, "padding-left", px(paddingLeft));
      adapter->setStyle(node, "padding-right", px(paddingRight));
      adapter->setStyle(node, "font-size", px(fontSize));
      adapter->setStyle(node, "border-radius", px(borderRadius));
      adapter->setStyle(node, "border", isFocused ? "1px solid rgb(33,150,243)"
                                                  : "1px solid rgb(180,180,180)");
      adapter->setStyle(node, "background-color", "rgb(255,255,255)");
      adapter->setStyle(node, "color", "rgb(30,30,30)");
      adapter->setStyle(node, "outline", "none"); // we draw our own focus border above

      // Deliberate exception to "capture div owns all input" (see
      // flux_window_dom.cpp) — a real <input> needs REAL pointer events
      // and a real native focus target, so it stacks ABOVE the capture
      // div rather than beneath it. Only this widget's node does this.
      adapter->setStyle(node, "pointer-events", "auto");
      adapter->setStyle(node, "z-index", "2");

      adapter->setAttr(node, "placeholder", placeholder);
      adapter->setInputValue(node, inputValue);
      adapter->bindInputEvents(node, this);
  }
#endif

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
    auto *ui = FluxUI::getCurrentInstance();
    MeasureContext mc = ui->getMeasureContext();
    NativeFont font = ui->getFontCache().getFont(fontSize, fontWeight);
    int bestPos = 0, bestDist = abs(pixelX);
    for (int i = 1; i <= (int)inputValue.size(); i++)
    {
      std::wstring wpre = toWideString(inputValue.c_str(), i);
      int tw = 0, th = 0;
      Painter(mc.ctx, this).measureText(wpre, font, tw, th);
      int dist = abs(tw - pixelX);
      if (dist < bestDist)
      {
        bestDist = dist;
        bestPos = i;
      }
    }
    return bestPos;
  }

  void updateScroll()
  {
    if (inputValue.empty())
    {
      scrollOffset = 0;
      return;
    }
    auto *ui = FluxUI::getCurrentInstance();
    MeasureContext mc = ui->getMeasureContext();
    NativeFont font = ui->getFontCache().getFont(fontSize, fontWeight);
    int tw = 0, th = 0;
    if (cursorPos > 0)
    {
      std::wstring wpre = toWideString(inputValue.c_str(), cursorPos);
      Painter(mc.ctx, this).measureText(wpre, font, tw, th);
    }
    int textAreaWidth = width - paddingLeft - paddingRight;
    int cursorX = tw - scrollOffset;
    if (cursorX < 10)
      scrollOffset = std::max(0, tw - 10);
    else if (cursorX > textAreaWidth - 10)
      scrollOffset = tw - textAreaWidth + 10;
  }
};

using TextInputWidgetPtr = std::shared_ptr<TextInputWidget>;

inline TextInputWidgetPtr TextInput(const std::string &placeholder = "")
{
  auto w = std::make_shared<TextInputWidget>();
  if (!placeholder.empty())
    w->setPlaceholder(placeholder);
  return w;
}

#endif // FLUX_INPUT_HPP