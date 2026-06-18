#ifndef FLUX_FILE_PICKER_HPP
#define FLUX_FILE_PICKER_HPP

#include "../flux_core.hpp"
#include "../flux_state.hpp"
#include "flux_layout.hpp"

#include <functional>
#include <string>
#include <vector>

// ============================================================================
// FILE FILTER
// ============================================================================

struct FileFilter
{
  std::string label; 
  std::vector<std::string> extensions;
  bool showExtsInLabel = true;

  FileFilter(const std::string &lbl, std::vector<std::string> exts)
      : label(lbl), extensions(std::move(exts)) {}
};

// ============================================================================
// FILE PICKER MODE
// ============================================================================

enum class FilePickerMode
{
  Open,
  OpenMultiple,
  Save,
  Folder,
};

// ============================================================================
// LINUX ASYNC SUPPORT
// ============================================================================

#if defined(__linux__) && !defined(__ANDROID__)
#include <SDL2/SDL.h>

// Magic SDL_USEREVENT code that identifies file picker results.
static constexpr Sint32 kFluxFilePickerEvent = 0xF11E;

// Payload heap-allocated by the background dialog thread.
// fluxFilePickerDispatchSDLEvent deletes it after the callback fires.
struct FilePickerPayload
{
  std::function<void(std::vector<std::string>)> callback;
  std::vector<std::string> paths;
};

// Call this from your SDL event loop for every SDL_USEREVENT:
//
//   case SDL_USEREVENT:
//       fluxFilePickerDispatchSDLEvent(e);
//       dirty = true;
//       break;
//
void fluxFilePickerDispatchSDLEvent(const SDL_Event &ev);

void linuxPickFileAsync(const std::string &title, const std::string &initialDir,
                        const std::vector<FileFilter> &filters,
                        std::function<void(std::string)> callback);

void linuxPickFilesAsync(const std::string &title, const std::string &initialDir,
                         const std::vector<FileFilter> &filters,
                         std::function<void(std::vector<std::string>)> callback);

void linuxSaveFileAsync(const std::string &title, const std::string &initialDir,
                        const std::string &defaultFilename,
                        const std::vector<FileFilter> &filters,
                        std::function<void(std::string)> callback);

void linuxPickFolderAsync(const std::string &title, const std::string &initialDir,
                          std::function<void(std::string)> callback);

#endif // __linux__ && !__ANDROID__

// ============================================================================
// WINDOWS BACKEND — forward declarations
// ============================================================================

#ifdef _WIN32
namespace FluxFilePickerWin32
{

  struct PickResult
  {
    std::vector<std::string> paths;
    bool cancelled() const { return paths.empty(); }
  };

  PickResult openFile(void *owner, const std::string &title,
                      const std::string &initialDir,
                      const std::string &defaultExt,
                      const std::vector<FileFilter> &filters);

  PickResult openFiles(void *owner, const std::string &title,
                       const std::string &initialDir,
                       const std::string &defaultExt,
                       const std::vector<FileFilter> &filters);

  PickResult saveFile(void *owner, const std::string &title,
                      const std::string &initialDir,
                      const std::string &defaultFilename,
                      const std::string &defaultExt,
                      const std::vector<FileFilter> &filters);

  PickResult pickFolder(void *owner, const std::string &title);

  void *getOwnerHwnd();

} // namespace FluxFilePickerWin32
#endif // _WIN32

// ============================================================================
// ANDROID BACKEND — forward declarations
// ============================================================================

#ifdef __ANDROID__
#include <jni.h>

namespace FluxFilePickerAndroid
{

  // Must be set once at startup (e.g. in android_main or JNI_OnLoad).
  extern JNIEnv *g_env;
  extern ANativeActivity *g_activity;

  void init(JNIEnv *env, ANativeActivity *activity);

  void pickFileAsync(const std::string &title,
                     const std::vector<FileFilter> &filters,
                     std::function<void(std::string)> callback);

  void pickFilesAsync(const std::string &title,
                      const std::vector<FileFilter> &filters,
                      std::function<void(std::vector<std::string>)> callback);

  void saveFileAsync(const std::string &title,
                     const std::string &defaultFilename,
                     const std::vector<FileFilter> &filters,
                     std::function<void(std::string)> callback);

