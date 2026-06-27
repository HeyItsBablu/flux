// flux_file_picker_linux.cpp
//
// Linux implementation of FilePickerWidget and the async dialog backend.

#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/widgets/flux_file_picker.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// ============================================================================
// §1  Async dialog backend
// ============================================================================

namespace
{

  // ── Shell helpers ─────────────────────────────────────────────────────────

  std::string shellQuote(const std::string &s)
  {
    std::string out = "'";
    for (char c : s)
    {
      if (c == '\'')
        out += "'\\''";
      else
        out += c;
    }
    out += "'";
    return out;
  }

  // Run a shell command via popen — BLOCKING, call only from a worker thread.
  // Returns trimmed stdout, or empty string on failure / cancellation.
  std::string runCaptureBlocking(const std::string &cmd)
  {
    FILE *fp = ::popen(cmd.c_str(), "r");
    if (!fp)
      return {};

    std::string result;
    char buf[4096];
    while (::fgets(buf, sizeof(buf), fp))
      result += buf;

    int rc = ::pclose(fp);
    if (rc != 0)
      return {};

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
      result.pop_back();

    return result;
  }

  // Spawn a background thread, run cmd, post an SDL_USEREVENT with the result.
  // multiLine=true splits output on '|' (zenity multi) or '\n' (kdialog).
  void runAsync(const std::string &cmd,
                std::function<void(std::vector<std::string>)> callback,
                bool multiLine = false)
  {
    std::thread([cmd, callback, multiLine]()
                {
    std::string raw = runCaptureBlocking(cmd);

    std::vector<std::string> paths;
    if (!raw.empty()) {
      if (multiLine) {
        char sep = (raw.find('|') != std::string::npos) ? '|' : '\n';
        std::stringstream ss(raw);
        std::string token;
        while (std::getline(ss, token, sep))
          if (!token.empty()) paths.push_back(token);
      } else {
        paths.push_back(raw);
      }
    }

    // Heap-allocated; fluxFilePickerDispatchSDLEvent deletes it.
    auto *payload = new FilePickerPayload{callback, std::move(paths)};

    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));
    ev.type      = SDL_USEREVENT;
    ev.user.code = kFluxFilePickerEvent;
    ev.user.data1 = payload;
    ev.user.data2 = nullptr;
    SDL_PushEvent(&ev); })
        .detach();
  }

  // ── Backend detection ─────────────────────────────────────────────────────

  bool programExists(const char *name)
  {
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return ::system(cmd.c_str()) == 0;
  }

  enum class DialogBackend
  {
    None,
    Zenity,
    Kdialog
  };

  DialogBackend detectBackend()
  {
    static DialogBackend cached = static_cast<DialogBackend>(-1);
    if (static_cast<int>(cached) != -1)
      return cached;

    // VS Code snap overwrites GTK/GDK env vars; restore originals so zenity works.
    auto restoreOrUnset = [](const char *var)
    {
      std::string origKey = std::string(var) + "_VSCODE_SNAP_ORIG";
      const char *orig = ::getenv(origKey.c_str());
      if (orig && orig[0] != '\0')
        ::setenv(var, orig, 1);
      else
        ::unsetenv(var);
    };

    restoreOrUnset("GDK_PIXBUF_MODULEDIR");
    restoreOrUnset("GDK_PIXBUF_MODULE_FILE");
    restoreOrUnset("GTK_PATH");
    restoreOrUnset("GTK_EXE_PREFIX");
    restoreOrUnset("GTK_IM_MODULE_FILE");
    restoreOrUnset("GSETTINGS_SCHEMA_DIR");
    restoreOrUnset("GIO_MODULE_DIR");
    restoreOrUnset("LOCPATH");

    if (const char *v = ::getenv("XDG_DATA_DIRS_VSCODE_SNAP_ORIG"))
      if (*v)
        ::setenv("XDG_DATA_DIRS", v, 1);

    if (const char *v = ::getenv("XDG_DATA_HOME_VSCODE_SNAP_ORIG"))
      if (*v == '\0')
        ::unsetenv("XDG_DATA_HOME");

    ::unsetenv("LD_PRELOAD");
    ::unsetenv("LD_LIBRARY_PATH");

    if (programExists("zenity"))
    {
      cached = DialogBackend::Zenity;
      return cached;
    }
    if (programExists("kdialog"))
    {
      cached = DialogBackend::Kdialog;
      return cached;
    }

    cached = DialogBackend::None;
    return cached;
  }

  // ── Filter string builders ────────────────────────────────────────────────

  std::string buildZenityFilters(const std::vector<FileFilter> &filters)
  {
    std::string out;
    for (const auto &f : filters)
    {
      std::string exts;
      for (size_t i = 0; i < f.extensions.size(); ++i)
      {
        if (i)
          exts += ' ';
        exts += f.extensions[i];
      }
      std::string label = f.label;
      if (f.showExtsInLabel && !exts.empty())
        label += " (" + exts + ")";
      out += " --file-filter=" + shellQuote(label + " | " + exts);
    }
    return out;
  }

  std::string buildKdialogFilter(const std::vector<FileFilter> &filters)
  {
    std::string out;
    for (size_t fi = 0; fi < filters.size(); ++fi)
    {
      const auto &f = filters[fi];
      std::string exts;
      for (size_t i = 0; i < f.extensions.size(); ++i)
      {
        if (i)
          exts += ' ';
        exts += f.extensions[i];
      }
      if (fi)
        out += ";;";
      out += f.label + " (" + exts + ")";
    }
    return out;
  }

  // ── Command builders ──────────────────────────────────────────────────────

  std::string cmdZenityOpenSingle(const std::string &title,
                                  const std::string &initialDir,
                                  const std::vector<FileFilter> &filters)
  {
    std::string cmd = "zenity --file-selection";
    if (!title.empty())
      cmd += " --title=" + shellQuote(title);
    if (!initialDir.empty())
      cmd += " --filename=" + shellQuote(initialDir + "/");
    cmd += buildZenityFilters(filters);
    cmd += " 2>/dev/null";
    return cmd;
  }

  std::string cmdZenityOpenMultiple(const std::string &title,
                                    const std::string &initialDir,
                                    const std::vector<FileFilter> &filters)
  {
    std::string cmd = "zenity --file-selection --multiple --separator='|'";
    if (!title.empty())
      cmd += " --title=" + shellQuote(title);
    if (!initialDir.empty())
      cmd += " --filename=" + shellQuote(initialDir + "/");
    cmd += buildZenityFilters(filters);
    cmd += " 2>/dev/null";
    return cmd;
  }

  std::string cmdZenitySave(const std::string &title,
                            const std::string &initialDir,
                            const std::string &defaultFilename,
                            const std::vector<FileFilter> &filters)
  {
    std::string cmd = "zenity --file-selection --save --confirm-overwrite";
    if (!title.empty())
      cmd += " --title=" + shellQuote(title);
    std::string startPath = initialDir;
    if (!startPath.empty() && startPath.back() != '/')
      startPath += '/';
    startPath += defaultFilename;
    if (!startPath.empty())
      cmd += " --filename=" + shellQuote(startPath);
    cmd += buildZenityFilters(filters);
    cmd += " 2>/dev/null";
    return cmd;
  }

  std::string cmdZenityFolder(const std::string &title,
                              const std::string &initialDir)
  {
    std::string cmd = "zenity --file-selection --directory";
    if (!title.empty())
      cmd += " --title=" + shellQuote(title);
    if (!initialDir.empty())
      cmd += " --filename=" + shellQuote(initialDir + "/");
    cmd += " 2>/dev/null";
    return cmd;
  }

  std::string cmdKdialogOpenSingle(const std::string &title,
                                   const std::string &initialDir,
                                   const std::vector<FileFilter> &filters)
  {
    std::string startDir = initialDir.empty() ? "." : initialDir;
    std::string cmd = "kdialog --getopenfilename " + shellQuote(startDir);
    std::string f = buildKdialogFilter(filters);
    if (!f.empty())
      cmd += " " + shellQuote(f);
    if (!title.empty())
      cmd += " --title " + shellQuote(title);
    cmd += " 2>/dev/null";
    return cmd;
  }

  std::string cmdKdialogSave(const std::string &title,
                             const std::string &initialDir,
                             const std::string &defaultFilename,
                             const std::vector<FileFilter> &filters)
  {
    std::string startPath = initialDir.empty() ? "." : initialDir;
    if (!startPath.empty() && startPath.back() != '/')
      startPath += '/';
    startPath += defaultFilename;
    std::string cmd = "kdialog --getsavefilename " + shellQuote(startPath);
    std::string f = buildKdialogFilter(filters);
    if (!f.empty())
      cmd += " " + shellQuote(f);
    if (!title.empty())
      cmd += " --title " + shellQuote(title);
    cmd += " 2>/dev/null";
    return cmd;
  }

  std::string cmdKdialogFolder(const std::string &title,
                               const std::string &initialDir)
  {
    std::string startDir = initialDir.empty() ? "." : initialDir;
    std::string cmd = "kdialog --getexistingdirectory " + shellQuote(startDir);
    if (!title.empty())
      cmd += " --title " + shellQuote(title);
    cmd += " 2>/dev/null";
    return cmd;
  }

} // anonymous namespace

