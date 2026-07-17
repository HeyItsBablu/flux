// flux_file_picker_ssr.cpp
//
// SSR (headless) implementation of FilePickerWidget.
//
// There is no browser, no OS, and no user on the other end of an SSR
// render — file dialogs have no meaning here. This file exists purely to
// satisfy the linker: every Widget-facing method FilePickerWidget declares
// (setMode, addFilter, bindPath, computeLayout, render, handleMouseDown,
// ...) needs a definition in every build that includes flux_file_picker.hpp,
// and flux_file_picker_web.cpp's definitions are compiled only under
// __EMSCRIPTEN__. Mirrors that file's §2 section 1:1; only _openDialog()
// differs (immediate no-op cancellation instead of an async browser flow).

#ifdef FLUX_SSR

#include "flux/flux_core.hpp"
#include "flux/widgets/flux_file_picker.hpp"

// ── Shared helpers ────────────────────────────────────────────────────────

std::shared_ptr<FilePickerWidget> FilePickerWidget::self_()
{
    return std::static_pointer_cast<FilePickerWidget>(shared_from_this());
}

bool FilePickerWidget::_inBounds(int mx, int my) const
{
    return mx >= x && mx < x + width && my >= y && my < y + height;
}

std::string FilePickerWidget::_label() const
{
    if (!customLabel_.empty())
        return customLabel_;
    switch (mode_)
    {
    case FilePickerMode::Open:
    case FilePickerMode::OpenMultiple:
        return "Open";
    case FilePickerMode::Save:
        return "Save As";
    case FilePickerMode::Folder:
        return "Choose Folder";
    }
    return "Open";
}

std::string FilePickerWidget::_defaultTitle() const
{
    switch (mode_)
    {
    case FilePickerMode::Open:
        return "Open File";
    case FilePickerMode::OpenMultiple:
        return "Open Files";
    case FilePickerMode::Save:
        return "Save File";
    case FilePickerMode::Folder:
        return "Select Folder";
    }
    return "Browse";
}

void FilePickerWidget::_measureLabel(GraphicsContext &ctx, FontCache &fc)
{
    NativeFont font = fc.getFont(fontFamily, labelFontSize, labelFontWeight);
    Painter p(ctx, this);
    int tw = 0, th = 0;
    p.measureText(toWideString(_label()), font, tw, th);
    if (autoWidth)
        width = tw;
    if (autoHeight)
        height = th;
}

void FilePickerWidget::_setSinglePath(const std::string &p)
{
    path_ = p;
    paths_ = {p};
    _commitPaths();
}

void FilePickerWidget::_setMultiPaths(const std::vector<std::string> &ps)
{
    paths_ = ps;
    path_ = ps.empty() ? "" : ps[0];
    _commitPaths();
}

void FilePickerWidget::_commitPaths()
{
    if (boundPath_)
        boundPath_->set(path_);
    if (boundPaths_)
        boundPaths_->set(paths_);
    if (onChanged_ && !path_.empty())
        onChanged_(path_);
    if (onMultiChanged_)
        onMultiChanged_(paths_);
    markNeedsPaint();
    _repaint();
}

// ── Public API ────────────────────────────────────────────────────────────

void FilePickerWidget::open() { _openDialog(); }

void FilePickerWidget::clear()
{
    path_ = "";
    paths_.clear();
    if (boundPath_)
        boundPath_->set("");
    if (boundPaths_)
        boundPaths_->set({});
    if (onChanged_)
        onChanged_("");
    markNeedsPaint();
    _repaint();
}

// ── Fluent configuration ──────────────────────────────────────────────────