  void pickFolderAsync(const std::string &title,
                       std::function<void(std::string)> callback);

  // Called by the JNI bridge (Java_com_flux_FluxBridge_nativeOnFilePickerResult)
  // to deliver results back to the C++ callback registry.
  void dispatchResult(int requestCode, std::vector<std::string> paths);
  void drainPendingCallbacks();

} // namespace FluxFilePickerAndroid
#endif // __ANDROID__

// ============================================================================
// MACOS BACKEND — forward declarations
// ============================================================================

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

namespace FluxFilePickerMacOS
{

  // All functions dispatch to the main queue internally and are safe to call
  // from any thread.  Callbacks are invoked on the main thread.

  void pickFileAsync(const std::string &title,
                     const std::string &initialDir,
                     const std::vector<FileFilter> &filters,
                     std::function<void(std::string)> callback);

  void pickFilesAsync(const std::string &title,
                      const std::string &initialDir,
                      const std::vector<FileFilter> &filters,
                      std::function<void(std::vector<std::string>)> callback);

  void saveFileAsync(const std::string &title,
                     const std::string &initialDir,
                     const std::string &defaultFilename,
                     const std::vector<FileFilter> &filters,
                     std::function<void(std::string)> callback);

  void pickFolderAsync(const std::string &title,
                       const std::string &initialDir,
                       std::function<void(std::string)> callback);

} // namespace FluxFilePickerMacOS

#endif // TARGET_OS_OSX
#endif // __APPLE__


// ============================================================================
// WEB BACKEND (Emscripten) — forward declarations
// ============================================================================
//
// There's no native dialog in a browser tab and no real filesystem path to
// hand back. To keep the existing "you get a path, fopen() it" contract
// working, results are materialized into Emscripten's MEMFS virtual
// filesystem and the MEMFS path is what gets returned.
//
//   Open / Open Multiple — backed by a hidden <input type="file">. Selected
//     files are read via FileReader and written under /flux_web_uploads/.
//
//   Save — no dialog appears. A scratch path under /flux_web_saves/ is
//     handed back immediately; the caller writes to it synchronously, then
//     the bytes are downloaded via a synthetic <a download> click.
//
//   Folder — unsupported. Always reports cancellation.
//
#ifdef __EMSCRIPTEN__

namespace FluxFilePickerWeb
{

  void pickFileAsync(const std::string &title,
                     const std::vector<FileFilter> &filters,
                     std::function<void(std::string)> callback);

  void pickFilesAsync(const std::string &title,
                      const std::vector<FileFilter> &filters,
                      std::function<void(std::vector<std::string>)> callback);

  void saveFileAsync(const std::string &title,
                     const std::string &defaultFilename,
                     const std::vector<FileFilter> &filters,
                     std::function<void(std::string)> callback);

  // Always invokes callback("") — folder picking has no web equivalent here.
  void pickFolderAsync(const std::string &title,
                       std::function<void(std::string)> callback);

} // namespace FluxFilePickerWeb

#endif // __EMSCRIPTEN__

