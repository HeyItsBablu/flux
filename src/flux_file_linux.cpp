// flux_file_linux.cpp
//
// Linux implementation of the FluxFile namespace.
//
// UTF-8 is the native encoding on Linux — no string conversion needed.
// All functions are thin wrappers around POSIX / glibc APIs.
//
// Sections:
//   §1  File handle             — open()
//   §2  Whole-file I/O          — readBytes(), readText(), writeBytes(), writeText()
//   §3  File queries            — exists(), isFile(), isDirectory(), fileSize()
//   §4  File operations         — remove(), rename(), copy(), createDirectory/ies()
//   §5  Path utilities          — filename(), stem(), extension(), directory(),
//                                 join(), normalize(), hasExtension()
//   §6  Directory listing       — listDirectory()
//   §7  Platform-specific paths — appDataDir(), documentsDir(), tempDir(),
//                                 executableDir()
// ============================================================================

#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_file.hpp"

#include <dirent.h>      // opendir, readdir, closedir
#include <sys/stat.h>    // stat, mkdir
#include <sys/types.h>
#include <unistd.h>      // readlink, access
#include <fcntl.h>       // open flags (used in copy)
#include <pwd.h>         // getpwuid — fallback for home dir

#include <cerrno>
#include <cstdio>
#include <cstdlib>       // getenv
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// §1  File handle
// ============================================================================

namespace FluxFile {

FILE* open(const std::string& utf8path, const char* mode) {
    if (utf8path.empty() || !mode) return nullptr;
    return fopen(utf8path.c_str(), mode);
}

// ============================================================================
// §2  Whole-file I/O
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
// §3  File queries
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
// §4  File operations
// ============================================================================

bool remove(const std::string& utf8path) {
    // ::remove handles both files and empty directories
    return ::remove(utf8path.c_str()) == 0;
}

bool rename(const std::string& oldPath, const std::string& newPath) {
    // ::rename is atomic on Linux when src and dst are on the same filesystem
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

    // Clean up partial output on failure
    if (!ok) ::remove(dstPath.c_str());
    return ok;
}

bool createDirectory(const std::string& utf8path) {
    // 0755 — rwxr-xr-x, standard directory permissions
    if (mkdir(utf8path.c_str(), 0755) == 0) return true;
    return errno == EEXIST && isDirectory(utf8path);
}

bool createDirectories(const std::string& utf8path) {
    std::string path = utf8path;

    // Walk each prefix and create it if missing
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
                return false; // a file exists with that name
            }
        }
        pos = next;
    }
    return true;
}

// ============================================================================
// §5  Path utilities
// ============================================================================
// Pure string operations — identical logic to the Win32 version, but only
// forward slash is a separator on Linux.

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
    if (aSlash && bSlash)   return a + b.substr(1);
    if (!aSlash && !bSlash) return a + '/' + b;
    return a + b;
}

std::string normalize(const std::string& path) {
    // Collapse consecutive slashes, preserve leading // (POSIX special case)
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
    // Case-sensitive on Linux
    return extension(path) == ext;
}

// ============================================================================
// §6  Directory listing
// ============================================================================

std::vector<Entry> listDirectory(const std::string& utf8path,
                                  bool includeDirs,
                                  bool includeFiles) {
    std::vector<Entry> entries;

    DIR* dir = opendir(utf8path.c_str());
    if (!dir) return entries;

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

        // Use stat to get definitive type and size
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
// §7  Platform-specific paths
// ============================================================================

// Helper — returns the current user's home directory.
// Prefers $HOME, falls back to getpwuid for robustness.
static std::string homeDir() {
    const char* home = getenv("HOME");
    if (home && *home) return home;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "";
}

std::string appDataDir() {
    // XDG_CONFIG_HOME is the standard; fallback to ~/.config
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return xdg;
    std::string home = homeDir();
    return home.empty() ? "" : home + "/.config";
}

std::string documentsDir() {
    // XDG_DOCUMENTS_DIR is set by xdg-user-dirs; fallback to ~/Documents
    const char* xdg = getenv("XDG_DOCUMENTS_DIR");
    if (xdg && *xdg) return xdg;
    std::string home = homeDir();
    return home.empty() ? "" : home + "/Documents";
}

std::string tempDir() {
    // TMPDIR is the POSIX standard override; fallback to /tmp
    const char* tmp = getenv("TMPDIR");
    if (tmp && *tmp) return tmp;
    return "/tmp";
}

std::string executableDir() {
    // /proc/self/exe is a symlink to the running binary on Linux
    char buf[4096] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return directory(std::string(buf));
}

} // namespace FluxFile

#endif // __linux__ && !__ANDROID__