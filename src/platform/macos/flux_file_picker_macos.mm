// flux_file_picker_macos.mm
//
// macOS implementation of FilePickerWidget and the Cocoa dialog backend.
// Uses NSOpenPanel / NSSavePanel — always called on the main thread via
// dispatch_async(dispatch_get_main_queue(), …) so the caller never blocks.

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "flux/widgets/flux_file_picker.hpp"
#include "flux/flux_window.hpp" 


#include <string>
#include <vector>
#include <functional>

// ============================================================================
// §1  FluxFilePickerMacOS — Cocoa panel helpers
// ============================================================================

namespace FluxFilePickerMacOS
{

// ── Extension helpers ─────────────────────────────────────────────────────

// Build an NSArray<UTType*> from a FileFilter vector for allowedContentTypes.
// Returns nil when the list is empty or contains a wildcard (allow everything).
// Requires macOS 11+, which is fine — we target macOS 11+ (Apple Silicon base).

static NSArray<UTType*>* buildAllowedContentTypes(const std::vector<FileFilter>& filters)
    API_AVAILABLE(macos(11.0))
{
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (const auto& f : filters)
    {
        for (const auto& ext : f.extensions)
        {
            // Accept "*.png", ".png", or "png"
            std::string e = ext;
            while (!e.empty() && (e[0] == '*' || e[0] == '.'))
                e = e.substr(1);
            if (e.empty() || e == "*")
                return nil; // wildcard → allow everything
            NSString* nsExt = [NSString stringWithUTF8String:e.c_str()];
            UTType* ut = [UTType typeWithFilenameExtension:nsExt];
            if (ut)
                [types addObject:ut];
        }
    }
    return types.count > 0 ? [types copy] : nil;
}

// Helper that applies the content-type filter to any NSSavePanel/NSOpenPanel.
static void applyFileFilter(NSSavePanel* panel, const std::vector<FileFilter>& filters)
    API_AVAILABLE(macos(11.0))
{
    NSArray<UTType*>* types = buildAllowedContentTypes(filters);
    if (types)
        panel.allowedContentTypes = types;
    // nil → leave unset, which lets all files through
}

// ── NSWindow accessor ─────────────────────────────────────────────────────

static NSWindow* ownerWindow()
{
    if (auto* inst = FluxUI::getCurrentInstance())
        if (auto* pw = inst->getPlatformWindowPtr())
        {
            NSView* view = (__bridge NSView*)pw->getMacNSViewPtr();
            return view.window;
        }
    return nil;
}

// ── std::string ↔ NSString ───────────────────────────────────────────────

static NSString* nsstr(const std::string& s)
{
    return s.empty() ? nil : [NSString stringWithUTF8String:s.c_str()];
}

static std::string stdstr(NSString* s)
{
    return s ? std::string([s UTF8String]) : std::string();
}

// ============================================================================
// Public async API
// ============================================================================

void pickFileAsync(const std::string& title,
                   const std::string& initialDir,
                   const std::vector<FileFilter>& filters,
                   std::function<void(std::string)> callback)
{
    // Capture by value so lifetime is safe across the dispatch.
    dispatch_async(dispatch_get_main_queue(), ^{
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles          = YES;
        panel.canChooseDirectories    = NO;
        panel.allowsMultipleSelection = NO;
        panel.resolvesAliases         = YES;

        if (NSString* t = nsstr(title))
            panel.message = t;

        if (NSString* dir = nsstr(initialDir))
            panel.directoryURL = [NSURL fileURLWithPath:dir isDirectory:YES];

        applyFileFilter(panel, filters);

        NSWindow* owner = ownerWindow();
        auto finish = ^(NSModalResponse r) {
            if (r == NSModalResponseOK && panel.URL)
                callback(stdstr(panel.URL.path));
            else
                callback({});
        };

        if (owner)
            [panel beginSheetModalForWindow:owner completionHandler:finish];
        else
            finish([panel runModal]);
    });
}

// ─────────────────────────────────────────────────────────────────────────────

void pickFilesAsync(const std::string& title,
                    const std::string& initialDir,
                    const std::vector<FileFilter>& filters,
                    std::function<void(std::vector<std::string>)> callback)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles          = YES;
        panel.canChooseDirectories    = NO;
        panel.allowsMultipleSelection = YES;
        panel.resolvesAliases         = YES;

