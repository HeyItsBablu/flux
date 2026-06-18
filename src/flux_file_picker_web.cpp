// flux_file_picker_web.cpp
//
// Web (Emscripten) implementation of FilePickerWidget and the
// FluxFilePickerWeb dialog backend.
//
// Browsers deliberately hide real filesystem paths, so this file's job is
// to keep FilePickerWidget's existing contract — "you get a path back,
// fopen() it like any other path" — working as closely as the platform
// allows:
//
//   Open / Open Multiple — a hidden <input type="file"> is the dialog.
//     Selected files are read with FileReader and written into Emscripten's
//     MEMFS virtual filesystem; the MEMFS path is what gets returned, so
//     downstream fopen()-based code (image decoders, etc.) doesn't need to
//     know anything changed.
//
//   Save — there is no native save dialog in a browser tab, so none is
//     shown. A scratch MEMFS path is handed back immediately so the caller
//     can write to it exactly like it would on desktop. Once that write
//     finishes, the bytes are read back and downloaded via a synthetic
//     <a download> click — the closest a browser gets to "Save As".
//
//   Folder — intentionally unsupported. Always reports cancellation.
//
// Async/lifetime note: file selection resolves on a later browser event
// (the <input>'s "change"), so _openDialog() captures a shared_ptr to the
// widget (self_()) in its completion lambda. That keeps the widget alive
// across the wait even if it's removed from the tree before the user
// finishes picking a file.
//
// Build requirements: this relies on Module.ccall (same runtime export
// already needed for Module.cwrap in main_web.cpp — make sure
// -sEXPORTED_RUNTIME_METHODS includes "ccall"), and on Emscripten's MEMFS
// being linked in (the default; only an issue if the build passes
// -sFILESYSTEM=0).

#ifdef __EMSCRIPTEN__

#include "flux/flux_core.hpp"
#include "flux/widgets/flux_file_picker.hpp"

#include <emscripten.h>

#include <cctype>
#include <cerrno>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

// ============================================================================
// §1  FluxFilePickerWeb — browser-backed dialog wrappers
// ============================================================================

namespace
{
    // ── Pending request registry ──────────────────────────────────────────
    //
    // An <input type="file"> selection resolves asynchronously (the "change"
    // event fires on a later turn of the browser event loop), so each open
    // request gets an id; JS calls back into the bridge functions below by
    // id as files are read, then signals completion.

    struct PendingRequest
    {
        std::vector<std::string> paths;
        std::function<void(std::vector<std::string>, bool)> onComplete; // (paths, cancelled)
    };

    std::unordered_map<int, PendingRequest> g_pending;
    int g_nextRequestId = 1;

    // ── Scratch directories ───────────────────────────────────────────────

    void ensureDir(const char *path)
    {
        if (::mkdir(path, 0777) != 0 && errno != EEXIST)
        {
            // Non-fatal: FS.writeFile below will surface a clearer error if the
            // directory genuinely isn't usable.
        }
    }

    std::string sanitizeFilename(const std::string &name)
    {
        std::string out = name.empty() ? "download" : name;
        for (char &c : out)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_'))
                c = '_';
        return out;
    }

    std::string makeScratchSavePath(const std::string &filename)
    {
        static int counter = 0;
        ensureDir("/flux_web_saves");
        return "/flux_web_saves/" + std::to_string(++counter) + "_" + sanitizeFilename(filename);
    }

    // ── Accept-attribute builder ──────────────────────────────────────────
    //
    // FileFilter extensions arrive as glob fragments like "*.png" (same form
    // the Win32 filter builder consumes). <input accept> wants ".png". A bare
    // "*" or "*.*" filter means "any file" — translate that to no restriction
    // at all rather than a literal accept value.

    std::string buildAcceptString(const std::vector<FileFilter> &filters)
    {
        std::vector<std::string> tokens;
        for (const auto &f : filters)
        {
            for (const auto &raw : f.extensions)
            {
                if (raw == "*" || raw == "*.*")
                    continue;
                std::string e = raw;
                size_t star = e.find('*');
                if (star != std::string::npos)
                    e.erase(star, 1);
                if (!e.empty() && e[0] != '.')
                    e = "." + e;
                if (!e.empty())
                    tokens.push_back(e);
            }
        }
        std::string accept;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            if (i)
                accept += ',';
            accept += tokens[i];
        }
        return accept;
    }

} // namespace

// ── JS → C++ bridge ──────────────────────────────────────────────────────
//
// Called from the EM_ASM blocks below via Module.ccall().