// ============================================================================
// FILE PICKER WIDGET
// ============================================================================
//
// A widget that opens the native OS file dialog when its trigger is clicked.
// Selected paths flow into bound State<> variables and fire callbacks.
//
// The trigger area is determined by the label mode:
//
//   Label mode  — string passed to FilePicker("...") or FilePicker()
//   ─────────────────────────────────────────────────────────────────
//   The label is drawn as plain unstyled text (no button border, no
//   background).  If you want custom typography, pass a TextWidget instead.
//
//   Widget mode — arbitrary WidgetPtr passed to FilePicker(widget)
//   ─────────────────────────────────────────────────────────────────
//   The supplied widget is laid out and painted as-is; clicks anywhere
//   inside its bounds open the dialog.  The widget owns its own styling.
//
// Cross-platform:
//   Windows  — GetOpenFileNameW / GetSaveFileNameW / SHBrowseForFolderW
//   Linux    — zenity (GNOME) or kdialog (KDE), async via background thread
//   Android  — ACTION_OPEN_DOCUMENT / CREATE_DOCUMENT / OPEN_DOCUMENT_TREE
//              via startActivityForResult, bridged through FluxBridge.java
//   macOS    — NSOpenPanel / NSSavePanel, async via dispatch_async(main_queue)
//
// ── String label (plain text trigger) ─────────────────────────────────────
//
//   FilePicker("Browse")
//       ->setMode(FilePickerMode::Open)
//       ->addFilter("Images", {"*.png","*.jpg"})
//       ->setOnChanged([](const std::string &p) { ... });
//
// ── Widget trigger ─────────────────────────────────────────────────────────
//
//   FilePicker(
//       Container(Text("Export SVG")->setTextColor(...))
//           ->setBackgroundColor(...)
//           ->setPadding(8)
//           ->setBorderRadius(4)
//   )
//   ->setMode(FilePickerMode::Save)
//   ->setDefaultFilename("circuit.svg")
//   ->setDefaultExtension("svg")
//   ->addFilter("SVG Vector", {"*.svg"})
//   ->setOnChanged([](const std::string &p) { ... });
//
// ── Save dialog ────────────────────────────────────────────────────────────
//
//   FilePicker("Save As")
//       ->setMode(FilePickerMode::Save)
//       ->setTitle("Export Image")
//       ->setDefaultFilename("output.png")
//       ->addFilter("PNG",  {"*.png"})
//       ->setDefaultExtension("png")
//       ->bindPath(exportPath)
//       ->setOnChanged([&](const std::string &p) { surface->exportImage(p); });
//
// ── Multiple files ─────────────────────────────────────────────────────────
//
//   State<std::vector<std::string>> paths({}, context);
//
//   FilePicker("Open Files")
//       ->setMode(FilePickerMode::OpenMultiple)
//       ->addFilter("Images", {"*.png","*.jpg"})
//       ->bindPaths(paths)
//       ->setOnMultiChanged([](const std::vector<std::string> &ps) { ... });
//
// ── Folder picker ──────────────────────────────────────────────────────────
//
//   FilePicker("Choose Folder")
//       ->setMode(FilePickerMode::Folder)
//       ->setTitle("Select output folder")
//       ->bindPath(folderPath);
//
// ============================================================================

class FilePickerWidget : public Widget
{
public:
  // ── Label-mode typography (used only when no custom widget is supplied) ──
  //   Override these to change the plain-text label appearance.
  Color labelTextColor = Color::fromRGB(30, 30, 30);
  Color labelHoverColor = Color::fromRGB(33, 150, 243); // tint on hover
  int labelFontSize = 14;                               // inherits Widget::fontSize default
  FontWeight labelFontWeight = FontWeight::Normal;

  // ── Constructor ──────────────────────────────────────────────────────────
  //   Do not call directly — use the FilePicker() factory functions below.
  explicit FilePickerWidget(const std::string &label = "")
      : customLabel_(label), labelWidget_(nullptr)
  {
    autoHeight = true;
    autoWidth = true;
    isFocusable = true;
  }

  explicit FilePickerWidget(std::shared_ptr<Widget> labelWidget)
      : customLabel_(""), labelWidget_(std::move(labelWidget))
  {
    autoHeight = true;
    autoWidth = true;
    isFocusable = true;
    if (labelWidget_)
      labelWidget_->parent = this;
  }

  // ── Public API ────────────────────────────────────────────────────────────

  void open();
  void clear();

  const std::string &path() const { return path_; }
  const std::vector<std::string> &paths() const { return paths_; }
  bool hasSelection() const { return !path_.empty(); }

  // ── Fluent configuration ──────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget> setMode(FilePickerMode m);
  std::shared_ptr<FilePickerWidget> setTitle(const std::string &t);
  std::shared_ptr<FilePickerWidget> setDefaultFilename(const std::string &f);
  std::shared_ptr<FilePickerWidget> setDefaultExtension(const std::string &e);
  std::shared_ptr<FilePickerWidget> setInitialDir(const std::string &d);
  std::shared_ptr<FilePickerWidget> addFilter(const std::string &label,
                                              std::vector<std::string> exts);
  std::shared_ptr<FilePickerWidget> addFilter(FileFilter f);
  std::shared_ptr<FilePickerWidget> setFilters(std::vector<FileFilter> fs);
  std::shared_ptr<FilePickerWidget> setHeight(int h);
  std::shared_ptr<FilePickerWidget> setWidth(int w);
  std::shared_ptr<FilePickerWidget> setFlex(int f);