        if (NSString* t = nsstr(title))
            panel.message = t;

        if (NSString* dir = nsstr(initialDir))
            panel.directoryURL = [NSURL fileURLWithPath:dir isDirectory:YES];

        applyFileFilter(panel, filters);

        NSWindow* owner = ownerWindow();
        auto finish = ^(NSModalResponse r) {
            std::vector<std::string> paths;
            if (r == NSModalResponseOK)
                for (NSURL* url in panel.URLs)
                    if (url.path)
                        paths.push_back(stdstr(url.path));
            callback(paths);
        };

        if (owner)
            [panel beginSheetModalForWindow:owner completionHandler:finish];
        else
            finish([panel runModal]);
    });
}

// ─────────────────────────────────────────────────────────────────────────────

void saveFileAsync(const std::string& title,
                   const std::string& initialDir,
                   const std::string& defaultFilename,
                   const std::vector<FileFilter>& filters,
                   std::function<void(std::string)> callback)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        NSSavePanel* panel = [NSSavePanel savePanel];

        if (NSString* t = nsstr(title))
            panel.message = t;

        if (NSString* dir = nsstr(initialDir))
            panel.directoryURL = [NSURL fileURLWithPath:dir isDirectory:YES];

        if (NSString* fn = nsstr(defaultFilename))
            panel.nameFieldStringValue = fn;

        applyFileFilter(panel, filters);

        NSWindow* owner = ownerWindow();
        auto finish = ^(NSModalResponse r) {
            if (r == NSModalResponseOK && panel.URL)
                callback(stdstr(panel.URL.path));
            else
                callback({});
        };

        if (owner)
            [panel beginSheetModalForWindow:owner completionHandler:finish];
        else
            finish([panel runModal]);
    });
}

// ─────────────────────────────────────────────────────────────────────────────

void pickFolderAsync(const std::string& title,
                     const std::string& initialDir,
                     std::function<void(std::string)> callback)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles          = NO;
        panel.canChooseDirectories    = YES;
        panel.allowsMultipleSelection = NO;
        panel.resolvesAliases         = YES;
        panel.canCreateDirectories    = YES;

        if (NSString* t = nsstr(title))
            panel.message = t;

        if (NSString* dir = nsstr(initialDir))
            panel.directoryURL = [NSURL fileURLWithPath:dir isDirectory:YES];

        NSWindow* owner = ownerWindow();
        auto finish = ^(NSModalResponse r) {
            if (r == NSModalResponseOK && panel.URL)
                callback(stdstr(panel.URL.path));
            else
                callback({});
        };

        if (owner)
            [panel beginSheetModalForWindow:owner completionHandler:finish];
        else
            finish([panel runModal]);
    });
}

} // namespace FluxFilePickerMacOS

// ============================================================================
// §2  FilePickerWidget — method definitions
// ============================================================================

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
    case FilePickerMode::OpenMultiple: return "Open";
    case FilePickerMode::Save:         return "Save As";
    case FilePickerMode::Folder:       return "Choose Folder";
    }
    return "Open";
}

std::string FilePickerWidget::_defaultTitle() const
{
    switch (mode_)
    {
    case FilePickerMode::Open:         return "Open File";
    case FilePickerMode::OpenMultiple: return "Open Files";
    case FilePickerMode::Save:         return "Save File";
    case FilePickerMode::Folder:       return "Select Folder";
    }
    return "Browse";
}