extern "C"
{
    EMSCRIPTEN_KEEPALIVE
    void fluxFilePickerWebAddPath(int requestId, const char *path)
    {
        auto it = g_pending.find(requestId);
        if (it != g_pending.end() && path)
            it->second.paths.push_back(path);
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxFilePickerWebFinish(int requestId, int cancelled)
    {
        auto it = g_pending.find(requestId);
        if (it == g_pending.end())
            return;
        auto onComplete = std::move(it->second.onComplete);
        auto paths = std::move(it->second.paths);
        g_pending.erase(it);
        if (onComplete)
            onComplete(paths, cancelled != 0);
    }
}

namespace
{
    // ── Shared <input type="file"> driver ───────────────────────────────────

    void openFileInputAsync(const std::vector<FileFilter> &filters, bool allowMultiple,
                            std::function<void(std::vector<std::string>, bool)> onComplete)
    {
        ensureDir("/flux_web_uploads");

        int requestId = g_nextRequestId++;
        PendingRequest req;
        req.onComplete = std::move(onComplete);
        g_pending[requestId] = std::move(req);

        std::string accept = buildAcceptString(filters);

        EM_ASM({
      var requestId = $0;
      var multiple  = $1;
      var accept    = UTF8ToString($2);

      var input = document.createElement('input');
      input.type = 'file';
      input.style.display = 'none';
      if (multiple) input.multiple = true;
      if (accept.length > 0) input.accept = accept;
      document.body.appendChild(input);

      var settled = false;

      var cleanup = function() {
        input.removeEventListener('change', onChange);
        input.removeEventListener('cancel', onCancel);
        document.body.removeChild(input);
      };

      var finish = function(cancelled) {
        if (settled) return;
        settled = true;
        cleanup();
        Module.ccall('fluxFilePickerWebFinish', null, ['number', 'number'], [requestId, cancelled]);
      };

      var onCancel = function() { finish(1); };

      var onChange = function() {
        var files = input.files;
        if (!files || files.length === 0) { finish(1); return; }

        var remaining = files.length;
        var settleOne = function() {
          remaining--;
          if (remaining === 0) finish(0);
        };

        for (var i = 0; i < files.length; i++) {
          (function(file) {
            var reader = new FileReader();
            reader.onerror = settleOne;
            reader.onload = function() {
              try {
                var bytes = new Uint8Array(reader.result);
                var n = (Module._fluxUploadCounter = (Module._fluxUploadCounter || 0) + 1);
                var safeName = file.name.replace(/[^a-zA-Z0-9._-]/g, '_');
                var path = '/flux_web_uploads/' + n + '_' + safeName;
                FS.writeFile(path, bytes);
                Module.ccall('fluxFilePickerWebAddPath', null, ['number', 'string'], [requestId, path]);
              } catch (e) {
                console.warn('FilePicker: failed to stage uploaded file', e);
              }
              settleOne();
            };
            reader.readAsArrayBuffer(file);
          })(files[i]);
        }
      };

      input.addEventListener('change', onChange);
      // Not universally supported (notably Safari) — where it's missing, a
      // true cancel just leaves this request pending until the widget is
      // asked to open again.
      input.addEventListener('cancel', onCancel);

      input.click(); }, requestId, allowMultiple ? 1 : 0, accept.c_str());
    }

    void triggerBrowserDownload(const std::string &path, const std::string &filename)
    {
        EM_ASM({
      try {
        var bytes = FS.readFile(UTF8ToString($0));
        var blob  = new Blob([bytes]);
        var url   = URL.createObjectURL(blob);
        var a     = document.createElement('a');
        a.href = url;
        a.download = UTF8ToString($1);
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
      } catch (e) {
        console.warn('FilePicker: nothing was written to the save path, skipping download', e);
      } }, path.c_str(), filename.c_str());
    }

} // namespace

namespace FluxFilePickerWeb
{

    void pickFileAsync(const std::string & /*title*/, const std::vector<FileFilter> &filters,
                       std::function<void(std::string)> callback)
    {
        openFileInputAsync(filters, false,
                           [callback](std::vector<std::string> paths, bool /*cancelled*/)
                           { callback(paths.empty() ? std::string() : paths[0]); });
    }

    void pickFilesAsync(const std::string & /*title*/, const std::vector<FileFilter> &filters,
                        std::function<void(std::vector<std::string>)> callback)
    {
        openFileInputAsync(filters, true,
                           [callback](std::vector<std::string> paths, bool /*cancelled*/)
                           { callback(paths); });
    }

    void saveFileAsync(const std::string & /*title*/, const std::string &defaultFilename,
                       const std::vector<FileFilter> & /*filters*/,
                       std::function<void(std::string)> callback)
    {
        std::string filename = defaultFilename.empty() ? "download" : defaultFilename;
        std::string path = makeScratchSavePath(filename);

        // No dialog to show — hand the scratch path straight to the caller, who
        // is expected to write the export synchronously (mirrors how a desktop
        // Save callback writes to the path it gets back).
        if (callback)
            callback(path);

        triggerBrowserDownload(path, filename);
    }

    void pickFolderAsync(const std::string & /*title*/, std::function<void(std::string)> callback)
    {
        // No supported web equivalent — report cancellation immediately, the
        // same convention every other backend uses for an empty result.
        if (callback)
            callback(std::string());
    }

} // namespace FluxFilePickerWeb

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
    Painter p(ctx);
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

std::shared_ptr<FilePickerWidget> FilePickerWidget::setMode(FilePickerMode m)
{
    mode_ = m;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setTitle(const std::string &t)
{
    title_ = t;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setDefaultFilename(const std::string &f)
{
    defaultFilename_ = f;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setDefaultExtension(const std::string &e)
{
    defaultExt_ = (!e.empty() && e[0] == '.') ? e.substr(1) : e;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setInitialDir(const std::string &d)
{
    initialDir_ = d;
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::addFilter(const std::string &label,
                                                              std::vector<std::string> exts)
{
    filters_.emplace_back(label, std::move(exts));
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::addFilter(FileFilter f)
{
    filters_.push_back(std::move(f));
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setFilters(std::vector<FileFilter> fs)
{
    filters_ = std::move(fs);
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setHeight(int h)
{
    height = h;
    autoHeight = false;
    markNeedsLayout();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setWidth(int w)
{
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setFlex(int f)
{
    flex = f;
    return self_();
}

// ── Label-mode styling ────────────────────────────────────────────────────

std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelTextColor(Color c)
{
    labelTextColor = c;
    markNeedsPaint();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelHoverColor(Color c)
{
    labelHoverColor = c;
    markNeedsPaint();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelFontSize(int s)
{
    labelFontSize = s;
    markNeedsLayout();
    return self_();
}
std::shared_ptr<FilePickerWidget> FilePickerWidget::setLabelFontWeight(FontWeight w)
{
    labelFontWeight = w;
    markNeedsLayout();
    return self_();
}

// ── State bindings ────────────────────────────────────────────────────────

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

std::shared_ptr<FilePickerWidget>
FilePickerWidget::bindPaths(State<std::vector<std::string>> &state)
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

// ── Callbacks ─────────────────────────────────────────────────────────────

std::shared_ptr<FilePickerWidget>
FilePickerWidget::setOnChanged(std::function<void(const std::string &)> fn)
{
    onChanged_ = std::move(fn);
    return self_();
}
std::shared_ptr<FilePickerWidget>
FilePickerWidget::setOnMultiChanged(std::function<void(const std::vector<std::string> &)> fn)
{
    onMultiChanged_ = std::move(fn);
    return self_();
}
std::shared_ptr<FilePickerWidget>
FilePickerWidget::setOnCancelled(std::function<void()> fn)
{
    onCancelled_ = std::move(fn);
    return self_();
}

// ── Layout ────────────────────────────────────────────────────────────────

void FilePickerWidget::computeLayout(GraphicsContext &ctx,
                                     const BoxConstraints &constraints,
                                     FontCache &fc)
{
    if (labelWidget_)
    {
        labelWidget_->computeLayout(ctx, constraints, fc);
        if (autoWidth)
            width = labelWidget_->width;
        if (autoHeight)
            height = labelWidget_->height;
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
        Painter(ctx).drawTextA(_label(), x, y, width, height, font, col,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
}

// ── Input events ──────────────────────────────────────────────────────────

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

void FilePickerWidget::_openDialog()
{
    using namespace FluxFilePickerWeb;

    const std::string title = title_.empty() ? _defaultTitle() : title_;

    switch (mode_)
    {
    case FilePickerMode::Open:
        pickFileAsync(title, filters_,
                      [self = self_()](std::string path)
                      {
                          if (path.empty())
                          {
                              if (self->onCancelled_)
                                  self->onCancelled_();
                              return;
                          }
                          self->_setSinglePath(path);
                      });
        break;

    case FilePickerMode::OpenMultiple:
        pickFilesAsync(title, filters_,
                       [self = self_()](std::vector<std::string> paths)
                       {
                           if (paths.empty())
                           {
                               if (self->onCancelled_)
                                   self->onCancelled_();
                               return;
                           }
                           self->_setMultiPaths(paths);
                       });
        break;

    case FilePickerMode::Save:
        saveFileAsync(title, defaultFilename_, filters_,
                      [self = self_()](std::string path)
                      { self->_setSinglePath(path); });
        break;

    case FilePickerMode::Folder:
        pickFolderAsync(title,
                        [self = self_()](std::string /*path*/)
                        {
                            if (self->onCancelled_)
                                self->onCancelled_();
                        });
        break;
    }
}

#endif // __EMSCRIPTEN__