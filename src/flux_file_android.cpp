// flux_file_android.cpp
//
// Android implementation of the FluxFile namespace.
//
// Android has two distinct storage worlds:
//
//   Internal storage  — app-private, always available, no permissions needed.
//                       Rooted at android_app->activity->internalDataPath.
//                       e.g. /data/data/<package>/files
//
//   External storage  — user-visible (SD card / emulated), may be unavailable.
//                       Rooted at android_app->activity->externalDataPath.
//                       e.g. /sdcard/Android/data/<package>/files
//                       Requires READ/WRITE_EXTERNAL_STORAGE on API < 29.
//
// File I/O itself is standard POSIX — fopen/fread/fwrite all work normally
// on absolute paths. The only Android-specific parts are §7 (platform paths)
// which read the path roots from the android_app struct, and the global
// app pointer that must be set once at startup.
//
// Sections:
//   §1  App pointer             — g_androidApp, FluxFile_setAndroidApp()
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

#ifdef __ANDROID__

#include "flux/flux_file.hpp"

#include <android_native_app_glue.h>
#include <android/log.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define FLUX_FILE_TAG "FluxFile"
#define FLUX_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, FLUX_FILE_TAG, __VA_ARGS__)

// ============================================================================
// §1  App pointer
// ============================================================================
//
// Set once at startup in android_main() before any FluxFile call is made:
//
//   FluxFile_setAndroidApp(state);
//
// All §7 path functions fall back gracefully to "" if not set.

static android_app* g_androidApp = nullptr;

void FluxFile_setAndroidApp(android_app* app) {
    g_androidApp = app;
}

// ============================================================================
// §2  File handle
// ============================================================================

