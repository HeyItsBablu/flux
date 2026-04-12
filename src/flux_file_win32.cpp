// flux_file_win32.cpp
//
// Windows implementation of the FluxFile namespace.
//
// All public API functions accept and return UTF-8 std::string.
// The single internal helper toWide() converts to UTF-16 at the boundary —
// this is the only place in the whole framework where that conversion happens
// for file I/O.
//
// Sections:
//   §1  Internal helpers        — toWide(), toUtf8(), stat wrapper
//   §2  File handle             — open()
//   §3  Whole-file I/O          — readBytes(), readText(), writeBytes(), writeText()
//   §4  File queries            — exists(), isFile(), isDirectory(), fileSize()
//   §5  File operations         — remove(), rename(), copy(), createDirectory/ies()
//   §6  Path utilities          — filename(), stem(), extension(), directory(),
//                                 join(), normalize(), hasExtension()
//   §7  Directory listing       — listDirectory()
//   §8  Platform-specific paths — appDataDir(), documentsDir(), tempDir(),
//                                 executableDir()
// ============================================================================

#ifdef _WIN32

#include "flux/flux_file.hpp"

#include <windows.h>
#include <shlobj.h>          // SHGetFolderPathW
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// §1  Internal helpers
// ============================================================================

namespace {

// UTF-8 → UTF-16 — used at every Win32 API call site in this file only.
static std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

// UTF-16 → UTF-8 — used when reading paths back from Win32 APIs.
static std::string toUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// Thin stat wrapper — returns false if the path doesn't exist or stat fails.
static bool wstat(const std::wstring& wp, WIN32_FILE_ATTRIBUTE_DATA& out) {
    return GetFileAttributesExW(wp.c_str(), GetFileExInfoStandard, &out) != FALSE;
}

} // anonymous namespace

// ============================================================================
// §2  File handle
// ============================================================================

namespace FluxFile {

FILE* open(const std::string& utf8path, const char* mode) {
    if (utf8path.empty() || !mode) return nullptr;

    // Convert mode string "rb" → L"rb" (mode strings are ASCII, safe cast)
    wchar_t wmode[8] = {};
    for (int i = 0; mode[i] && i < 7; ++i)
        wmode[i] = static_cast<wchar_t>(mode[i]);

    std::wstring wp = toWide(utf8path);
    if (wp.empty()) return nullptr;

    return _wfopen(wp.c_str(), wmode);
}

// ============================================================================
// §3  Whole-file I/O
// ============================================================================

std::vector<uint8_t> readBytes(const std::string& utf8path) {
    FILE* f = FluxFile::open(utf8path, "rb");
    if (!f) return {};

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) { fclose(f); return {}; }

    std::vector<uint8_t> buf(static_cast<size_t>(len));
    size_t got = fread(buf.data(), 1, buf.size(), f);
    fclose(f);

    buf.resize(got);
    return buf;
}

std::string readText(const std::string& utf8path) {
    FILE* f = FluxFile::open(utf8path, "rb");
    if (!f) return {};

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) { fclose(f); return {}; }

    std::string s(static_cast<size_t>(len), '\0');
    size_t got = fread(s.data(), 1, s.size(), f);
    fclose(f);

    s.resize(got);
    return s;
}

bool writeBytes(const std::string& utf8path, const void* data, size_t size) {
    if (!data && size > 0) return false;
    FILE* f = FluxFile::open(utf8path, "wb");
    if (!f) return false;
    bool ok = (size == 0) || (fwrite(data, 1, size, f) == size);
    fclose(f);
    return ok;
}

bool writeBytes(const std::string& utf8path, const std::vector<uint8_t>& data) {
    return writeBytes(utf8path, data.data(), data.size());
}

bool writeText(const std::string& utf8path, const std::string& text) {
    return writeBytes(utf8path, text.data(), text.size());
}

// ============================================================================
// §4  File queries
// ============================================================================