void FilePickerWidget::_measureLabel(GraphicsContext& ctx, FontCache& fc)
{
    NativeFont font = fc.getFont(fontFamily, labelFontSize, labelFontWeight);
    Painter p(ctx);
    int tw = 0, th = 0;
    p.measureText(toWideString(_label()), font, tw, th);
    if (autoWidth)  width  = tw;
    if (autoHeight) height = th;
}

void FilePickerWidget::_setSinglePath(const std::string& p)
{
    path_  = p;
    paths_ = {p};
    _commitPaths();
}

void FilePickerWidget::_setMultiPaths(const std::vector<std::string>& ps)
{
    paths_ = ps;
    path_  = ps.empty() ? "" : ps[0];
    _commitPaths();
}

void FilePickerWidget::_commitPaths()
{
    if (boundPath_)   boundPath_->set(path_);
    if (boundPaths_)  boundPaths_->set(paths_);
    if (onChanged_ && !path_.empty()) onChanged_(path_);
    if (onMultiChanged_) onMultiChanged_(paths_);
    markNeedsPaint();
    _repaint();
}

// ── Public API ────────────────────────────────────────────────────────────

void FilePickerWidget::open()  { _openDialog(); }

void FilePickerWidget::clear()
{
    path_ = "";
    paths_.clear();
    if (boundPath_)  boundPath_->set("");
    if (boundPaths_) boundPaths_->set({});
    if (onChanged_)  onChanged_("");
    markNeedsPaint();
    _repaint();
}

// ── Fluent configuration ──────────────────────────────────────────────────

