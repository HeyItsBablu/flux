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

struct FileFilter {
  std::string label;
  std::vector<std::string> extensions;
  bool showExtsInLabel = true;

  FileFilter(const std::string &lbl, std::vector<std::string> exts)
      : label(lbl), extensions(std::move(exts)) {}
};

// ============================================================================
// FILE PICKER MODE
// ============================================================================

enum class FilePickerMode {
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
struct FilePickerPayload {
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
namespace FluxFilePickerWin32 {

struct PickResult {
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

namespace FluxFilePickerAndroid {

// Must be set once at startup (e.g. in android_main or JNI_OnLoad).
extern JNIEnv        *g_env;
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
// FILE PICKER WIDGET
// ============================================================================
//
// A button-like widget that opens the native OS file dialog when clicked.
// Selected paths flow into bound State<> variables and fire callbacks.
//
// Cross-platform:
//   Windows  — GetOpenFileNameW / GetSaveFileNameW / SHBrowseForFolderW
//   Linux    — zenity (GNOME) or kdialog (KDE), async via background thread
//   Android  — ACTION_OPEN_DOCUMENT / CREATE_DOCUMENT / OPEN_DOCUMENT_TREE
//              via startActivityForResult, bridged through FluxBridge.java
//
// ── Single file open ──────────────────────────────────────────────────────
//
//   State<std::string> filePath("", context);
//
//   FilePicker()
//       ->setMode(FilePickerMode::Open)
//       ->addFilter("Images", {"*.png","*.jpg","*.jpeg","*.bmp"})
//       ->addFilter("All files", {"*.*"})
//       ->setDefaultExtension("png")
//       ->bindPath(filePath)
//       ->setOnChanged([](const std::string &path) {
//           std::cout << "Picked: " << path << "\n";
//       });
//
// ── Save dialog ───────────────────────────────────────────────────────────
//
//   FilePicker()
//       ->setMode(FilePickerMode::Save)
//       ->setTitle("Export Image")
//       ->setDefaultFilename("output.png")
//       ->addFilter("PNG",  {"*.png"})
//       ->addFilter("JPEG", {"*.jpg","*.jpeg"})
//       ->setDefaultExtension("png")
//       ->bindPath(exportPath)
//       ->setOnChanged([&](const std::string &p) { surface->exportImage(p); });
//
// ── Multiple files ────────────────────────────────────────────────────────
//
//   State<std::vector<std::string>> paths({}, context);
//
//   FilePicker()
//       ->setMode(FilePickerMode::OpenMultiple)
//       ->addFilter("Images", {"*.png","*.jpg"})
//       ->bindPaths(paths)
//       ->setOnMultiChanged([](const std::vector<std::string> &ps) { ... });
//
// ── Folder picker ─────────────────────────────────────────────────────────
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
  Color btnBgColor      = Color::fromRGB(245, 247, 250);
  Color btnHoverColor   = Color::fromRGB(224, 235, 248);
  Color btnBorderColor  = Color::fromRGB(200, 204, 210);
  Color btnTextColor    = Color::fromRGB( 30,  30,  30);
  Color pathTextColor   = Color::fromRGB( 80,  80,  90);
  Color placeholderColor= Color::fromRGB(160, 160, 170);
  Color accentColor     = Color::fromRGB( 33, 150, 243);

  int  btnHeight    = 32;
  int  btnPadding   = 10;
  int  borderRadius = 5;
  int  pathMaxWidth = 300;
  bool showPath     = true;
  bool showClearBtn = true;

  // ── Constructor ───────────────────────────────────────────────────────────
  explicit FilePickerWidget(const std::string &label = "")
      : customLabel_(label) {
    autoHeight  = false;
    autoWidth   = true;
    height      = btnHeight;
    isFocusable = true;
  }

  // ── Public API ────────────────────────────────────────────────────────────

  void open();
  void clear();

  const std::string              &path()         const { return path_;  }
  const std::vector<std::string> &paths()        const { return paths_; }
  bool                            hasSelection() const { return !path_.empty(); }

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
  std::shared_ptr<FilePickerWidget> setShowPath(bool v);
  std::shared_ptr<FilePickerWidget> setShowClearBtn(bool v);
  std::shared_ptr<FilePickerWidget> setPathMaxWidth(int w);
  std::shared_ptr<FilePickerWidget> setAccentColor(Color c);
  std::shared_ptr<FilePickerWidget> setHeight(int h);
  std::shared_ptr<FilePickerWidget> setWidth(int w);
  std::shared_ptr<FilePickerWidget> setFlex(int f);

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
  FilePickerMode           mode_            = FilePickerMode::Open;
  std::string              title_;
  std::string              customLabel_;
  std::string              defaultFilename_;
  std::string              defaultExt_;
  std::string              initialDir_;
  std::vector<FileFilter>  filters_;

  // ── Selection ─────────────────────────────────────────────────────────────
  std::string              path_;
  std::vector<std::string> paths_;

  // ── Bindings ──────────────────────────────────────────────────────────────
  State<std::string>              *boundPath_  = nullptr;
  State<std::vector<std::string>> *boundPaths_ = nullptr;

  // ── Callbacks ─────────────────────────────────────────────────────────────
  std::function<void(const std::string &)>              onChanged_;
  std::function<void(const std::vector<std::string> &)> onMultiChanged_;
  std::function<void()>                                  onCancelled_;

  // ── Layout cache ──────────────────────────────────────────────────────────
  int btnW_   = 120;
  int clearW_ = 0;
  int lastMx_ = -9999;
  int lastMy_ = -9999;

  // ── Shared helpers ────────────────────────────────────────────────────────
  // Defined in all platform .cpp files (identical implementations).
  std::shared_ptr<FilePickerWidget> self_();
  bool        _inBounds(int mx, int my) const;
  bool        _isOverClear(int mx, int my) const;
  std::string _label() const;
  std::string _placeholder() const;
  std::string _displayPath() const;
  int         _measureBtnWidth() const;
  std::string _defaultTitle() const;
  void        _setSinglePath(const std::string &p);
  void        _setMultiPaths(const std::vector<std::string> &ps);
  void        _commitPaths();

  // ── Platform-specific ─────────────────────────────────────────────────────
  // Each platform .cpp provides its own definition of these two functions.
  void _openDialog();  // open the native file dialog
  void _repaint();     // trigger a platform repaint after selection
};

// ============================================================================
// FACTORY
// ============================================================================

using FilePickerWidgetPtr = std::shared_ptr<FilePickerWidget>;

inline FilePickerWidgetPtr FilePicker(const std::string &label = "") {
  return std::make_shared<FilePickerWidget>(label);
}

#endif // FLUX_FILE_PICKER_HPP