bool exists(const std::string& utf8path) {
    std::wstring wp = toWide(utf8path);
    if (wp.empty()) return false;
    return GetFileAttributesW(wp.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool isFile(const std::string& utf8path) {
    std::wstring wp = toWide(utf8path);
    if (wp.empty()) return false;
    DWORD attr = GetFileAttributesW(wp.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool isDirectory(const std::string& utf8path) {
    std::wstring wp = toWide(utf8path);
    if (wp.empty()) return false;
    DWORD attr = GetFileAttributesW(wp.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           (attr & FILE_ATTRIBUTE_DIRECTORY);
}

size_t fileSize(const std::string& utf8path) {
    std::wstring wp = toWide(utf8path);
    if (wp.empty()) return 0;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!wstat(wp, info)) return 0;
    ULARGE_INTEGER ul;
    ul.HighPart = info.nFileSizeHigh;
    ul.LowPart  = info.nFileSizeLow;
    return static_cast<size_t>(ul.QuadPart);
}

// ============================================================================
// §5  File operations
// ============================================================================

bool remove(const std::string& utf8path) {
    std::wstring wp = toWide(utf8path);
    if (wp.empty()) return false;
    DWORD attr = GetFileAttributesW(wp.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return RemoveDirectoryW(wp.c_str()) != FALSE;
    return DeleteFileW(wp.c_str()) != FALSE;
}

bool rename(const std::string& oldPath, const std::string& newPath) {
    std::wstring wo = toWide(oldPath);
    std::wstring wn = toWide(newPath);
    if (wo.empty() || wn.empty()) return false;
    // MOVEFILE_REPLACE_EXISTING mirrors POSIX rename() semantics
    return MoveFileExW(wo.c_str(), wn.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

bool copy(const std::string& srcPath, const std::string& dstPath) {
    std::wstring ws = toWide(srcPath);
    std::wstring wd = toWide(dstPath);
    if (ws.empty() || wd.empty()) return false;
    // FALSE = do not fail if destination exists (overwrite)
    return CopyFileW(ws.c_str(), wd.c_str(), FALSE) != FALSE;
}

bool createDirectory(const std::string& utf8path) {
    std::wstring wp = toWide(utf8path);
    if (wp.empty()) return false;
    if (CreateDirectoryW(wp.c_str(), nullptr) != FALSE) return true;
    // Already exists is not an error
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool createDirectories(const std::string& utf8path) {
    std::string path = utf8path;

    // Normalise separators so the splitting logic below is simple
    for (char& c : path)
        if (c == '\\') c = '/';

    // Walk each prefix and create it if missing
    size_t pos = 0;
    while (pos < path.size()) {
        size_t next = path.find('/', pos + 1);
        if (next == std::string::npos) next = path.size();

        std::string prefix = path.substr(0, next);

        // Skip drive root (e.g. "C:/") and UNC prefixes
        if (!prefix.empty() && prefix.back() != ':') {
            std::wstring wp = toWide(prefix);
            if (!wp.empty()) {
                DWORD attr = GetFileAttributesW(wp.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES) {
                    if (!CreateDirectoryW(wp.c_str(), nullptr))
                        return false;
                } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    return false; // a file exists with that name
                }
            }
        }
        pos = next;
    }
    return true;
}

// ============================================================================
// §6  Path utilities
// ============================================================================
// Pure string operations — identical on all platforms, but defined here so
// each platform .cpp is self-contained and there is no shared .cpp file.

std::string filename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

std::string stem(const std::string& path) {
    std::string fn = filename(path);
    size_t dot = fn.rfind('.');
    return (dot == std::string::npos || dot == 0) ? fn : fn.substr(0, dot);
}

std::string extension(const std::string& path) {
    std::string fn = filename(path);
    size_t dot = fn.rfind('.');
    return (dot == std::string::npos || dot == 0) ? "" : fn.substr(dot);
}

std::string directory(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    bool aSlash = (a.back() == '/' || a.back() == '\\');
    bool bSlash = (!b.empty() && (b.front() == '/' || b.front() == '\\'));
    if (aSlash && bSlash)  return a + b.substr(1);
    if (!aSlash && !bSlash) return a + '/' + b;
    return a + b;
}

std::string normalize(const std::string& path) {
    std::string out = path;
    for (char& c : out)
        if (c == '\\') c = '/';
    // Collapse double slashes (preserve leading // for UNC)
    std::string result;
    result.reserve(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '/' && i > 0 && out[i - 1] == '/')
            continue;
        result += out[i];
    }
    return result;
}

bool hasExtension(const std::string& path, const std::string& ext) {
    std::string e = extension(path);
    if (e.empty() || ext.empty()) return false;
    // Case-insensitive on Windows
    if (e.size() != ext.size()) return false;
    for (size_t i = 0; i < e.size(); ++i) {
        if (tolower(static_cast<unsigned char>(e[i])) !=
            tolower(static_cast<unsigned char>(ext[i])))
            return false;
    }
    return true;
}

// ============================================================================
// §7  Directory listing
// ============================================================================

std::vector<Entry> listDirectory(const std::string& utf8path,
                                  bool includeDirs,
                                  bool includeFiles) {
    std::vector<Entry> entries;

    std::string pattern = utf8path;
    if (!pattern.empty() && pattern.back() != '/' && pattern.back() != '\\')
        pattern += '/';
    pattern += '*';

    std::wstring wp = toWide(pattern);
    if (wp.empty()) return entries;

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(wp.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return entries;

    do {
        std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..") continue;

        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (isDir  && !includeDirs)  continue;
        if (!isDir && !includeFiles) continue;

        Entry e;
        e.name  = toUtf8(fd.cFileName);
        e.path  = join(utf8path, e.name);
        e.isDir = isDir;

        if (!isDir) {
            ULARGE_INTEGER ul;
            ul.HighPart = fd.nFileSizeHigh;
            ul.LowPart  = fd.nFileSizeLow;
            e.size = static_cast<size_t>(ul.QuadPart);
        }

        entries.push_back(std::move(e));

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return entries;
}

// ============================================================================
// §8  Platform-specific paths
// ============================================================================

std::string appDataDir() {
    wchar_t buf[MAX_PATH] = {};
    // CSIDL_APPDATA → C:\Users\<user>\AppData\Roaming
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf)))
        return toUtf8(buf);
    return {};
}

std::string documentsDir() {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, buf)))
        return toUtf8(buf);
    return {};
}

std::string tempDir() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0 || n > MAX_PATH) return {};
    // GetTempPath appends a trailing backslash — strip it for consistency
    std::string s = toUtf8(buf);
    if (!s.empty() && (s.back() == '\\' || s.back() == '/'))
        s.pop_back();
    return s;
}

std::string executableDir() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return directory(toUtf8(buf));
}

} // namespace FluxFile

#endif // _WIN32