std::shared_ptr<FilePickerWidget> FilePickerWidget::setMode(FilePickerMode m)
    { mode_ = m; return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setTitle(const std::string& t)
    { title_ = t; return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setDefaultFilename(const std::string& f)
    { defaultFilename_ = f; return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setDefaultExtension(const std::string& e)
    { defaultExt_ = (!e.empty() && e[0] == '.') ? e.substr(1) : e; return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setInitialDir(const std::string& d)
    { initialDir_ = d; return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::addFilter(const std::string& label,
                                                               std::vector<std::string> exts)
    { filters_.emplace_back(label, std::move(exts)); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::addFilter(FileFilter f)
    { filters_.push_back(std::move(f)); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setFilters(std::vector<FileFilter> fs)
    { filters_ = std::move(fs); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setHeight(int h)
    { height = h; autoHeight = false; markNeedsLayout(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setWidth(int w)
    { width = w; autoWidth = false; markNeedsLayout(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setFlex(int f)
    { flex = f; return self_(); }

// ── Label-mode styling ────────────────────────────────────────────────────

std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelTextColor(Color c)
    { labelTextColor = c; markNeedsPaint(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelHoverColor(Color c)
    { labelHoverColor = c; markNeedsPaint(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelFontSize(int s)
    { labelFontSize = s; markNeedsLayout(); return self_(); }
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelFontWeight(FontWeight w)
    { labelFontWeight = w; markNeedsLayout(); return self_(); }

// ── State bindings ────────────────────────────────────────────────────────

std::shared_ptr<FilePickerWidget> FilePickerWidget::bindPath(State<std::string>& state)
{
    path_ = state.get();
    state.bindProperty(shared_from_this(), [](Widget* w, const std::string& v) {
        auto* fp = static_cast<FilePickerWidget*>(w);
        fp->path_ = v;
        fp->markNeedsPaint();
    }, false);
    boundPath_ = &state;
    return self_();
}

std::shared_ptr<FilePickerWidget>
FilePickerWidget::bindPaths(State<std::vector<std::string>>& state)
{
    paths_ = state.get();
    if (!paths_.empty()) path_ = paths_[0];
    state.bindProperty(shared_from_this(), [](Widget* w, const std::vector<std::string>& v) {
        auto* fp = static_cast<FilePickerWidget*>(w);
        fp->paths_ = v;
        fp->path_  = v.empty() ? "" : v[0];
        fp->markNeedsPaint();
    }, false);
    boundPaths_ = &state;
    return self_();
}

// ── Callbacks ─────────────────────────────────────────────────────────────

std::shared_ptr<FilePickerWidget>
FilePickerWidget::setOnChanged(std::function<void(const std::string&)> fn)
    { onChanged_ = std::move(fn); return self_(); }

std::shared_ptr<FilePickerWidget>
FilePickerWidget::setOnMultiChanged(std::function<void(const std::vector<std::string>&)> fn)
    { onMultiChanged_ = std::move(fn); return self_(); }

std::shared_ptr<FilePickerWidget>
FilePickerWidget::setOnCancelled(std::function<void()> fn)
    { onCancelled_ = std::move(fn); return self_(); }

// ── Layout ────────────────────────────────────────────────────────────────

void FilePickerWidget::computeLayout(GraphicsContext& ctx,
                                     const BoxConstraints& constraints,
                                     FontCache& fc)
{
    if (labelWidget_)
    {
        labelWidget_->computeLayout(ctx, constraints, fc);
        if (autoWidth)  width  = labelWidget_->width;
        if (autoHeight) height = labelWidget_->height;
    }
    else
    {
        _measureLabel(ctx, fc);
    }

    width  = constraints.clampWidth(width);
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
            labelWidget_->width  - labelWidget_->paddingLeft - labelWidget_->paddingRight,
            labelWidget_->height - labelWidget_->paddingTop  - labelWidget_->paddingBottom);
    }
}

// ── Render ────────────────────────────────────────────────────────────────

void FilePickerWidget::render(GraphicsContext& ctx, FontCache& fontCache)
{
    if (!visible) return;

    if (labelWidget_)
    {
        labelWidget_->isHovered = isHovered;
        labelWidget_->render(ctx, fontCache);
    }
    else
    {
        Color col = (isHovered && isFocusable) ? labelHoverColor : labelTextColor;
        NativeFont font = fontCache.getFont(fontFamily, labelFontSize, labelFontWeight);
        Painter(ctx).drawTextA(_label(), x, y, width, height, font, col,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
}

// ── Input events ──────────────────────────────────────────────────────────

bool FilePickerWidget::handleMouseDown(int mx, int my)
{
    if (!_inBounds(mx, my)) return false;
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
// Delegate to PlatformWindow::invalidate() so it goes through the macOS
// dispatch_async path the same way all other redraws do.

void FilePickerWidget::_repaint()
{
    needsPaint = true;
    if (auto* ui = FluxUI::getCurrentInstance())
        ui->getPlatformWindow().invalidate();
}

// ── Platform-specific: _openDialog ───────────────────────────────────────

void FilePickerWidget::_openDialog()
{
    using namespace FluxFilePickerMacOS;

    const std::string title = title_.empty() ? _defaultTitle() : title_;
    auto self = self_();

    switch (mode_)
    {
    case FilePickerMode::Open:
        pickFileAsync(title, initialDir_, filters_, [self](std::string p) {
            if (p.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
            self->_setSinglePath(p);
        });
        break;

    case FilePickerMode::OpenMultiple:
        pickFilesAsync(title, initialDir_, filters_, [self](std::vector<std::string> ps) {
            if (ps.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
            self->_setMultiPaths(ps);
        });
        break;

    case FilePickerMode::Save:
        saveFileAsync(title, initialDir_, defaultFilename_, filters_, [self](std::string p) {
            if (p.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
            // Append default extension if the user typed a bare name.
            if (!self->defaultExt_.empty() && p.find('.') == std::string::npos)
                p += '.' + self->defaultExt_;
            self->_setSinglePath(p);
        });
        break;

    case FilePickerMode::Folder:
        pickFolderAsync(title, initialDir_, [self](std::string p) {
            if (p.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
            self->_setSinglePath(p);
        });
        break;
    }
}

#endif // TARGET_OS_OSX
#endif // __APPLE__