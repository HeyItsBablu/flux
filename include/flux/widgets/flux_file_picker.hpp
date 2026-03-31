#ifndef FLUX_FILE_PICKER_HPP
#define FLUX_FILE_PICKER_HPP

#include "flux_core.hpp"
#include "flux_layout.hpp"
#include "flux_state.hpp"

#include <commdlg.h>
#include <shlobj.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#include <functional>
#include <string>
#include <vector>

// ============================================================================
// FILE FILTER
// ============================================================================
//
// Describes one entry in the file-type dropdown of the OS dialog.
//
// Usage:
//   FileFilter("Images",       {"*.png", "*.jpg", "*.jpeg", "*.bmp"})
//   FileFilter("Text files",   {"*.txt", "*.md"})
//   FileFilter("All files",    {"*.*"})
//
// The extensions are joined with semicolons internally:
//   "Images (*.png;*.jpg)\0*.png;*.jpg\0"
// ============================================================================

struct FileFilter {
  std::string label;                   // e.g. "PNG Images"
  std::vector<std::string> extensions; // e.g. {"*.png", "*.jpg"}
  bool showExtsInLabel = true;         // "PNG Images (*.png;*.jpg)"

  FileFilter(const std::string &lbl, std::vector<std::string> exts)
      : label(lbl), extensions(std::move(exts)) {}
};

// ============================================================================
// FILE PICKER MODE
// ============================================================================

enum class FilePickerMode {
  Open,         // single file open
  OpenMultiple, // multiple files open
  Save,         // save / export
  Folder,       // folder picker (uses SHBrowseForFolder)
};

// ============================================================================
// FILE PICKER WIDGET
// ============================================================================
//
// A button-like widget that opens the OS file dialog when clicked.
// The selected path (or paths) flow into bound State<> variables and
// fire an onChanged callback.
//
// Include:  #include "flux/flux_file_picker.hpp"
//
// ── Single file open ─────────────────────────────────────────────────────────
//
//   State<std::string> filePath("", context);
//
//   FilePicker()
//       ->setMode(FilePickerMode::Open)
//       ->addFilter("Images",    {"*.png","*.jpg","*.jpeg","*.bmp","*.tga"})
//       ->addFilter("PNG",       {"*.png"})
//       ->addFilter("JPEG",      {"*.jpg","*.jpeg"})
//       ->addFilter("All files", {"*.*"})
//       ->setDefaultExtension("png")
//       ->bindPath(filePath)
//       ->setOnChanged([](const std::string &path) {
//           std::cout << "Picked: " << path << std::endl;
//       });
//
// ── Save dialog ──────────────────────────────────────────────────────────────
//
//   FilePicker()
//       ->setMode(FilePickerMode::Save)
//       ->setTitle("Export Image")
//       ->setDefaultFilename("edited.png")
//       ->addFilter("PNG",  {"*.png"})
//       ->addFilter("JPEG", {"*.jpg","*.jpeg"})
//       ->setDefaultExtension("png")
//       ->bindPath(exportPath)
//       ->setOnChanged([&](const std::string &p) { surface->exportImage(p); });
//
// ── Multiple files ───────────────────────────────────────────────────────────
//
//   State<std::vector<std::string>> paths({}, context);
//
//   FilePicker()
//       ->setMode(FilePickerMode::OpenMultiple)
//       ->addFilter("Images", {"*.png","*.jpg"})
//       ->bindPaths(paths)
//       ->setOnMultiChanged([](const std::vector<std::string> &ps) { ... });
//
// ── Folder picker ────────────────────────────────────────────────────────────
//
//   FilePicker()
//       ->setMode(FilePickerMode::Folder)
//       ->setTitle("Select output folder")
//       ->bindPath(folderPath);
//
// ── Custom button label
// ───────────────────────────────────────────────────────
//
//   FilePicker("📂 Open Image")   // custom label
//   FilePicker()                  // auto-label from mode
//
// ── Embedding the path display
// ────────────────────────────────────────────────
//
//   FilePicker()
//       ->setShowPath(true)       // shows selected path beside the button
//       ->setPathMaxWidth(260)
//
// ============================================================================

