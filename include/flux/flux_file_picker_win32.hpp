// flux_file_picker_win32.hpp
//
// Windows backend for FilePickerWidget.
// Included by flux_file_picker.hpp when building on Win32.
//
// Uses the classic Win32 Common Dialog APIs:
//   Open / OpenMultiple  →  GetOpenFileNameW   (comdlg32)
//   Save                 →  GetSaveFileNameW   (comdlg32)
//   Folder               →  SHBrowseForFolderW (shell32)
//
// All string I/O uses the wide-character (W) API and is converted to UTF-8
// before being handed back to the widget's platform-neutral path_ field.
//
// No other files need to include Windows headers — every Win32 type lives
// entirely within this translation unit.
//
// ============================================================================

#pragma once

#ifdef _WIN32

#include <commdlg.h>
#include <shlobj.h>
#include <windows.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#include <string>
#include <vector>



namespace FluxFilePickerWin32 {

// ============================================================================
// String helpers
// ============================================================================

// Narrow UTF-8 → wide
inline std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    // Drop the null terminator MultiByteToWideChar appends
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

// Wide → narrow UTF-8
inline std::string toUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// ============================================================================
// Filter string builder
//
// OPENFILENAMEW expects a double-null-terminated wide string in the form:
//   "Description\0*.ext1;*.ext2\0Description2\0*.ext3\0\0"
// ============================================================================

inline std::vector<wchar_t> buildFilterW(const std::vector<FileFilter>& filters) {
    std::vector<wchar_t> result;

    auto append = [&](const std::wstring& s) {
        for (wchar_t c : s) result.push_back(c);
        result.push_back(L'\0');
    };

    for (const auto& f : filters) {
        // Join extensions:  "*.png;*.jpg"
        std::string exts;
        for (size_t i = 0; i < f.extensions.size(); ++i) {
            if (i) exts += ';';
            exts += f.extensions[i];
        }

        // Label:  "Images (*.png;*.jpg)"  or just "Images"
        std::string label = f.label;
        if (f.showExtsInLabel && !exts.empty())
            label += " (" + exts + ")";

        append(toWide(label));
        append(toWide(exts));
    }

    // Fallback when no filters provided
    if (filters.empty()) {
        append(L"All Files (*.*)");
        append(L"*.*");
    }

    result.push_back(L'\0'); // final double-null terminator
    return result;
}

// ============================================================================
// Result type returned by every open function
// ============================================================================

struct PickResult {
    std::vector<std::string> paths;  // empty = cancelled
    bool cancelled() const { return paths.empty(); }
};

// ============================================================================
// Open — single file
// ============================================================================

inline PickResult openFile(HWND                           owner,
                            const std::string&             title,
                            const std::string&             initialDir,
                            const std::string&             defaultExt,
                            const std::vector<FileFilter>& filters) {
    auto filter = buildFilterW(filters);

    std::wstring wTitle    = toWide(title.empty()      ? "Open File" : title);
    std::wstring wInitDir  = toWide(initialDir);
    std::wstring wDefExt   = toWide(defaultExt);

    std::vector<wchar_t> buf(MAX_PATH, L'\0');

    OPENFILENAMEW ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = owner;
    ofn.lpstrFilter     = filter.data();
    ofn.lpstrFile       = buf.data();
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrTitle      = wTitle.empty()   ? nullptr : wTitle.c_str();
    ofn.lpstrInitialDir = wInitDir.empty() ? nullptr : wInitDir.c_str();
    ofn.lpstrDefExt     = wDefExt.empty()  ? nullptr : wDefExt.c_str();
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return {};
    return {{ toUtf8(buf.data()) }};
}

// ============================================================================
// Open — multiple files
//
// OFN multi-select buffer layout (Explorer mode):
//   <dir>\0<file1>\0<file2>\0\0   (multiple)
//   <fullpath>\0\0                (single — user selected only one)
// ============================================================================

inline PickResult openFiles(HWND                           owner,
                             const std::string&             title,
                             const std::string&             initialDir,
                             const std::string&             defaultExt,
                             const std::vector<FileFilter>& filters) {
    auto filter = buildFilterW(filters);

    std::wstring wTitle   = toWide(title.empty() ? "Open Files" : title);
    std::wstring wInitDir = toWide(initialDir);
    std::wstring wDefExt  = toWide(defaultExt);

    // 32 KB buffer — enough for ~200 typical paths
    const int bufSize = 32768;
    std::vector<wchar_t> buf(bufSize, L'\0');

    OPENFILENAMEW ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = owner;
    ofn.lpstrFilter     = filter.data();
    ofn.lpstrFile       = buf.data();
    ofn.nMaxFile        = bufSize;
    ofn.lpstrTitle      = wTitle.empty()   ? nullptr : wTitle.c_str();
    ofn.lpstrInitialDir = wInitDir.empty() ? nullptr : wInitDir.c_str();
    ofn.lpstrDefExt     = wDefExt.empty()  ? nullptr : wDefExt.c_str();
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST
                        | OFN_EXPLORER     | OFN_ALLOWMULTISELECT;

    if (!GetOpenFileNameW(&ofn)) return {};

    // Parse the buffer
    std::wstring dir(buf.data());
    const wchar_t* ptr = buf.data() + dir.size() + 1;

    // Single file — buf already contains the full path
    if (*ptr == L'\0') return {{ toUtf8(buf.data()) }};

    // Multiple files — dir\0file1\0file2\0\0
    PickResult result;
    while (*ptr != L'\0') {
        std::wstring fname(ptr);
        result.paths.push_back(toUtf8((dir + L'\\' + fname).c_str()));
        ptr += fname.size() + 1;
    }
    return result;
}

// ============================================================================
// Save
// ============================================================================

inline PickResult saveFile(HWND                           owner,
                            const std::string&             title,
                            const std::string&             initialDir,
                            const std::string&             defaultFilename,
                            const std::string&             defaultExt,
                            const std::vector<FileFilter>& filters) {
    auto filter = buildFilterW(filters);

    std::wstring wTitle   = toWide(title.empty()   ? "Save File" : title);
    std::wstring wInitDir = toWide(initialDir);
    std::wstring wDefExt  = toWide(defaultExt);
    std::wstring wDefault = toWide(defaultFilename);

    std::vector<wchar_t> buf(MAX_PATH, L'\0');
    if (!wDefault.empty()) {
        size_t n = std::min(wDefault.size(), size_t(MAX_PATH - 1));
        memcpy(buf.data(), wDefault.c_str(), n * sizeof(wchar_t));
    }

    OPENFILENAMEW ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = owner;
    ofn.lpstrFilter     = filter.data();
    ofn.lpstrFile       = buf.data();
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrTitle      = wTitle.empty()   ? nullptr : wTitle.c_str();
    ofn.lpstrInitialDir = wInitDir.empty() ? nullptr : wInitDir.c_str();
    ofn.lpstrDefExt     = wDefExt.empty()  ? nullptr : wDefExt.c_str();
    ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) return {};
    return {{ toUtf8(buf.data()) }};
}

// ============================================================================
// Folder
// ============================================================================

inline PickResult pickFolder(HWND               owner,
                              const std::string& title) {
    std::wstring wTitle = toWide(title.empty() ? "Select Folder" : title);

    wchar_t buf[MAX_PATH] = {};

    BROWSEINFOW bi  = {};
    bi.hwndOwner    = owner;
    bi.lpszTitle    = wTitle.c_str();
    bi.ulFlags      = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};

    SHGetPathFromIDListW(pidl, buf);
    CoTaskMemFree(pidl);

    return {{ toUtf8(buf) }};
}

// ============================================================================
// HWND accessor — reaches back into FluxUI without coupling to Win32 in the
// main header
// ============================================================================

inline HWND getOwnerHwnd() {
    auto* ui = FluxUI::getCurrentInstance();
    return ui ? ui->getWindow() : nullptr;
}

} // namespace FluxFilePickerWin32

#endif // _WIN32