// ── Public async API (declared in flux_file_picker.hpp) ──────────────────

void fluxFilePickerDispatchSDLEvent(const SDL_Event &ev)
{
  if (ev.type != SDL_USEREVENT || ev.user.code != kFluxFilePickerEvent)
    return;
  auto *payload = static_cast<FilePickerPayload *>(ev.user.data1);
  if (payload)
  {
    payload->callback(std::move(payload->paths));
    delete payload;
  }
}

void linuxPickFileAsync(const std::string &title, const std::string &initialDir,
                        const std::vector<FileFilter> &filters,
                        std::function<void(std::string)> callback)
{
  std::string cmd;
  switch (detectBackend())
  {
  case DialogBackend::Zenity:
    cmd = cmdZenityOpenSingle(title, initialDir, filters);
    break;
  case DialogBackend::Kdialog:
    cmd = cmdKdialogOpenSingle(title, initialDir, filters);
    break;
  default:
    callback({});
    return;
  }
  runAsync(cmd, [callback](std::vector<std::string> ps)
           { callback(ps.empty() ? "" : ps[0]); });
}

void linuxPickFilesAsync(const std::string &title, const std::string &initialDir,
                         const std::vector<FileFilter> &filters,
                         std::function<void(std::vector<std::string>)> callback)
{
  std::string cmd;
  bool multi = false;
  switch (detectBackend())
  {
  case DialogBackend::Zenity:
    cmd = cmdZenityOpenMultiple(title, initialDir, filters);
    multi = true;
    break;
  case DialogBackend::Kdialog:
    // kdialog has no true multi-select; falls back to single
    cmd = cmdKdialogOpenSingle(title, initialDir, filters);
    break;
  default:
    callback({});
    return;
  }
  runAsync(cmd, callback, multi);
}

