// flux_file_picker_linux.hpp
//
// Linux backend for FilePickerWidget.
// Included by flux_file_picker.hpp when building on Linux.
//
// Strategy (tried in order):
//   1. zenity   — GTK-based, ships with GNOME (most common)
//   2. kdialog  — Qt-based,  ships with KDE
//   3. xdg-open fallback (folder only) — minimal but always present
//
// No additional dependencies beyond what SDL2+Cairo apps already have.
// popen()/pclose() are used to run the dialog and capture its output.
//
// Folder picker notes:
//   zenity --file-selection --directory
//   kdialog --getexistingdirectory
//
// ============================================================================

#pragma once

#if defined(__linux__) && !defined(__ANDROID__)

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <unistd.h>   // access()

// ============================================================================
// Internal helpers  (anonymous namespace — translation-unit-local)
// ============================================================================
namespace {

// ----------------------------------------------------------------------------
// Shell-escape a single token so it can be inserted into a popen() command.
// We wrap it in single quotes and escape any embedded single quotes.
// ----------------------------------------------------------------------------
inline std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";   // end quote, escaped quote, reopen
        else           out += c;
    }
    out += "'";
    return out;
}

// ----------------------------------------------------------------------------
// Run a shell command via popen and capture its stdout, stripping trailing
// newlines.  Returns empty string on failure or if the process exits non-zero.
// ----------------------------------------------------------------------------
inline std::string runCapture(const std::string& cmd) {
    FILE* fp = ::popen(cmd.c_str(), "r");
    if (!fp) return {};

    std::string result;
    char buf[4096];
    while (::fgets(buf, sizeof(buf), fp))
        result += buf;

    int rc = ::pclose(fp);
    if (rc != 0) return {};   // user cancelled or tool not found

    // Strip trailing newlines / carriage returns
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    return result;
}

// ----------------------------------------------------------------------------
// Check whether a program is available on PATH.
// ----------------------------------------------------------------------------
inline bool programExists(const char* name) {
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return ::system(cmd.c_str()) == 0;
}

// ----------------------------------------------------------------------------
// Detect which dialog backend is available (cached after first call).
// ----------------------------------------------------------------------------
enum class DialogBackend { None, Zenity, Kdialog };

inline DialogBackend detectBackend() {
    static DialogBackend cached = static_cast<DialogBackend>(-1);
    if (static_cast<int>(cached) != -1) return cached;

    if (programExists("zenity"))   { cached = DialogBackend::Zenity;   return cached; }
    if (programExists("kdialog"))  { cached = DialogBackend::Kdialog;  return cached; }

    cached = DialogBackend::None;
    return cached;
}

// ============================================================================
// Build filter arguments
// ============================================================================

// zenity uses --file-filter="Label | *.ext1 *.ext2"  (space-separated exts)
inline std::string buildZenityFilters(const std::vector<FileFilter>& filters) {
    if (filters.empty()) return {};
    std::string out;
    for (const auto& f : filters) {
        std::string exts;
        for (size_t i = 0; i < f.extensions.size(); ++i) {
            if (i) exts += ' ';
            exts += f.extensions[i];
        }
        // --file-filter='Label | *.ext1 *.ext2'
        std::string label = f.label;
        if (f.showExtsInLabel && !exts.empty())
            label += " (" + exts + ")";
        out += " --file-filter=" + shellQuote(label + " | " + exts);
    }
    return out;
}

// kdialog uses --mimetype or manually appended extensions; simplest approach:
// pass a single "name pattern" string like "*.png *.jpg"
inline std::string buildKdialogFilter(const std::vector<FileFilter>& filters) {
    if (filters.empty()) return {};
    // kdialog --getopenfilename <startDir> <filter>
    // filter = "Description (*.ext1 *.ext2);;Description2 (*.ext3)"
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
// Zenity implementations
// ============================================================================

inline std::string zenityOpenSingle(const std::string& title,
                                    const std::string& initialDir,
                                    const std::vector<FileFilter>& filters) {
    std::string cmd = "zenity --file-selection";
    if (!title.empty())      cmd += " --title=" + shellQuote(title);
    if (!initialDir.empty()) cmd += " --filename=" + shellQuote(initialDir + "/");
    cmd += buildZenityFilters(filters);
    cmd += " 2>/dev/null";
    return runCapture(cmd);
}

inline std::vector<std::string>
zenityOpenMultiple(const std::string& title,
                   const std::string& initialDir,
                   const std::vector<FileFilter>& filters) {
    std::string cmd = "zenity --file-selection --multiple --separator=|";
    if (!title.empty())      cmd += " --title=" + shellQuote(title);
    if (!initialDir.empty()) cmd += " --filename=" + shellQuote(initialDir + "/");
    cmd += buildZenityFilters(filters);
    cmd += " 2>/dev/null";

    std::string raw = runCapture(cmd);
    if (raw.empty()) return {};

    std::vector<std::string> paths;
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, '|'))
        if (!token.empty()) paths.push_back(token);
    return paths;
}

