#ifndef FLUX_FILE_PICKER_HPP
#define FLUX_FILE_PICKER_HPP

#include "../flux_core.hpp"
#include "flux_layout.hpp"
#include "../flux_state.hpp"


// ============================================================================
// FILE FILTER  — must be defined before any platform backend is included
// ============================================================================

struct FileFilter {
  std::string              label;
  std::vector<std::string> extensions;
  bool                     showExtsInLabel = true;

  FileFilter(const std::string& lbl, std::vector<std::string> exts)
      : label(lbl), extensions(std::move(exts)) {}
};

// ============================================================================
// FILE PICKER MODE — likewise needed by backends
// ============================================================================

enum class FilePickerMode {
  Open,
  OpenMultiple,
  Save,
  Folder,
};



// ============================================================================
// PLATFORM BACKENDS
// Each backend header is fully self-contained: it owns its own system
// includes, helper functions, and lives inside its own namespace.
//
//   flux_file_picker_win32.hpp   — GetOpenFileNameW / SHBrowseForFolderW
//   flux_file_picker_linux.hpp   — zenity / kdialog via popen()
//   flux_file_picker_android.hpp — ACTION_OPEN_DOCUMENT Intent + JNI bridge
// ============================================================================

#ifdef _WIN32
#  include "../flux_file_picker_win32.hpp"
#elif defined(__ANDROID__)
#  include "../flux_file_picker_android.hpp"
#elif defined(__linux__)
#  include "../flux_file_picker_linux.hpp"
#endif

#include <functional>
#include <string>
#include <vector>



// ============================================================================
// FILE PICKER WIDGET
// ============================================================================
//
// A button-like widget that opens the native OS file dialog when clicked.
// Selected paths flow into bound State<> variables and fire callbacks.
//
// Cross-platform:
//   Windows  — GetOpenFileNameW / GetSaveFileNameW / SHBrowseForFolderW
//   Linux    — zenity (GNOME) or kdialog (KDE), auto-detected at runtime
//   Android  — ACTION_OPEN_DOCUMENT / ACTION_CREATE_DOCUMENT Intent
//
// ── Single file open ─────────────────────────────────────────────────────────
//
//   State<std::string> filePath("", context);
//
//   FilePicker()
//       ->setMode(FilePickerMode::Open)
//       ->addFilter("Images", {"*.png","*.jpg","*.jpeg","*.bmp"})
//       ->addFilter("All files", {"*.*"})
//       ->setDefaultExtension("png")
//       ->bindPath(filePath)
//       ->setOnChanged([](const std::string& path) {
//           std::cout << "Picked: " << path << "\n";
//       });
//
// ── Save dialog ──────────────────────────────────────────────────────────────
//
//   FilePicker()
//       ->setMode(FilePickerMode::Save)
//       ->setTitle("Export Image")
//       ->setDefaultFilename("output.png")
//       ->addFilter("PNG",  {"*.png"})
//       ->addFilter("JPEG", {"*.jpg","*.jpeg"})
//       ->setDefaultExtension("png")
//       ->bindPath(exportPath)
//       ->setOnChanged([&](const std::string& p) { surface->exportImage(p); });
//
// ── Multiple files ───────────────────────────────────────────────────────────
//
//   State<std::vector<std::string>> paths({}, context);
//
//   FilePicker()
//       ->setMode(FilePickerMode::OpenMultiple)
//       ->addFilter("Images", {"*.png","*.jpg"})
//       ->bindPaths(paths)
//       ->setOnMultiChanged([](const std::vector<std::string>& ps) { ... });
//
// ── Folder picker ────────────────────────────────────────────────────────────
//
//   FilePicker()
//       ->setMode(FilePickerMode::Folder)
//       ->setTitle("Select output folder")
//       ->bindPath(folderPath);
//
// ============================================================================

class FilePickerWidget : public Widget {
public:
  // ── Appearance ────────────────────────────────────────────────────────────
  Color btnBgColor       = Color::fromRGB(245, 247, 250);
  Color btnHoverColor    = Color::fromRGB(224, 235, 248);
  Color btnBorderColor   = Color::fromRGB(200, 204, 210);
  Color btnTextColor     = Color::fromRGB(30,  30,  30);
  Color pathTextColor    = Color::fromRGB(80,  80,  90);
  Color placeholderColor = Color::fromRGB(160, 160, 170);
  Color accentColor      = Color::fromRGB(33,  150, 243);

