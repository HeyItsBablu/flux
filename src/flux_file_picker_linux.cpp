// flux_file_picker_linux.cpp
//
// Linux implementation of the file picker async functions declared in
// flux_file_picker.hpp.
//
// Strategy (tried in order at runtime):
//   1. zenity   — GTK-based, ships with GNOME
//   2. kdialog  — Qt-based,  ships with KDE
//
// Each dialog runs on a background thread so the SDL2 event loop keeps
// pumping while the dialog is open. The result is marshalled back to the
// main thread via SDL_PushEvent (SDL_USEREVENT / code kFluxFilePickerEvent).
//
// flux_window_linux.cpp must forward SDL_USEREVENTs to
//   fluxFilePickerDispatchSDLEvent(e)
// so results are delivered on the main thread.
//
// ============================================================================

#if defined(__linux__) && !defined(__ANDROID__)

// flux_file_picker.hpp must come first — it defines FileFilter, FilePickerMode,
// FilePickerPayload, kFluxFilePickerEvent, and the function declarations.
#include "flux/widgets/flux_file_picker.hpp"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// ============================================================================
// Internal helpers
// ============================================================================
namespace {

// ----------------------------------------------------------------------------
// Shell-escape a token for safe insertion into a popen() command string.
// ----------------------------------------------------------------------------
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += "'";
    return out;
}

// ----------------------------------------------------------------------------
// Run a shell command via popen (BLOCKING — call only from a worker thread).
// Returns trimmed stdout, or empty string on failure / user cancellation.
// ----------------------------------------------------------------------------
std::string runCaptureBlocking(const std::string& cmd) {
    FILE* fp = ::popen(cmd.c_str(), "r");
    if (!fp) return {};

    std::string result;
    char buf[4096];
    while (::fgets(buf, sizeof(buf), fp))
        result += buf;

    int rc = ::pclose(fp);
    if (rc != 0) return {};   // non-zero = cancelled or error

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    return result;
}

// ----------------------------------------------------------------------------
// Spawn a background thread that runs cmd, then posts an SDL_USEREVENT
// containing the result back to the main thread.
// multiLine=true splits output on '|' (zenity multi) or '\n' (kdialog).
// ----------------------------------------------------------------------------
void runAsync(const std::string& cmd,
              std::function<void(std::vector<std::string>)> callback,
              bool multiLine = false) {
    std::thread([cmd, callback, multiLine]() {
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

        // Heap-allocate payload; fluxFilePickerDispatchSDLEvent deletes it.
        auto* payload = new FilePickerPayload{callback, std::move(paths)};

        SDL_Event ev;
        SDL_memset(&ev, 0, sizeof(ev));
        ev.type       = SDL_USEREVENT;
        ev.user.code  = kFluxFilePickerEvent;
        ev.user.data1 = payload;
        ev.user.data2 = nullptr;
        SDL_PushEvent(&ev);
    }).detach();
}

// ----------------------------------------------------------------------------
// Check whether a program exists on PATH.
// ----------------------------------------------------------------------------
bool programExists(const char* name) {
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return ::system(cmd.c_str()) == 0;
}

// ----------------------------------------------------------------------------
// Detect and cache which dialog backend is available.
// Also scrubs VS Code snap environment pollution on first call so that
// zenity/kdialog link against the system glibc, not snap's.
// ----------------------------------------------------------------------------
enum class DialogBackend { None, Zenity, Kdialog };