inline std::string zenitySave(const std::string& title,
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
    return runCapture(cmd);
}

inline std::string zenityFolder(const std::string& title,
                                 const std::string& initialDir) {
    std::string cmd = "zenity --file-selection --directory";
    if (!title.empty())      cmd += " --title=" + shellQuote(title);
    if (!initialDir.empty()) cmd += " --filename=" + shellQuote(initialDir + "/");
    cmd += " 2>/dev/null";
    return runCapture(cmd);
}

// ============================================================================
// kdialog implementations
// ============================================================================

inline std::string kdialogOpenSingle(const std::string& title,
                                     const std::string& initialDir,
                                     const std::vector<FileFilter>& filters) {
    std::string startDir = initialDir.empty() ? "." : initialDir;
    std::string filter   = buildKdialogFilter(filters);
    std::string cmd = "kdialog --getopenfilename " + shellQuote(startDir);
    if (!filter.empty()) cmd += " " + shellQuote(filter);
    if (!title.empty())  cmd += " --title " + shellQuote(title);
    cmd += " 2>/dev/null";
    return runCapture(cmd);
}

inline std::vector<std::string>
kdialogOpenMultiple(const std::string& title,
                    const std::string& initialDir,
                    const std::vector<FileFilter>& filters) {
    std::string startDir = initialDir.empty() ? "." : initialDir;
    std::string filter   = buildKdialogFilter(filters);
    std::string cmd = "kdialog --getopenfilename " + shellQuote(startDir);
    if (!filter.empty()) cmd += " " + shellQuote(filter);
    if (!title.empty())  cmd += " --title " + shellQuote(title);
    // kdialog doesn't have a clean multi-select; open single and return as vector
    cmd += " 2>/dev/null";
    std::string p = runCapture(cmd);
    if (p.empty()) return {};
    return {p};
}

inline std::string kdialogSave(const std::string& title,
                                const std::string& initialDir,
                                const std::string& defaultFilename,
                                const std::vector<FileFilter>& filters) {
    std::string startPath = initialDir.empty() ? "." : initialDir;
    if (!startPath.empty() && startPath.back() != '/') startPath += '/';
    startPath += defaultFilename;
    std::string filter = buildKdialogFilter(filters);
    std::string cmd = "kdialog --getsavefilename " + shellQuote(startPath);
    if (!filter.empty()) cmd += " " + shellQuote(filter);
    if (!title.empty())  cmd += " --title " + shellQuote(title);
    cmd += " 2>/dev/null";
    return runCapture(cmd);
}

inline std::string kdialogFolder(const std::string& title,
                                  const std::string& initialDir) {
    std::string startDir = initialDir.empty() ? "." : initialDir;
    std::string cmd = "kdialog --getexistingdirectory " + shellQuote(startDir);
    if (!title.empty()) cmd += " --title " + shellQuote(title);
    cmd += " 2>/dev/null";
    return runCapture(cmd);
}

} // anonymous namespace

// ============================================================================
// Public entry points called by FilePickerWidget
// ============================================================================

// These free functions have C linkage names so they can be called from the
// widget's private _openDialog methods via #ifdef guards.

inline std::string linuxPickFile(const std::string& title,
                                  const std::string& initialDir,
                                  const std::vector<FileFilter>& filters) {
    switch (detectBackend()) {
    case DialogBackend::Zenity:  return zenityOpenSingle(title, initialDir, filters);
    case DialogBackend::Kdialog: return kdialogOpenSingle(title, initialDir, filters);
    default: return {};
    }
}

inline std::vector<std::string>
linuxPickFiles(const std::string& title,
               const std::string& initialDir,
               const std::vector<FileFilter>& filters) {
    switch (detectBackend()) {
    case DialogBackend::Zenity:  return zenityOpenMultiple(title, initialDir, filters);
    case DialogBackend::Kdialog: return kdialogOpenMultiple(title, initialDir, filters);
    default: return {};
    }
}

inline std::string linuxSaveFile(const std::string& title,
                                  const std::string& initialDir,
                                  const std::string& defaultFilename,
                                  const std::vector<FileFilter>& filters) {
    switch (detectBackend()) {
    case DialogBackend::Zenity:  return zenitySave(title, initialDir, defaultFilename, filters);
    case DialogBackend::Kdialog: return kdialogSave(title, initialDir, defaultFilename, filters);
    default: return {};
    }
}

inline std::string linuxPickFolder(const std::string& title,
                                    const std::string& initialDir) {
    switch (detectBackend()) {
    case DialogBackend::Zenity:  return zenityFolder(title, initialDir);
    case DialogBackend::Kdialog: return kdialogFolder(title, initialDir);
    default: return {};
    }
}

#endif // __linux__ && !__ANDROID__