class FilePickerWidget : public Widget {
public:
  // ── Appearance ────────────────────────────────────────────────────────────
  Color btnBgColor = Color::fromRGB(245, 247, 250);
  Color btnHoverColor = Color::fromRGB(224, 235, 248);
  Color btnBorderColor = Color::fromRGB(200, 204, 210);
  Color btnTextColor = Color::fromRGB(30, 30, 30);
  Color pathTextColor = Color::fromRGB(80, 80, 90);
  Color placeholderColor = Color::fromRGB(160, 160, 170);
  Color accentColor = Color::fromRGB(33, 150, 243);

  int btnHeight = 32;
  int btnPadding = 10;
  int borderRadius = 5;
  int pathMaxWidth = 300;
  bool showPath = true;     // show selected path beside button
  bool showClearBtn = true; // × button to clear selection

  // ── Constructor ───────────────────────────────────────────────────────────
  explicit FilePickerWidget(const std::string &label = "")
      : customLabel_(label) {
    autoHeight = false;
    autoWidth = true;
    height = btnHeight;
    isFocusable = true;
  }

  // ── Public API ────────────────────────────────────────────────────────────

  // Open the OS dialog programmatically (same as clicking the button)
  void open() { _openDialog(); }

  // Clear the current selection
  void clear() {
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

  // Read current selection
  const std::string &path() const { return path_; }
  const std::vector<std::string> &paths() const { return paths_; }
  bool hasSelection() const { return !path_.empty(); }

  // ── Fluent configuration ──────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget> setMode(FilePickerMode m) {
    mode_ = m;
    _autoLabel();
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setTitle(const std::string &t) {
    title_ = t;
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setDefaultFilename(const std::string &f) {
    defaultFilename_ = f;
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setDefaultExtension(const std::string &e) {
    // strip leading dot if present
    defaultExt_ = (!e.empty() && e[0] == '.') ? e.substr(1) : e;
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setInitialDir(const std::string &d) {
    initialDir_ = d;
    return self_();
  }
  std::shared_ptr<FilePickerWidget> addFilter(const std::string &label,
                                              std::vector<std::string> exts) {
    filters_.push_back(FileFilter(label, std::move(exts)));
    return self_();
  }
  std::shared_ptr<FilePickerWidget> addFilter(FileFilter f) {
    filters_.push_back(std::move(f));
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setFilters(std::vector<FileFilter> fs) {
    filters_ = std::move(fs);
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setShowPath(bool v) {
    showPath = v;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setShowClearBtn(bool v) {
    showClearBtn = v;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setPathMaxWidth(int w) {
    pathMaxWidth = w;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setAccentColor(Color c) {
    accentColor = c;
    markNeedsPaint();
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setHeight(int h) {
    height = h;
    btnHeight = h;
    autoHeight = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setWidth(int w) {
    width = w;
    autoWidth = false;
    markNeedsLayout();
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setFlex(int f) {
    flex = f;
    return self_();
  }

  // ── State bindings ────────────────────────────────────────────────────────

  // Single path — two-way
  std::shared_ptr<FilePickerWidget> bindPath(State<std::string> &state) {
    path_ = state.get();
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::string &v) {
          auto *fp = static_cast<FilePickerWidget *>(w);
          fp->path_ = v;
          fp->markNeedsPaint();
        },
        false);
    boundPath_ = &state;
    return self_();
  }

  // Multiple paths — two-way
  std::shared_ptr<FilePickerWidget>
  bindPaths(State<std::vector<std::string>> &state) {
    paths_ = state.get();
    if (!paths_.empty())
      path_ = paths_[0];
    state.bindProperty(
        shared_from_this(),
        [](Widget *w, const std::vector<std::string> &v) {
          auto *fp = static_cast<FilePickerWidget *>(w);
          fp->paths_ = v;
          fp->path_ = v.empty() ? "" : v[0];
          fp->markNeedsPaint();
        },
        false);
    boundPaths_ = &state;
    return self_();
  }

  // ── Callbacks ─────────────────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget>
  setOnChanged(std::function<void(const std::string &)> fn) {
    onChanged_ = std::move(fn);
    return self_();
  }
  std::shared_ptr<FilePickerWidget>
  setOnMultiChanged(std::function<void(const std::vector<std::string> &)> fn) {
    onMultiChanged_ = std::move(fn);
    return self_();
  }
  std::shared_ptr<FilePickerWidget> setOnCancelled(std::function<void()> fn) {
    onCancelled_ = std::move(fn);
    return self_();
  }

  // ── Layout ────────────────────────────────────────────────────────────────

  void computeLayout(GraphicsContext & /*ctx*/,
                     const BoxConstraints &constraints,
                     FontCache & /*fc*/) override {
    if (autoWidth)
      width = constraints.maxWidth;
    height = btnHeight;
    applyConstraints();
    needsLayout = false;

    // Measure button width
    btnW_ = _measureBtnWidth();
    clearW_ = (showClearBtn && hasSelection()) ? btnHeight : 0;
  }

  void positionChildren(int, int, int, int) override {}

  // ── Render ────────────────────────────────────────────────────────────────

void render(GraphicsContext &ctx, FontCache &fontCache) override {
    if (!visible) return;

    Painter painter(ctx);

    // ── Button ────────────────────────────────────────────────────────────
    bool hov = isHovered && !_isOverClear(lastMx_, lastMy_);
    Color bg  = hov ? btnHoverColor : btnBgColor;
    Color bdr = isFocused ? accentColor : btnBorderColor;

    painter.fillRoundedRectGDI(x, y, btnW_, height, borderRadius * 2,
                               bg, bdr, 1);

    // Button label
    painter.drawTextA(_label(), x + btnPadding, y,
                      btnW_ - btnPadding * 2, height,
                      fontCache.getFont(fontSize, FontWeight::Normal),
                      btnTextColor,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // ── Path display ──────────────────────────────────────────────────────
    if (showPath) {
        int pathX     = x + btnW_ + 8;
        int pathRight = x + width - clearW_ - (clearW_ ? 4 : 0);

        std::string displayPath  = _displayPath();
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

    // ── Clear (×) button ─────────────────────────────────────────────────
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

  // ── Mouse events ──────────────────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override {
    if (!_inBounds(mx, my))
      return false;

    if (showClearBtn && hasSelection() && _isOverClear(mx, my)) {
      clear();
      return true;
    }

    if (mx >= x && mx < x + btnW_ && my >= y && my < y + height) {
      _openDialog();
      return true;
    }
    return false;
  }

  bool handleMouseMove(int mx, int my) override {
    lastMx_ = mx;
    lastMy_ = my;
    bool nowHovered = _inBounds(mx, my);
    if (nowHovered != isHovered) {
      isHovered = nowHovered;
      markNeedsPaint();
    } else {
      markNeedsPaint(); // repaint for clear-btn hover
    }
    return false;
  }

  bool handleMouseLeave() override {
    lastMx_ = lastMy_ = -9999;
    isHovered = false;
    markNeedsPaint();
    return false;
  }

  bool handleKeyDown(int key) override {
    if (key == Key::Return || key == Key::Space) {
      _openDialog();
      return true;
    }
    if (key == Key::Delete || key == Key::Backspace) {
      clear();
      return true;
    }
    return false;
  }

private:
  // ── Config ────────────────────────────────────────────────────────────────
  FilePickerMode mode_ = FilePickerMode::Open;
  std::string title_;
  std::string customLabel_;
  std::string defaultFilename_;
  std::string defaultExt_;
  std::string initialDir_;
  std::vector<FileFilter> filters_;

  // ── Selection ─────────────────────────────────────────────────────────────
  std::string path_;
  std::vector<std::string> paths_;

  // ── State bindings ────────────────────────────────────────────────────────
  State<std::string> *boundPath_ = nullptr;
  State<std::vector<std::string>> *boundPaths_ = nullptr;

  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void(const std::string &)> onChanged_;
  std::function<void(const std::vector<std::string> &)> onMultiChanged_;
  std::function<void()> onCancelled_;

  // ── Layout cache ──────────────────────────────────────────────────────────
  int btnW_ = 120;
  int clearW_ = 0;
  int lastMx_ = -9999, lastMy_ = -9999;

  // ── Helpers ───────────────────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget> self_() {
    return std::static_pointer_cast<FilePickerWidget>(shared_from_this());
  }

  bool _inBounds(int mx, int my) const {
    return mx >= x && mx < x + width && my >= y && my < y + height;
  }

  bool _isOverClear(int mx, int my) const {
    if (!showClearBtn || !hasSelection() || clearW_ == 0)
      return false;
    int cx = x + width - clearW_;
    return mx >= cx && mx < cx + clearW_ && my >= y && my < y + height;
  }

  void _autoLabel() {
    if (!customLabel_.empty())
      return;
    switch (mode_) {
    case FilePickerMode::Open:
      customLabel_ = "";
      break; // use _label()
    case FilePickerMode::OpenMultiple:
      customLabel_ = "";
      break;
    case FilePickerMode::Save:
      customLabel_ = "";
      break;
    case FilePickerMode::Folder:
      customLabel_ = "";
      break;
    }
  }

  std::string _label() const {
    if (!customLabel_.empty())
      return customLabel_;
    switch (mode_) {
    case FilePickerMode::Open:
      return "Browse...";
    case FilePickerMode::OpenMultiple:
      return "Browse...";
    case FilePickerMode::Save:
      return "Save As...";
    case FilePickerMode::Folder:
      return "Choose Folder...";
    }
    return "Browse...";
  }

  std::string _placeholder() const {
    switch (mode_) {
    case FilePickerMode::Open:
    case FilePickerMode::OpenMultiple:
      return "No file selected";
    case FilePickerMode::Save:
      return "No destination chosen";
    case FilePickerMode::Folder:
      return "No folder selected";
    }
    return "No file selected";
  }

  // Returns just the filename (or folder name) for path display
  std::string _displayPath() const {
    if (mode_ == FilePickerMode::OpenMultiple && paths_.size() > 1) {
      return std::to_string(paths_.size()) + " files selected";
    }
    if (path_.empty())
      return "";
    // Find last backslash or forward slash
    size_t pos = path_.find_last_of("\\/");
    return (pos != std::string::npos) ? path_.substr(pos + 1) : path_;
  }

  int _measureBtnWidth() const {
    // Approximate: label chars × ~7px + padding.  Good enough without HDC.
    int labelW = (int)_label().size() * 7 + btnPadding * 2;
    return std::max(80, labelW);
  }

  // ── Build OPENFILENAME filter string ──────────────────────────────────────
  //
  // Format required by Windows:
  //   "Description\0*.ext1;*.ext2\0Description2\0*.ext3\0\0"
  //
  // We build this into a wchar_t vector (wide API avoids codepage issues).

  std::vector<wchar_t> _buildFilterW() const {
    std::vector<wchar_t> result;

    auto appendW = [&](const std::wstring &s) {
      for (wchar_t c : s)
        result.push_back(c);
      result.push_back(L'\0');
    };

    for (const auto &f : filters_) {
      // Join extensions with semicolons: "*.png;*.jpg"
      std::string extJoined;
      for (size_t i = 0; i < f.extensions.size(); i++) {
        if (i > 0)
          extJoined += ";";
        extJoined += f.extensions[i];
      }

      // Label: "Images (*.png;*.jpg)"  or just "Images"
      std::string lbl = f.label;
      if (f.showExtsInLabel && !extJoined.empty())
        lbl += " (" + extJoined + ")";

      // Convert to wide
      std::wstring wLbl(lbl.begin(), lbl.end());
      std::wstring wExt(extJoined.begin(), extJoined.end());
      appendW(wLbl);
      appendW(wExt);
    }

    if (filters_.empty()) {
      appendW(L"All Files (*.*)");
      appendW(L"*.*");
    }

    result.push_back(L'\0'); // double-null terminator
    return result;
  }



  std::string _toUtf8(const wchar_t *w) const {
    if (!w || !*w)
      return {};
    int len =
        WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], len, nullptr, nullptr);
    // Remove null terminator that WideCharToMultiByte appends
    if (!s.empty() && s.back() == '\0')
      s.pop_back();
    return s;
  }

  // ── Open the OS dialog ────────────────────────────────────────────────────

  void _openDialog() {
    HWND hw = _hwnd();

    if (mode_ == FilePickerMode::Folder) {
      _openFolderDialog(hw);
    } else if (mode_ == FilePickerMode::Save) {
      _openSaveDialog(hw);
    } else {
      _openFileDialog(hw, mode_ == FilePickerMode::OpenMultiple);
    }
  }

  void _openFileDialog(HWND owner, bool multi) {
    auto filter = _buildFilterW();

    std::wstring wTitle = toWideString(title_.empty() ? "Open File" : title_);
    std::wstring wInitDir = toWideString(initialDir_);
    std::wstring wDefExt = toWideString(defaultExt_);
    // Buffer: for multi-select we need a large buffer
    //   First MAX_PATH chars = directory, then null-separated filenames,
    //   terminated by double null.
    const int bufSize = multi ? 32768 : MAX_PATH;
    std::vector<wchar_t> buf(bufSize, L'\0');

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.data();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = bufSize;
    ofn.lpstrTitle = wTitle.empty() ? nullptr : wTitle.c_str();
    ofn.lpstrInitialDir = wInitDir.empty() ? nullptr : wInitDir.c_str();
    ofn.lpstrDefExt = wDefExt.empty() ? nullptr : wDefExt.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (multi)
      ofn.Flags |= OFN_ALLOWMULTISELECT;

    if (!GetOpenFileNameW(&ofn)) {
      if (onCancelled_)
        onCancelled_();
      return;
    }

    if (multi) {
      _parseMultiSelect(buf.data());
    } else {
      std::string picked = _toUtf8(buf.data());
      _setSinglePath(picked);
    }
  }

  void _openSaveDialog(HWND owner) {
    auto filter = _buildFilterW();
std::wstring wTitle    = toWideString(title_.empty() ? "Save File" : title_);
std::wstring wInitDir  = toWideString(initialDir_);
std::wstring wDefExt   = toWideString(defaultExt_);
std::wstring wDefault  = toWideString(defaultFilename_);

    std::vector<wchar_t> buf(MAX_PATH, L'\0');
    if (!wDefault.empty()) {
      size_t copyLen = std::min(wDefault.size(), size_t(MAX_PATH - 1));
      memcpy(buf.data(), wDefault.c_str(), copyLen * sizeof(wchar_t));
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.data();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = wTitle.empty() ? nullptr : wTitle.c_str();
    ofn.lpstrInitialDir = wInitDir.empty() ? nullptr : wInitDir.c_str();
    ofn.lpstrDefExt = wDefExt.empty() ? nullptr : wDefExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) {
      if (onCancelled_)
        onCancelled_();
      return;
    }

    std::string picked = _toUtf8(buf.data());
    _setSinglePath(picked);
  }

  void _openFolderDialog(HWND owner) {
    wchar_t buf[MAX_PATH] = {};
std::wstring wTitle = toWideString(title_.empty() ? "Select Folder" : title_);

    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = wTitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
      if (onCancelled_)
        onCancelled_();
      return;
    }

    SHGetPathFromIDListW(pidl, buf);
    CoTaskMemFree(pidl);

    std::string picked = _toUtf8(buf);
    _setSinglePath(picked);
  }

  // OFN multi-select buffer format:
  //   dir\0file1\0file2\0\0   (multiple files)
  //   fullpath\0\0            (single file — falls back)
  void _parseMultiSelect(const wchar_t *buf) {
    // First token = directory (or full path if only one file selected)
    std::wstring dir(buf);
    const wchar_t *ptr = buf + dir.size() + 1;

    if (*ptr == L'\0') {
      // Single file — buf contains full path
      _setSinglePath(_toUtf8(buf));
      return;
    }

    paths_.clear();
    while (*ptr != L'\0') {
      std::wstring fname(ptr);
      std::wstring full = dir + L"\\" + fname;
      paths_.push_back(_toUtf8(full.c_str()));
      ptr += fname.size() + 1;
    }

    path_ = paths_.empty() ? "" : paths_[0];

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

  void _setSinglePath(const std::string &p) {
    path_ = p;
    paths_ = {p};

    if (boundPath_)
      boundPath_->set(p);
    if (boundPaths_)
      boundPaths_->set(paths_);
    if (onChanged_)
      onChanged_(p);
    if (onMultiChanged_)
      onMultiChanged_(paths_);
    markNeedsPaint();
    _repaint();
  }

  void _repaint() {
    needsPaint = true;
    HWND hw = _hwnd();
    if (hw) {
      RECT r = {x, y, x + width, y + height};
      InvalidateRect(hw, &r, FALSE);
    }
  }

  HWND _hwnd() const {
    auto *ui = FluxUI::getCurrentInstance();
    return ui ? ui->getWindow() : nullptr;
  }
};

// ============================================================================
// FACTORY
// ============================================================================

using FilePickerWidgetPtr = std::shared_ptr<FilePickerWidget>;

inline FilePickerWidgetPtr FilePicker(const std::string &label = "") {
  return std::make_shared<FilePickerWidget>(label);
}

#endif // FLUX_FILE_PICKER_HPP