DialogBackend detectBackend() {
    static DialogBackend cached = static_cast<DialogBackend>(-1);
    if (static_cast<int>(cached) != -1) return cached;

    // VS Code snap overwrites GTK/GDK env vars and saves originals in
    // *_VSCODE_SNAP_ORIG variables. Restore them so zenity works correctly.
    auto restoreOrUnset = [](const char* var) {
        std::string origKey = std::string(var) + "_VSCODE_SNAP_ORIG";
        const char* orig = ::getenv(origKey.c_str());
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

    const char* xdgOrig = ::getenv("XDG_DATA_DIRS_VSCODE_SNAP_ORIG");
    if (xdgOrig && xdgOrig[0] != '\0')
        ::setenv("XDG_DATA_DIRS", xdgOrig, 1);

    const char* dataHomeOrig = ::getenv("XDG_DATA_HOME_VSCODE_SNAP_ORIG");
    if (dataHomeOrig && dataHomeOrig[0] == '\0')
        ::unsetenv("XDG_DATA_HOME");

    ::unsetenv("LD_PRELOAD");
    ::unsetenv("LD_LIBRARY_PATH");

    if (programExists("zenity"))  { cached = DialogBackend::Zenity;  return cached; }
    if (programExists("kdialog")) { cached = DialogBackend::Kdialog; return cached; }

    cached = DialogBackend::None;
    return cached;
}

// ============================================================================
// Filter string builders
// ============================================================================

std::string buildZenityFilters(const std::vector<FileFilter>& filters) {
    std::string out;
    for (const auto& f : filters) {
        std::string exts;
        for (size_t i = 0; i < f.extensions.size(); ++i) {
            if (i) exts += ' ';
            exts += f.extensions[i];
        }
        std::string label = f.label;
        if (f.showExtsInLabel && !exts.empty())
            label += " (" + exts + ")";
        out += " --file-filter=" + shellQuote(label + " | " + exts);
    }
    return out;
}

std::string buildKdialogFilter(const std::vector<FileFilter>& filters) {
    std::string out;
    for (size_t fi = 0; fi < filters.size(); ++fi) {
        const auto& f = filters[fi];
        std::string exts;
        for (size_t i = 0; i < f.extensions.size(); ++i) {
            if (i) exts += ' ';
            exts += f.extensions[i];
        }
        if (fi) out += ";;";
        out += f.label + " (" + exts + ")";
    }
    return out;
}

// ============================================================================
// Command builders — return the complete shell command string for popen()
// ============================================================================

std::string cmdZenityOpenSingle(const std::string& title,
                                 const std::string& initialDir,
                                 const std::vector<FileFilter>& filters) {
    std::string cmd = "zenity --file-selection";
    if (!title.empty())      cmd += " --title=" + shellQuote(title);
    if (!initialDir.empty()) cmd += " --filename=" + shellQuote(initialDir + "/");
    cmd += buildZenityFilters(filters);
    cmd += " 2>/dev/null";
    return cmd;
}

// flux_file_picker_linux.cpp
std::string cmdZenityOpenMultiple(const std::string& title,
                                   const std::string& initialDir,
                                   const std::vector<FileFilter>& filters) {
    std::string cmd = "zenity --file-selection --multiple --separator='|'";  // ← quote the pipe
    if (!title.empty())      cmd += " --title=" + shellQuote(title);
    if (!initialDir.empty()) cmd += " --filename=" + shellQuote(initialDir + "/");
    cmd += buildZenityFilters(filters);
    cmd += " 2>/dev/null";
    return cmd;
}

std::string cmdZenitySave(const std::string& title,
                           const std::string& initialDir,
                           const std::string& defaultFilename,
                           const std::vector<FileFilter>& filters) {
    std::string cmd = "zenity --file-selection --save --confirm-overwrite";
    if (!title.empty()) cmd += " --title=" + shellQuote(title);
    std::string startPath = initialDir;
    if (!startPath.empty() && startPath.back() != '/') startPath += '/';
    startPath += defaultFilename;
    if (!startPath.empty()) cmd += " --filename=" + shellQuote(startPath);
    cmd += buildZenityFilters(filters);
    cmd += " 2>/dev/null";
    return cmd;
}

std::string cmdZenityFolder(const std::string& title,
                             const std::string& initialDir) {
    std::string cmd = "zenity --file-selection --directory";
    if (!title.empty())      cmd += " --title=" + shellQuote(title);
    if (!initialDir.empty()) cmd += " --filename=" + shellQuote(initialDir + "/");
    cmd += " 2>/dev/null";
    return cmd;
}

std::string cmdKdialogOpenSingle(const std::string& title,
                                  const std::string& initialDir,
                                  const std::vector<FileFilter>& filters) {
    std::string startDir = initialDir.empty() ? "." : initialDir;
    std::string cmd = "kdialog --getopenfilename " + shellQuote(startDir);
    std::string f = buildKdialogFilter(filters);
    if (!f.empty())     cmd += " " + shellQuote(f);
    if (!title.empty()) cmd += " --title " + shellQuote(title);
    cmd += " 2>/dev/null";
    return cmd;
}

std::string cmdKdialogSave(const std::string& title,
                            const std::string& initialDir,
                            const std::string& defaultFilename,
                            const std::vector<FileFilter>& filters) {
    std::string startPath = initialDir.empty() ? "." : initialDir;
    if (!startPath.empty() && startPath.back() != '/') startPath += '/';
    startPath += defaultFilename;
    std::string cmd = "kdialog --getsavefilename " + shellQuote(startPath);
    std::string f = buildKdialogFilter(filters);
    if (!f.empty())     cmd += " " + shellQuote(f);
    if (!title.empty()) cmd += " --title " + shellQuote(title);
    cmd += " 2>/dev/null";
    return cmd;
}

std::string cmdKdialogFolder(const std::string& title,
                              const std::string& initialDir) {
    std::string startDir = initialDir.empty() ? "." : initialDir;
    std::string cmd = "kdialog --getexistingdirectory " + shellQuote(startDir);
    if (!title.empty()) cmd += " --title " + shellQuote(title);
    cmd += " 2>/dev/null";
    return cmd;
}

} // anonymous namespace

