// flux_file_picker_win32.cpp
//
// Windows implementation of FilePickerWidget and the FluxFilePickerWin32
// dialog backend.

#ifdef _WIN32

#include <windows.h> // must be first — shlobj.h and commdlg.h depend on it
#include <commdlg.h>
#include <shlobj.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#include "flux/flux_core.hpp"
#include "flux/widgets/flux_file_picker.hpp"

#include <algorithm>
#include <string>
#include <vector>

// ============================================================================
// §1  FluxFilePickerWin32 — native dialog wrappers
// ============================================================================

namespace FluxFilePickerWin32
{

  // ── String conversion helpers ─────────────────────────────────────────────

  static std::wstring toWide(const std::string &s)
  {
    if (s.empty())
      return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0')
      w.pop_back();
    return w;
  }

  static std::string toUtf8(const wchar_t *w)
  {
    if (!w || !*w)
      return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0')
      s.pop_back();
    return s;
  }

  // ── Filter string builder ─────────────────────────────────────────────────
  //
  // OPENFILENAMEW expects a double-null-terminated wide string:
  //   "Description\0*.ext1;*.ext2\0Description2\0*.ext3\0\0"

  static std::vector<wchar_t> buildFilterW(const std::vector<FileFilter> &filters)
  {
    std::vector<wchar_t> result;

    auto append = [&](const std::wstring &s)
    {
      for (wchar_t c : s)
        result.push_back(c);
      result.push_back(L'\0');
    };

    for (const auto &f : filters)
    {
      std::string exts;
      for (size_t i = 0; i < f.extensions.size(); ++i)
      {
        if (i)
          exts += ';';
        exts += f.extensions[i];
      }
      std::string label = f.label;
      if (f.showExtsInLabel && !exts.empty())
        label += " (" + exts + ")";
      append(toWide(label));
      append(toWide(exts));
    }

    if (filters.empty())
    {
      append(L"All Files (*.*)");
      append(L"*.*");
    }

    result.push_back(L'\0'); // final double-null terminator
    return result;
  }

  // ── Open — single file ────────────────────────────────────────────────────

  PickResult openFile(void *ownerVoid, const std::string &title,
                      const std::string &initialDir,
                      const std::string &defaultExt,
                      const std::vector<FileFilter> &filters)
  {
    HWND owner = static_cast<HWND>(ownerVoid);
    auto filter = buildFilterW(filters);

    std::wstring wTitle = toWide(title.empty() ? "Open File" : title);
    std::wstring wInitDir = toWide(initialDir);
    std::wstring wDefExt = toWide(defaultExt);

    std::vector<wchar_t> buf(MAX_PATH, L'\0');

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.data();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = wTitle.empty() ? nullptr : wTitle.c_str();
    ofn.lpstrInitialDir = wInitDir.empty() ? nullptr : wInitDir.c_str();
    ofn.lpstrDefExt = wDefExt.empty() ? nullptr : wDefExt.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn))
      return {};
    return {{toUtf8(buf.data())}};
  }

  // ── Open — multiple files ─────────────────────────────────────────────────
  //
  // OFN multi-select buffer layout (Explorer mode):
  //   <dir>\0<file1>\0<file2>\0\0  (multiple selections)
  //   <fullpath>\0\0               (single selection)

  PickResult openFiles(void *ownerVoid, const std::string &title,
                       const std::string &initialDir,
                       const std::string &defaultExt,
                       const std::vector<FileFilter> &filters)
  {
    HWND owner = static_cast<HWND>(ownerVoid);
    auto filter = buildFilterW(filters);

    std::wstring wTitle = toWide(title.empty() ? "Open Files" : title);
    std::wstring wInitDir = toWide(initialDir);
    std::wstring wDefExt = toWide(defaultExt);

    const int bufSize = 32768; // enough for ~200 typical paths
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
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_EXPLORER | OFN_ALLOWMULTISELECT;

    if (!GetOpenFileNameW(&ofn))
      return {};

    std::wstring dir(buf.data());
    const wchar_t *ptr = buf.data() + dir.size() + 1;

    // Single file — buffer already holds the full path
    if (*ptr == L'\0')
      return {{toUtf8(buf.data())}};

    // Multiple files — dir\0file1\0file2\0\0
    PickResult result;
    while (*ptr != L'\0')
    {
      std::wstring fname(ptr);
      result.paths.push_back(toUtf8((dir + L'\\' + fname).c_str()));
      ptr += fname.size() + 1;
    }
    return result;
  }

  // ── Save ──────────────────────────────────────────────────────────────────

  PickResult saveFile(void *ownerVoid, const std::string &title,
                      const std::string &initialDir,
                      const std::string &defaultFilename,
                      const std::string &defaultExt,
                      const std::vector<FileFilter> &filters)
  {
    HWND owner = static_cast<HWND>(ownerVoid);
    auto filter = buildFilterW(filters);

    std::wstring wTitle = toWide(title.empty() ? "Save File" : title);
    std::wstring wInitDir = toWide(initialDir);
    std::wstring wDefExt = toWide(defaultExt);
    std::wstring wDefault = toWide(defaultFilename);

    std::vector<wchar_t> buf(MAX_PATH, L'\0');
    if (!wDefault.empty())
    {
      size_t n = std::min(wDefault.size(), size_t(MAX_PATH - 1));
      memcpy(buf.data(), wDefault.c_str(), n * sizeof(wchar_t));
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

    if (!GetSaveFileNameW(&ofn))
      return {};
    return {{toUtf8(buf.data())}};
  }

  // ── Folder ────────────────────────────────────────────────────────────────

  PickResult pickFolder(void *ownerVoid, const std::string &title)
  {
    HWND owner = static_cast<HWND>(ownerVoid);
    std::wstring wTitle = toWide(title.empty() ? "Select Folder" : title);

    wchar_t buf[MAX_PATH] = {};

    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = wTitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
      return {};

    SHGetPathFromIDListW(pidl, buf);
    CoTaskMemFree(pidl);

    return {{toUtf8(buf)}};
  }

  // ── HWND accessor ─────────────────────────────────────────────────────────

  void *getOwnerHwnd()
  {
    auto *ui = FluxUI::getCurrentInstance();
    return ui ? static_cast<void *>(ui->getWindow()) : nullptr;
  }

} // namespace FluxFilePickerWin32

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
  {
    HWND hw = ui->getWindow();
    if (hw)
    {
      RECT r = {x, y, x + width, y + height};
      InvalidateRect(hw, &r, FALSE);
    }
  }
}

// ── Platform-specific: _openDialog ───────────────────────────────────────

void FilePickerWidget::_openDialog()
{
  using namespace FluxFilePickerWin32;

  const std::string title = title_.empty() ? _defaultTitle() : title_;
  void *owner = getOwnerHwnd();

  PickResult result;
  switch (mode_)
  {
  case FilePickerMode::Open:
    result = openFile(owner, title, initialDir_, defaultExt_, filters_);
    break;
  case FilePickerMode::OpenMultiple:
    result = openFiles(owner, title, initialDir_, defaultExt_, filters_);
    break;
  case FilePickerMode::Save:
    result = saveFile(owner, title, initialDir_, defaultFilename_,
                      defaultExt_, filters_);
    break;
  case FilePickerMode::Folder:
    result = pickFolder(owner, title);
    break;
  }

  if (result.cancelled())
  {
    if (onCancelled_)
      onCancelled_();
    return;
  }

  if (mode_ == FilePickerMode::OpenMultiple)
    _setMultiPaths(result.paths);
  else
    _setSinglePath(result.paths[0]);
}

#endif // _WIN32