namespace FluxFile {

FILE* open(const std::string& utf8path, const char* mode) {
    if (utf8path.empty() || !mode) return nullptr;
    FILE* f = fopen(utf8path.c_str(), mode);
    if (!f) {
        FLUX_LOGE("open failed: %s (mode=%s) — %s",
                  utf8path.c_str(), mode, strerror(errno));
    }
    return f;
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
    return access(utf8path.c_str(), F_OK) == 0;
}

bool isFile(const std::string& utf8path) {
    struct stat st{};
    if (stat(utf8path.c_str(), &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

bool isDirectory(const std::string& utf8path) {
    struct stat st{};
    if (stat(utf8path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

size_t fileSize(const std::string& utf8path) {
    struct stat st{};
    if (stat(utf8path.c_str(), &st) != 0) return 0;
    return static_cast<size_t>(st.st_size);
}

// ============================================================================
// §5  File operations
// ============================================================================

bool remove(const std::string& utf8path) {
    return ::remove(utf8path.c_str()) == 0;
}

bool rename(const std::string& oldPath, const std::string& newPath) {
    return ::rename(oldPath.c_str(), newPath.c_str()) == 0;
}

bool copy(const std::string& srcPath, const std::string& dstPath) {
    FILE* src = FluxFile::open(srcPath, "rb");
    if (!src) return false;

    FILE* dst = FluxFile::open(dstPath, "wb");
    if (!dst) { fclose(src); return false; }

    char buf[65536];
    bool ok = true;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) { ok = false; break; }
    }
    if (ferror(src)) ok = false;

    fclose(src);
    fclose(dst);

    if (!ok) ::remove(dstPath.c_str());
    return ok;
}

bool createDirectory(const std::string& utf8path) {
    if (mkdir(utf8path.c_str(), 0755) == 0) return true;
    return errno == EEXIST && isDirectory(utf8path);
}

bool createDirectories(const std::string& utf8path) {
    std::string path = utf8path;

    size_t pos = 0;
    while (pos < path.size()) {
        size_t next = path.find('/', pos + 1);
        if (next == std::string::npos) next = path.size();

        std::string prefix = path.substr(0, next);
        if (!prefix.empty() && prefix != ".") {
            struct stat st{};
            if (stat(prefix.c_str(), &st) != 0) {
                if (mkdir(prefix.c_str(), 0755) != 0 && errno != EEXIST)
                    return false;
            } else if (!S_ISDIR(st.st_mode)) {
                return false;
            }
        }
        pos = next;
    }
    return true;
}

// ============================================================================
// §6  Path utilities
// ============================================================================
// Pure string operations — identical to the Linux version.
// Only forward slash is a separator on Android.

std::string filename(const std::string& path) {
    size_t pos = path.find_last_of('/');
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
    size_t pos = path.find_last_of('/');
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    bool aSlash = (a.back() == '/');
    bool bSlash = (!b.empty() && b.front() == '/');
    if (aSlash  &&  bSlash) return a + b.substr(1);
    if (!aSlash && !bSlash) return a + '/' + b;
    return a + b;
}

std::string normalize(const std::string& path) {
    std::string result;
    result.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/' && i > 1 && path[i - 1] == '/')
            continue;
        result += path[i];
    }
    return result;
}

bool hasExtension(const std::string& path, const std::string& ext) {
    // Case-sensitive — matches Android/Linux filesystem behavior
    return extension(path) == ext;
}

// ============================================================================
// §7  Directory listing
// ============================================================================

std::vector<Entry> listDirectory(const std::string& utf8path,
                                  bool includeDirs,
                                  bool includeFiles) {
    std::vector<Entry> entries;

    DIR* dir = opendir(utf8path.c_str());
    if (!dir) {
        FLUX_LOGE("listDirectory: opendir failed: %s — %s",
                  utf8path.c_str(), strerror(errno));
        return entries;
    }

    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;

        Entry e;
        e.name = de->d_name;
        e.path = join(utf8path, e.name);

        struct stat st{};
        if (stat(e.path.c_str(), &st) != 0) continue;

        e.isDir = S_ISDIR(st.st_mode);

        if (e.isDir  && !includeDirs)  continue;
        if (!e.isDir && !includeFiles) continue;

        if (!e.isDir)
            e.size = static_cast<size_t>(st.st_size);

        entries.push_back(std::move(e));
    }

    closedir(dir);
    return entries;
}

// ============================================================================
// §8  Platform-specific paths
// ============================================================================
//
// On Android all writable paths come from the android_app activity struct.
// These are guaranteed writable without any runtime permissions.
//
// internalDataPath  — /data/data/<package>/files      (always available)
// externalDataPath  — /sdcard/Android/data/<package>  (may be null/unavailable)
//
// If g_androidApp is not set yet (called too early) we return "" safely.

std::string appDataDir() {
    if (!g_androidApp || !g_androidApp->activity) return {};
    const char* p = g_androidApp->activity->internalDataPath;
    return p ? p : "";
}

std::string documentsDir() {
    // External storage is the closest equivalent to a Documents folder.
    // Falls back to internal storage if external is unavailable.
    if (!g_androidApp || !g_androidApp->activity) return {};
    const char* ext = g_androidApp->activity->externalDataPath;
    if (ext && *ext) return ext;
    const char* internal = g_androidApp->activity->internalDataPath;
    return internal ? internal : "";
}

std::string tempDir() {
    // Android's internalDataPath + /cache is the standard temp location.
    // We create it if it doesn't exist — it's always writable.
    if (!g_androidApp || !g_androidApp->activity) return "/tmp";
    const char* base = g_androidApp->activity->internalDataPath;
    if (!base || !*base) return "/tmp";
    std::string cacheDir = std::string(base) + "/../cache";
    createDirectories(cacheDir);
    return cacheDir;
}

std::string executableDir() {
    // There is no meaningful "executable directory" on Android —
    // the APK is not a directory on the filesystem. Return the
    // internal data path as the closest useful equivalent.
    if (!g_androidApp || !g_androidApp->activity) return {};
    const char* p = g_androidApp->activity->internalDataPath;
    return p ? p : "";
}

} // namespace FluxFile

#endif // __ANDROID__