std::shared_ptr<FilePickerWidget> FilePickerWidget::setMode(FilePickerMode m) { mode_ = m; return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setTitle(const std::string &t) { title_ = t; return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setDefaultFilename(const std::string &f) { defaultFilename_ = f; return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setDefaultExtension(const std::string &e)
{
    defaultExt_ = (!e.empty() && e[0] == '.') ? e.substr(1) : e;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setInitialDir(const std::string &d) { initialDir_ = d; return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::addFilter(const std::string &label, std::vector<std::string> exts)
{
    filters_.emplace_back(label, std::move(exts));
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::addFilter(FileFilter f) { filters_.push_back(std::move(f)); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setFilters(std::vector<FileFilter> fs) { filters_ = std::move(fs); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setHeight(int h) { height = h; autoHeight = false; markNeedsLayout(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setWidth(int w) { width = w; autoWidth = false; markNeedsLayout(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setFlex(int f) { flex = f; return self_(); }

std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelTextColor(Color c) { labelTextColor = c; markNeedsPaint(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelHoverColor(Color c) { labelHoverColor = c; markNeedsPaint(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelFontSize(int s) { labelFontSize = s; markNeedsLayout(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelFontWeight(FontWeight w) { labelFontWeight = w; markNeedsLayout(); return self_(); }

std::shared_ptr<FilePickerWidget> FilePickerWidget::bindPath(State<std::string> &state)
{
    path_ = state.get();
    state.bindProperty(shared_from_this(), [](Widget *w, const std::string &v)
                       {
      auto *fp = static_cast<FilePickerWidget *>(w);
      fp->path_ = v;
      fp->markNeedsPaint(); }, false);
    boundPath_ = &state;
    return self_();
}

std::shared_ptr<FilePickerWidget> FilePickerWidget::bindPaths(State<std::vector<std::string>> &state)
{
    paths_ = state.get();
    if (!paths_.empty())
        path_ = paths_[0];
    state.bindProperty(shared_from_this(), [](Widget *w, const std::vector<std::string> &v)
                       {
      auto *fp = static_cast<FilePickerWidget *>(w);
      fp->paths_ = v;
      fp->path_  = v.empty() ? "" : v[0];
      fp->markNeedsPaint(); }, false);
    boundPaths_ = &state;
    return self_();
}

std::shared_ptr<FilePickerWidget> FilePickerWidget::setOnChanged(std::function<void(const std::string &)> fn) { onChanged_ = std::move(fn); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setOnMultiChanged(std::function<void(const std::vector<std::string> &)> fn) { onMultiChanged_ = std::move(fn); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setOnCancelled(std::function<void()> fn) { onCancelled_ = std::move(fn); return self_(); }

// ── Layout ────────────────────────────────────────────────────────────────

void FilePickerWidget::computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints, FontCache &fc)
{
    if (labelWidget_)
    {
        labelWidget_->computeLayout(ctx, constraints, fc);
        if (autoWidth) width = labelWidget_->width;
        if (autoHeight) height = labelWidget_->height;
    }
    else
    {
        _measureLabel(ctx, fc);
    }

    width = constraints.clampWidth(width);
    height = constraints.clampHeight(height);
    applyConstraints();
    needsLayout = false;

    if (labelWidget_)
    {
        labelWidget_->x = x;
        labelWidget_->y = y;
        labelWidget_->positionChildren(
            x + labelWidget_->paddingLeft,
            y + labelWidget_->paddingTop,
            labelWidget_->width - labelWidget_->paddingLeft - labelWidget_->paddingRight,
            labelWidget_->height - labelWidget_->paddingTop - labelWidget_->paddingBottom);
    }
}

// ── Render ────────────────────────────────────────────────────────────────
//
// FLUX_SSR routes drawText/fillRect/etc. through IDomAdapter (see
// flux_painter_dom.cpp), same as every other widget on this backend — no
// SSR-specific painting logic needed here at all.

void FilePickerWidget::render(GraphicsContext &ctx, FontCache &fontCache)
{
    if (!visible)
        return;

    if (labelWidget_)
    {
        labelWidget_->isHovered = isHovered;
        labelWidget_->render(ctx, fontCache);
    }
    else
    {
        Color col = (isHovered && isFocusable) ? labelHoverColor : labelTextColor;
        NativeFont font = fontCache.getFont(fontFamily, labelFontSize, labelFontWeight);
        Painter(ctx, this).drawTextA(_label(), x, y, width, height, font, col,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
}

// ── Input events ──────────────────────────────────────────────────────────
//
// SSR has no live pointer/keyboard — these exist only so the shared Widget
// event-dispatch code paths compile and link. handleMouseDown still calls
// _openDialog() for symmetry with every other backend, but _openDialog()
// below just reports cancellation immediately.

bool FilePickerWidget::handleMouseDown(int mx, int my)
{
    if (!_inBounds(mx, my))
        return false;
    _openDialog();
    return true;
}

bool FilePickerWidget::handleMouseMove(int mx, int my)
{
    lastMx_ = mx;
    lastMy_ = my;
    bool nowHovered = _inBounds(mx, my);
    if (nowHovered != isHovered)
    {
        isHovered = nowHovered;
        markNeedsPaint();
    }
    return false;
}

bool FilePickerWidget::handleMouseLeave()
{
    lastMx_ = lastMy_ = -9999;
    isHovered = false;
    markNeedsPaint();
    return false;
}

bool FilePickerWidget::handleKeyDown(int key)
{
    if (key == Key::Return || key == Key::Space)
    {
        _openDialog();
        return true;
    }
    return false;
}

// ── Platform-specific: _repaint ───────────────────────────────────────────

void FilePickerWidget::_repaint()
{
    needsPaint = true;
    if (auto *ui = FluxUI::getCurrentInstance())
        ui->getPlatformWindow().invalidate();
}

// ── Platform-specific: _openDialog ───────────────────────────────────────
//
// No dialog, no filesystem access, no user — a headless render has nothing
// to show and nothing to pick. Every mode reports cancellation immediately,
// the same convention FluxFilePickerWeb::pickFolderAsync already uses for
// "no supported equivalent on this backend".

void FilePickerWidget::_openDialog()
{
    if (onCancelled_)
        onCancelled_();
}

#endif // FLUX_SSR