  // ── Label-mode styling (no-op when a widget trigger is used) ─────────────
  std::shared_ptr<FilePickerWidget> setLabelTextColor(Color c);
  std::shared_ptr<FilePickerWidget> setLabelHoverColor(Color c);
  std::shared_ptr<FilePickerWidget> setLabelFontSize(int s);
  std::shared_ptr<FilePickerWidget> setLabelFontWeight(FontWeight w);

  // ── State bindings ────────────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget> bindPath(State<std::string> &state);
  std::shared_ptr<FilePickerWidget> bindPaths(State<std::vector<std::string>> &state);

  // ── Callbacks ─────────────────────────────────────────────────────────────

  std::shared_ptr<FilePickerWidget>
  setOnChanged(std::function<void(const std::string &)> fn);

  std::shared_ptr<FilePickerWidget>
  setOnMultiChanged(std::function<void(const std::vector<std::string> &)> fn);

  std::shared_ptr<FilePickerWidget>
  setOnCancelled(std::function<void()> fn);

  // ── Layout ────────────────────────────────────────────────────────────────

  void computeLayout(GraphicsContext &ctx, const BoxConstraints &constraints,
                     FontCache &fc) override;
  void positionChildren(int, int, int, int) override {}

  // ── Render ────────────────────────────────────────────────────────────────

  void render(GraphicsContext &ctx, FontCache &fontCache) override;

  // ── Input events ──────────────────────────────────────────────────────────

  bool handleMouseDown(int mx, int my) override;
  bool handleMouseMove(int mx, int my) override;
  bool handleMouseLeave() override;
  bool handleKeyDown(int key) override;

private:
  // ── Config ────────────────────────────────────────────────────────────────
  FilePickerMode mode_ = FilePickerMode::Open;
  std::string title_;
  std::string customLabel_;
  std::string defaultFilename_;
  std::string defaultExt_;
  std::string initialDir_;
  std::vector<FileFilter> filters_;

  // ── Trigger widget (widget mode) ──────────────────────────────────────────
  // When non-null the widget is laid out and painted instead of the plain text.
  std::shared_ptr<Widget> labelWidget_;

  // ── Selection ─────────────────────────────────────────────────────────────
  std::string path_;
  std::vector<std::string> paths_;

  // ── Bindings ──────────────────────────────────────────────────────────────
  State<std::string> *boundPath_ = nullptr;
  State<std::vector<std::string>> *boundPaths_ = nullptr;

  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void(const std::string &)> onChanged_;
  std::function<void(const std::vector<std::string> &)> onMultiChanged_;
  std::function<void()> onCancelled_;

  // ── Layout cache ──────────────────────────────────────────────────────────
  int lastMx_ = -9999;
  int lastMy_ = -9999;

  // ── Shared helpers ────────────────────────────────────────────────────────
  std::shared_ptr<FilePickerWidget> self_();
  bool _inBounds(int mx, int my) const;
  std::string _label() const; // resolved display string
  std::string _defaultTitle() const;

  // ── Label-mode helpers ────────────────────────────────────────────────────
  // Measure the plain-text label so computeLayout can set width/height.
  void _measureLabel(GraphicsContext &ctx, FontCache &fc);

  void _setSinglePath(const std::string &p);
  void _setMultiPaths(const std::vector<std::string> &ps);
  void _commitPaths();

  // ── Platform-specific ─────────────────────────────────────────────────────
  void _openDialog();
  void _repaint();
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================
//
// FilePicker()           — plain-text label using the default mode string
// FilePicker("Browse")   — plain-text label with custom string
// FilePicker(myWidget)   — arbitrary widget as the clickable trigger
//
// ============================================================================

using FilePickerWidgetPtr = std::shared_ptr<FilePickerWidget>;

// Plain-text label overloads
inline FilePickerWidgetPtr FilePicker(const std::string &label = "")
{
  return std::make_shared<FilePickerWidget>(label);
}

// Widget-trigger overload
inline FilePickerWidgetPtr FilePicker(std::shared_ptr<Widget> triggerWidget)
{
  return std::make_shared<FilePickerWidget>(std::move(triggerWidget));
}

#endif // FLUX_FILE_PICKER_HPP