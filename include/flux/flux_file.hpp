// flux/flux_file.hpp
#pragma once

#include "flux_platform.hpp"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// FluxFile
//
// Cross-platform file & path utilities. Public API is always UTF-8 strings.
// Platform encoding conversions are buried in the platform .cpp files.
//
// Win32   — uses _wfopen / wide Win32 APIs for full Unicode path support
// Linux   — UTF-8 is native; thin wrappers around fopen / POSIX
// Android — same as Linux; paths rooted at app internal/external storage
//
// FILE* open() integrates directly with stb_image / stb_image_write via
// stbi_load_from_file / stbi_write_png_to_func so ImageEditSurface never
// needs to touch platform encoding at all.
// ============================================================================

namespace FluxFile {

// ============================================================================
// §1  FILE HANDLE
// ============================================================================
//
// Always use FluxFile::open() instead of fopen() / _wfopen() directly.
// The returned FILE* is a standard C file handle — pass to stbi_load_from_file,
// fread, fwrite, etc. Caller is responsible for fclose().
//
//   FILE* f = FluxFile::open(path, "rb");
//   if (!f) { /* handle error */ }
//   auto* img = stbi_load_from_file(f, &w, &h, &ch, 4);
//   fclose(f);

FILE* open(const std::string& utf8path, const char* mode);

// ============================================================================
// §2  WHOLE-FILE I/O
// ============================================================================

// Read entire file into a byte buffer. Returns empty vector on failure.
std::vector<uint8_t> readBytes(const std::string& utf8path);

// Read entire file as a UTF-8 string. Returns empty string on failure.
std::string readText(const std::string& utf8path);

// Write bytes to file. Returns true on success.
bool writeBytes(const std::string& utf8path, const void* data, size_t size);
bool writeBytes(const std::string& utf8path, const std::vector<uint8_t>& data);

// Write text to file. Returns true on success.
bool writeText(const std::string& utf8path, const std::string& text);

// ============================================================================
// §3  FILE QUERIES
// ============================================================================

bool   exists(const std::string& utf8path);
bool   isFile(const std::string& utf8path);
bool   isDirectory(const std::string& utf8path);
size_t fileSize(const std::string& utf8path);   // 0 on failure

// ============================================================================
// §4  FILE OPERATIONS
// ============================================================================

bool remove(const std::string& utf8path);
bool rename(const std::string& oldPath, const std::string& newPath);
bool copy(const std::string& srcPath,  const std::string& dstPath);
bool createDirectory(const std::string& utf8path);  // creates one level
bool createDirectories(const std::string& utf8path); // creates full chain

// ============================================================================
// §5  PATH UTILITIES
// ============================================================================
// Pure string operations — no filesystem access, no platform code needed.

std::string filename(const std::string& path);        // "photo.jpg"
std::string stem(const std::string& path);            // "photo"
std::string extension(const std::string& path);       // ".jpg"
std::string directory(const std::string& path);       // "/home/user/pics"
std::string join(const std::string& a, const std::string& b);
std::string normalize(const std::string& path);       // cleans separators
bool        hasExtension(const std::string& path, const std::string& ext);

// ============================================================================
// §6  DIRECTORY LISTING
// ============================================================================

struct Entry {
    std::string name;         // filename only, not full path
    std::string path;         // full path
    bool        isDir = false;
    size_t      size  = 0;
};

// Returns entries in the given directory. Empty on failure or empty dir.
std::vector<Entry> listDirectory(const std::string& utf8path,
                                  bool includeDirs  = true,
                                  bool includeFiles = true);

// ============================================================================
// §7  PLATFORM-SPECIFIC PATHS
// ============================================================================
//
// These return sensible writable locations per platform:
//   Win32   — APPDATA / Documents / temp
//   Linux   — XDG_CONFIG_HOME / XDG_DATA_HOME / /tmp
//   Android — app->activity->internalDataPath / externalDataPath

std::string appDataDir();    // config / preferences storage
std::string documentsDir();  // user-visible documents
std::string tempDir();       // temporary files
std::string executableDir(); // directory containing the running binary

} // namespace FluxFile