void linuxSaveFileAsync(const std::string &title, const std::string &initialDir,
                        const std::string &defaultFilename,
                        const std::vector<FileFilter> &filters,
                        std::function<void(std::string)> callback)
{
  std::string cmd;
  switch (detectBackend())
  {
  case DialogBackend::Zenity:
    cmd = cmdZenitySave(title, initialDir, defaultFilename, filters);
    break;
  case DialogBackend::Kdialog:
    cmd = cmdKdialogSave(title, initialDir, defaultFilename, filters);
    break;
  default:
    callback({});
    return;
  }
  runAsync(cmd, [callback](std::vector<std::string> ps)
           { callback(ps.empty() ? "" : ps[0]); });
}

void linuxPickFolderAsync(const std::string &title, const std::string &initialDir,
                          std::function<void(std::string)> callback)
{
  std::string cmd;
  switch (detectBackend())
  {
  case DialogBackend::Zenity:
    cmd = cmdZenityFolder(title, initialDir);
    break;
  case DialogBackend::Kdialog:
    cmd = cmdKdialogFolder(title, initialDir);
    break;
  default:
    callback({});
    return;
  }
  runAsync(cmd, [callback](std::vector<std::string> ps)
           { callback(ps.empty() ? "" : ps[0]); });
}

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
  // Measure the plain-text label to size the widget in label mode.
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
    // ── Widget mode: delegate entirely to the trigger widget ─────────────
    labelWidget_->computeLayout(ctx, constraints, fc);
    if (autoWidth)
      width = labelWidget_->width;
    if (autoHeight)
      height = labelWidget_->height;
  }
  else
  {
    // ── Label mode: size to the plain-text string ─────────────────────────
    _measureLabel(ctx, fc);
  }

  width = constraints.clampWidth(width);
  height = constraints.clampHeight(height);
  applyConstraints();
  needsLayout = false;

  // Position the trigger widget to overlap our own bounds exactly.
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
    // ── Widget mode: let the trigger widget paint itself ──────────────────
    //   Forward hover state so the widget can react if it has hover colors.
    labelWidget_->isHovered = isHovered;
    labelWidget_->render(ctx, fontCache);
  }
  else
  {
    // ── Label mode: plain text, no border/background ──────────────────────
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
// SDL redraws on the next frame after markNeedsPaint(); no explicit
// invalidation call is needed on Linux.

void FilePickerWidget::_repaint()
{
  needsPaint = true;
}

// ── Platform-specific: _openDialog ───────────────────────────────────────

void FilePickerWidget::_openDialog()
{
  const std::string title = title_.empty() ? _defaultTitle() : title_;
  auto self = self_();

  switch (mode_)
  {
  case FilePickerMode::Open:
    linuxPickFileAsync(title, initialDir_, filters_, [self](std::string p)
                       {
      if (p.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
      self->_setSinglePath(p); });
    break;

  case FilePickerMode::OpenMultiple:
    linuxPickFilesAsync(title, initialDir_, filters_,
                        [self](std::vector<std::string> ps)
                        {
                          if (ps.empty())
                          {
                            if (self->onCancelled_)
                              self->onCancelled_();
                            return;
                          }
                          self->_setMultiPaths(ps);
                        });
    break;

  case FilePickerMode::Save:
    linuxSaveFileAsync(title, initialDir_, defaultFilename_, filters_,
                       [self](std::string p)
                       {
                         if (p.empty())
                         {
                           if (self->onCancelled_)
                             self->onCancelled_();
                           return;
                         }
                         if (!self->defaultExt_.empty() && p.find('.') == std::string::npos)
                           p += '.' + self->defaultExt_;
                         self->_setSinglePath(p);
                       });
    break;

  case FilePickerMode::Folder:
    linuxPickFolderAsync(title, initialDir_, [self](std::string p)
                         {
      if (p.empty()) { if (self->onCancelled_) self->onCancelled_(); return; }
      self->_setSinglePath(p); });
    break;
  }
}

#endif // __linux__ && !__ANDROID__