  int  btnHeight    = 32;
  int  btnPadding   = 10;
  int  borderRadius = 5;
  int  pathMaxWidth = 300;
  bool showPath     = true;
  bool showClearBtn = true;

  // ── Constructor ───────────────────────────────────────────────────────────
  explicit FilePickerWidget(const std::string& label = "")
      : customLabel_(label) {
    autoHeight  = false;
    autoWidth   = true;
    height      = btnHeight;
    isFocusable = true;
  }

  // ── Public API ────────────────────────────────────────────────────────────

  void open()  { _openDialog(); }

  void clear() {
    path_  = "";
    paths_.clear();
    if (boundPath_)  boundPath_->set("");
    if (boundPaths_) boundPaths_->set({});
    if (onChanged_)  onChanged_("");
    markNeedsPaint();
    _repaint();
  }

  const std::string&              path()  const { return path_;  }
  const std::vector<std::string>& paths() const { return paths_; }
  bool hasSelection() const { return !path_.empty(); }

  // ── Fluent configuration ──────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget> setMode(FilePickerMode m) {
    mode_ = m; return self_();
  }
  std::shared_ptr<FilePickerWidget> setTitle(const std::string& t) {
    title_ = t; return self_();
  }
  std::shared_ptr<FilePickerWidget> setDefaultFilename(const std::string& f) {
    defaultFilename_ = f; return self_();
  }
  std::shared_ptr<FilePickerWidget> setDefaultExtension(const std::string& e) {
    defaultExt_ = (!e.empty() && e[0] == '.') ? e.substr(1) : e;
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setInitialDir(const std::string& d) {
    initialDir_ = d; return self_();
  }
  std::shared_ptr<FilePickerWidget> addFilter(const std::string& label,
                                               std::vector<std::string> exts) {
    filters_.emplace_back(label, std::move(exts)); return self_();
  }
  std::shared_ptr<FilePickerWidget> addFilter(FileFilter f) {
    filters_.push_back(std::move(f)); return self_();
  }
  std::shared_ptr<FilePickerWidget> setFilters(std::vector<FileFilter> fs) {
    filters_ = std::move(fs); return self_();
  }
  std::shared_ptr<FilePickerWidget> setShowPath(bool v) {
    showPath = v; markNeedsLayout(); return self_();
  }
  std::shared_ptr<FilePickerWidget> setShowClearBtn(bool v) {
    showClearBtn = v; markNeedsLayout(); return self_();
  }
  std::shared_ptr<FilePickerWidget> setPathMaxWidth(int w) {
    pathMaxWidth = w; markNeedsLayout(); return self_();
  }
  std::shared_ptr<FilePickerWidget> setAccentColor(Color c) {
    accentColor = c; markNeedsPaint(); return self_();
  }
  std::shared_ptr<FilePickerWidget> setHeight(int h) {
    height = h; btnHeight = h; autoHeight = false;
    markNeedsLayout(); return self_();
  }
  std::shared_ptr<FilePickerWidget> setWidth(int w) {
    width = w; autoWidth = false; markNeedsLayout(); return self_();
  }
  std::shared_ptr<FilePickerWidget> setFlex(int f) {
    flex = f; return self_();
  }

  // ── State bindings ────────────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget> bindPath(State<std::string>& state) {
    path_ = state.get();
    state.bindProperty(
        shared_from_this(),
        [](Widget* w, const std::string& v) {
          auto* fp = static_cast<FilePickerWidget*>(w);
          fp->path_ = v;
          fp->markNeedsPaint();
        },
        false);
    boundPath_ = &state;
    return self_();
  }

  std::shared_ptr<FilePickerWidget>
  bindPaths(State<std::vector<std::string>>& state) {
    paths_ = state.get();
    if (!paths_.empty()) path_ = paths_[0];
    state.bindProperty(
        shared_from_this(),
        [](Widget* w, const std::vector<std::string>& v) {
          auto* fp = static_cast<FilePickerWidget*>(w);
          fp->paths_ = v;
          fp->path_  = v.empty() ? "" : v[0];
          fp->markNeedsPaint();
        },
        false);
    boundPaths_ = &state;
    return self_();
  }

  // ── Callbacks ─────────────────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget>
  setOnChanged(std::function<void(const std::string&)> fn) {
    onChanged_ = std::move(fn); return self_();
  }
  std::shared_ptr<FilePickerWidget>
  setOnMultiChanged(std::function<void(const std::vector<std::string>&)> fn) {
    onMultiChanged_ = std::move(fn); return self_();
  }
  std::shared_ptr<FilePickerWidget>
  setOnCancelled(std::function<void()> fn) {
    onCancelled_ = std::move(fn); return self_();
  }

  // ── Layout ────────────────────────────────────────────────────────────────

  void computeLayout(GraphicsContext& /*ctx*/,
                     const BoxConstraints& constraints,
                     FontCache& /*fc*/) override {
    if (autoWidth) width = constraints.maxWidth;
    height = btnHeight;
    applyConstraints();
    needsLayout = false;
    btnW_   = _measureBtnWidth();
    clearW_ = (showClearBtn && hasSelection()) ? btnHeight : 0;
  }

  void positionChildren(int, int, int, int) override {}

  // ── Render ────────────────────────────────────────────────────────────────

  void render(GraphicsContext& ctx, FontCache& fontCache) override {
    if (!visible) return;

    Painter painter(ctx);

    // Button
    bool  hov = isHovered && !_isOverClear(lastMx_, lastMy_);
    Color bg  = hov ? btnHoverColor : btnBgColor;
    Color bdr = isFocused ? accentColor : btnBorderColor;

    painter.fillRoundedRectGDI(x, y, btnW_, height, borderRadius * 2,
                               bg, bdr, 1);
    painter.drawTextA(_label(), x + btnPadding, y,
                      btnW_ - btnPadding * 2, height,
                      fontCache.getFont(fontSize, FontWeight::Normal),
                      btnTextColor,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Path display
    if (showPath) {
      int pathX     = x + btnW_ + 8;
      int pathRight = x + width - clearW_ - (clearW_ ? 4 : 0);

      std::string displayPath   = _displayPath();
      bool        isPlaceholder = displayPath.empty() || path_.empty();

      painter.pushClipRect(pathX, y, pathRight - pathX, height);
      painter.drawTextA(
          isPlaceholder ? _placeholder() : displayPath,
          pathX, y, pathRight - pathX, height,
          fontCache.getFont(fontSize - 1, FontWeight::Normal),
          isPlaceholder ? placeholderColor : pathTextColor,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      painter.popClipRect();
    }

    // Clear (×) button
    if (showClearBtn && hasSelection() && clearW_ > 0) {
      int  cx       = x + width - clearW_;
      bool clearHov = _isOverClear(lastMx_, lastMy_);

      painter.fillRoundedRectGDI(cx, y, clearW_, height, borderRadius * 2,
                                 clearHov ? btnHoverColor : btnBgColor,
                                 btnBorderColor, 1);
      painter.drawTextA("x", cx, y, clearW_, height,
                        fontCache.getFont(11, FontWeight::Normal),
                        clearHov ? Color::fromRGB(200, 60, 60)
                                 : Color::fromRGB(140, 140, 150),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    needsPaint = false;
  }

  // ── Mouse / keyboard events ───────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override {
    if (!_inBounds(mx, my)) return false;
    if (showClearBtn && hasSelection() && _isOverClear(mx, my)) {
      clear(); return true;
    }
    if (mx >= x && mx < x + btnW_ && my >= y && my < y + height) {
      _openDialog(); return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    lastMx_ = mx; lastMy_ = my;
    bool nowHovered = _inBounds(mx, my);
    if (nowHovered != isHovered) isHovered = nowHovered;
    markNeedsPaint();
    return false;
  }

  bool handleMouseLeave() override {
    lastMx_ = lastMy_ = -9999;
    isHovered = false;
    markNeedsPaint();
    return false;
  }

  bool handleKeyDown(int key) override {
    if (key == Key::Return || key == Key::Space) { _openDialog(); return true; }
    if (key == Key::Delete || key == Key::Backspace) { clear(); return true; }
    return false;
  }

private:
  // ── Config fields ─────────────────────────────────────────────────────────
  FilePickerMode          mode_            = FilePickerMode::Open;
  std::string             title_;
  std::string             customLabel_;
  std::string             defaultFilename_;
  std::string             defaultExt_;
  std::string             initialDir_;
  std::vector<FileFilter> filters_;

  // ── Selection ─────────────────────────────────────────────────────────────
  std::string              path_;
  std::vector<std::string> paths_;

  // ── Bindings ──────────────────────────────────────────────────────────────
  State<std::string>*              boundPath_  = nullptr;
  State<std::vector<std::string>>* boundPaths_ = nullptr;

  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void(const std::string&)>              onChanged_;
  std::function<void(const std::vector<std::string>&)> onMultiChanged_;
  std::function<void()>                                onCancelled_;

  // ── Layout cache ──────────────────────────────────────────────────────────
  int btnW_   = 120;
  int clearW_ = 0;
  int lastMx_ = -9999, lastMy_ = -9999;

  // ── Utilities ─────────────────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget> self_() {
    return std::static_pointer_cast<FilePickerWidget>(shared_from_this());
  }

  bool _inBounds(int mx, int my) const {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  bool _isOverClear(int mx, int my) const {
    if (!showClearBtn || !hasSelection() || clearW_ == 0) return false;
    int cx = x + width - clearW_;
    return mx >= cx && mx < cx + clearW_ && my >= y && my < y + height;
  }

  std::string _label() const {
    if (!customLabel_.empty()) return customLabel_;
    switch (mode_) {
    case FilePickerMode::Open:         return "Browse...";
    case FilePickerMode::OpenMultiple: return "Browse...";
    case FilePickerMode::Save:         return "Save As...";
    case FilePickerMode::Folder:       return "Choose Folder...";
    }
    return "Browse...";
  }

  std::string _placeholder() const {
    switch (mode_) {
    case FilePickerMode::Open:
    case FilePickerMode::OpenMultiple: return "No file selected";
    case FilePickerMode::Save:         return "No destination chosen";
    case FilePickerMode::Folder:       return "No folder selected";
    }
    return "No file selected";
  }

  std::string _displayPath() const {
    if (mode_ == FilePickerMode::OpenMultiple && paths_.size() > 1)
      return std::to_string(paths_.size()) + " files selected";
    if (path_.empty()) return "";
    size_t pos = path_.find_last_of("\\/");
    return (pos != std::string::npos) ? path_.substr(pos + 1) : path_;
  }

  int _measureBtnWidth() const {
    return std::max(80, static_cast<int>(_label().size()) * 7 + btnPadding * 2);
  }

  std::string _defaultTitle() const {
    switch (mode_) {
    case FilePickerMode::Open:         return "Open File";
    case FilePickerMode::OpenMultiple: return "Open Files";
    case FilePickerMode::Save:         return "Save File";
    case FilePickerMode::Folder:       return "Select Folder";
    }
    return "Browse";
  }

  // ── Path commit (shared, platform-neutral) ────────────────────────────────

  void _setSinglePath(const std::string& p) {
    path_ = p; paths_ = {p}; _commitPaths();
  }

  void _setMultiPaths(const std::vector<std::string>& ps) {
    paths_ = ps; path_ = ps.empty() ? "" : ps[0]; _commitPaths();
  }

  void _commitPaths() {
    if (boundPath_)  boundPath_->set(path_);
    if (boundPaths_) boundPaths_->set(paths_);
    if (onChanged_ && !path_.empty()) onChanged_(path_);
    if (onMultiChanged_)              onMultiChanged_(paths_);
    markNeedsPaint();
    _repaint();
  }

  void _repaint() {
    needsPaint = true;
#ifdef _WIN32
    // Win32 needs an explicit InvalidateRect; SDL/EGL platforms handle this
    // automatically via the dirty flag raised by markNeedsPaint().
    if (auto* ui = FluxUI::getCurrentInstance()) {
      HWND hw = ui->getWindow();
      if (hw) {
        RECT r = {x, y, x + width, y + height};
        InvalidateRect(hw, &r, FALSE);
      }
    }
#endif
  }

  // ==========================================================================
  // Platform dispatch
  // All dialog logic lives in the backend headers — nothing platform-specific
  // appears below except the preprocessor guard and a one-line call.
  // ==========================================================================

  void _openDialog() {
#ifdef _WIN32
    _openDialogWin32();
#elif defined(__ANDROID__)
    _openDialogAndroid();
#elif defined(__linux__)
    _openDialogLinux();
#else
    if (onCancelled_) onCancelled_();
#endif
  }

  // ==========================================================================
  // Windows — delegates entirely to FluxFilePickerWin32::*
  // ==========================================================================

#ifdef _WIN32

  void _openDialogWin32() {
    using namespace FluxFilePickerWin32;

    const std::string title = title_.empty() ? _defaultTitle() : title_;
    HWND owner = getOwnerHwnd();

    PickResult result;
    switch (mode_) {
    case FilePickerMode::Open:
      result = openFile(owner, title, initialDir_, defaultExt_, filters_);
      break;
    case FilePickerMode::OpenMultiple:
      result = openFiles(owner, title, initialDir_, defaultExt_, filters_);
      break;
    case FilePickerMode::Save:
      result = saveFile(owner, title, initialDir_,
                        defaultFilename_, defaultExt_, filters_);
      break;
    case FilePickerMode::Folder:
      result = pickFolder(owner, title);
      break;
    }

    if (result.cancelled()) { if (onCancelled_) onCancelled_(); return; }

    if (mode_ == FilePickerMode::OpenMultiple) _setMultiPaths(result.paths);
    else                                       _setSinglePath(result.paths[0]);
  }

#endif // _WIN32

  // ==========================================================================
  // Android — delegates entirely to FluxFilePickerAndroid::launch*
  // ==========================================================================

#ifdef __ANDROID__

  void _openDialogAndroid() {
    using namespace FluxFilePickerAndroid;

    auto self = self_(); // keep widget alive across the async result

    auto deliver = [self](std::vector<std::string> paths) {
      if (paths.empty()) {
        if (self->onCancelled_) self->onCancelled_();
        return;
      }
      if (self->mode_ == FilePickerMode::OpenMultiple) self->_setMultiPaths(paths);
      else                                             self->_setSinglePath(paths[0]);
    };

    const std::string title = title_.empty() ? _defaultTitle() : title_;

    switch (mode_) {
    case FilePickerMode::Open:
      launchOpenIntent(title, filters_, /*multi=*/false, std::move(deliver));
      break;
    case FilePickerMode::OpenMultiple:
      launchOpenIntent(title, filters_, /*multi=*/true,  std::move(deliver));
      break;
    case FilePickerMode::Save:
      launchSaveIntent(title, defaultFilename_, filters_, std::move(deliver));
      break;
    case FilePickerMode::Folder:
      launchFolderIntent(title, std::move(deliver));
      break;
    }
  }

#endif // __ANDROID__

  // ==========================================================================
  // Linux — delegates entirely to linuxPick* / linuxSaveFile
  // ==========================================================================

#if defined(__linux__) && !defined(__ANDROID__)

  void _openDialogLinux() {
    const std::string title = title_.empty() ? _defaultTitle() : title_;

    switch (mode_) {
    case FilePickerMode::Open: {
      std::string p = linuxPickFile(title, initialDir_, filters_);
      if (p.empty()) { if (onCancelled_) onCancelled_(); return; }
      _setSinglePath(p);
      break;
    }
    case FilePickerMode::OpenMultiple: {
      auto ps = linuxPickFiles(title, initialDir_, filters_);
      if (ps.empty()) { if (onCancelled_) onCancelled_(); return; }
      _setMultiPaths(ps);
      break;
    }
    case FilePickerMode::Save: {
      std::string p = linuxSaveFile(title, initialDir_, defaultFilename_, filters_);
      if (p.empty()) { if (onCancelled_) onCancelled_(); return; }
      if (!defaultExt_.empty() && p.find('.') == std::string::npos)
        p += '.' + defaultExt_;
      _setSinglePath(p);
      break;
    }
    case FilePickerMode::Folder: {
      std::string p = linuxPickFolder(title, initialDir_);
      if (p.empty()) { if (onCancelled_) onCancelled_(); return; }
      _setSinglePath(p);
      break;
    }
    }
  }

#endif // __linux__
};

// ============================================================================
// FACTORY
// ============================================================================

using FilePickerWidgetPtr = std::shared_ptr<FilePickerWidget>;

inline FilePickerWidgetPtr FilePicker(const std::string& label = "") {
  return std::make_shared<FilePickerWidget>(label);
}

#endif // FLUX_FILE_PICKER_HPP