// ============================================================================
// Public function definitions (declared in flux_file_picker.hpp)
// ============================================================================

void fluxFilePickerDispatchSDLEvent(const SDL_Event& ev) {
    if (ev.type != SDL_USEREVENT || ev.user.code != kFluxFilePickerEvent) return;
    auto* payload = static_cast<FilePickerPayload*>(ev.user.data1);
    if (payload) {
        payload->callback(std::move(payload->paths));
        delete payload;
    }
}

void linuxPickFileAsync(const std::string& title,
                         const std::string& initialDir,
                         const std::vector<FileFilter>& filters,
                         std::function<void(std::string)> callback) {
    std::string cmd;
    switch (detectBackend()) {
    case DialogBackend::Zenity:  cmd = cmdZenityOpenSingle(title, initialDir, filters); break;
    case DialogBackend::Kdialog: cmd = cmdKdialogOpenSingle(title, initialDir, filters); break;
    default: callback({}); return;
    }
    runAsync(cmd, [callback](std::vector<std::string> ps) {
        callback(ps.empty() ? "" : ps[0]);
    });
}

void linuxPickFilesAsync(const std::string& title,
                          const std::string& initialDir,
                          const std::vector<FileFilter>& filters,
                          std::function<void(std::vector<std::string>)> callback) {
    std::string cmd;
    bool multi = false;
    switch (detectBackend()) {
    case DialogBackend::Zenity:
        cmd   = cmdZenityOpenMultiple(title, initialDir, filters);
        multi = true;
        break;
    case DialogBackend::Kdialog:
        // kdialog has no true multi-select; falls back to single
        cmd = cmdKdialogOpenSingle(title, initialDir, filters);
        break;
    default: callback({}); return;
    }
    runAsync(cmd, callback, multi);
}

void linuxSaveFileAsync(const std::string& title,
                         const std::string& initialDir,
                         const std::string& defaultFilename,
                         const std::vector<FileFilter>& filters,
                         std::function<void(std::string)> callback) {
    std::string cmd;
    switch (detectBackend()) {
    case DialogBackend::Zenity:  cmd = cmdZenitySave(title, initialDir, defaultFilename, filters); break;
    case DialogBackend::Kdialog: cmd = cmdKdialogSave(title, initialDir, defaultFilename, filters); break;
    default: callback({}); return;
    }
    runAsync(cmd, [callback](std::vector<std::string> ps) {
        callback(ps.empty() ? "" : ps[0]);
    });
}

void linuxPickFolderAsync(const std::string& title,
                           const std::string& initialDir,
                           std::function<void(std::string)> callback) {
    std::string cmd;
    switch (detectBackend()) {
    case DialogBackend::Zenity:  cmd = cmdZenityFolder(title, initialDir); break;
    case DialogBackend::Kdialog: cmd = cmdKdialogFolder(title, initialDir); break;
    default: callback({}); return;
    }
    runAsync(cmd, [callback](std::vector<std::string> ps) {
        callback(ps.empty() ? "" : ps[0]);
    });
}

#endif // __linux__ && !__ANDROID__