#include "package_manager.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "../include/flux/flux_json.hpp"

namespace fs = std::filesystem;
using json = JsonValue;

namespace {

std::optional<fs::path> find_project_root() {
    fs::path dir = fs::current_path();
    for (int i = 0; i < 16; ++i) {
        if (fs::exists(dir / "CMakeLists.txt") && fs::exists(dir / "config" / "AppConfig.cmake")) {
            return dir;
        }
        fs::path parent = dir.parent_path();
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }
    return std::nullopt;
}

std::optional<std::string> run_capture(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string result;

#if defined(_WIN32)
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) return std::nullopt;

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }
#if defined(_WIN32)
    int rc = _pclose(pipe);
#else
    int rc = pclose(pipe);
#endif

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }

    if (rc != 0) return std::nullopt;
    return result;
}

bool command_exists(const std::string& cmd) {
#if defined(_WIN32)
    return std::system(("where " + cmd + " > NUL 2>&1").c_str()) == 0;
#else
    return std::system(("command -v " + cmd + " > /dev/null 2>&1").c_str()) == 0;
#endif
}

fs::path registry_path(const fs::path& root) { return root / "cli" / "registry" / "index.json"; }
fs::path manifest_path(const fs::path& root) { return root / "flux.deps.json"; }

std::optional<json> load_json_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;

    std::ostringstream ss;
    ss << in.rdbuf();

    try {
        return JsonParser::parse(ss.str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "flux: failed to parse %s: %s\n", path.string().c_str(), e.what());
        return std::nullopt;
    }
}

bool write_json_file(const fs::path& path, const json& j) {
    std::ofstream out(path);
    if (!out) return false;
    out << j.dump(2) << "\n";
    return out.good();
}

json load_manifest(const fs::path& root) {
    auto path = manifest_path(root);
    if (!fs::exists(path)) {
        json fresh;
        fresh["packages"] = json::object();
        return fresh;
    }
    auto j = load_json_file(path);
    if (!j || !j->contains("packages")) {
        json fresh;
        fresh["packages"] = json::object();
        return fresh;
    }
    return *j;
}

bool save_manifest(const fs::path& root, const json& manifest) {
    return write_json_file(manifest_path(root), manifest);
}

} // namespace

int cmd_add(const std::string& package, const std::string& ref_override) {
    auto root = find_project_root();
    if (!root) {
        std::fprintf(stderr,
            "ERROR: could not locate project root "
            "(no CMakeLists.txt / config/AppConfig.cmake found above current directory).\n");
        return 1;
    }

    if (!command_exists("git")) {
        std::fprintf(stderr, "ERROR: git not found in PATH. Run 'flux doctor' to check your toolchain.\n");
        return 1;
    }

    auto registry = load_json_file(registry_path(*root));
    if (!registry) {
        std::fprintf(stderr, "ERROR: could not read registry at %s\n", registry_path(*root).string().c_str());
        return 1;
    }

    if (!registry->contains(package)) {
        std::fprintf(stderr, "flux: unknown package '%s' (not found in registry).\n", package.c_str());
        return 1;
    }

    json manifest = load_manifest(*root);
    if (manifest["packages"].contains(package)) {
        std::fprintf(stderr,
            "flux: '%s' is already added (see flux.deps.json). Run 'flux remove %s' first to re-add.\n",
            package.c_str(), package.c_str());
        return 1;
    }

    const auto& entry = (*registry)[package];
    if (!entry.contains("git") || !entry.contains("cmake_fragment")) {
        std::fprintf(stderr, "flux: registry entry for '%s' is malformed (missing git/cmake_fragment).\n",
            package.c_str());
        return 1;
    }

    std::string git_url = entry["git"].asString();
    std::string ref = !ref_override.empty() ? ref_override : entry.value("ref", std::string("main"));
    std::string cmake_fragment = entry["cmake_fragment"].asString();

    fs::path dest = *root / "external" / package;
    if (fs::exists(dest)) {
        std::fprintf(stderr, "flux: %s already exists on disk; remove it manually or run 'flux remove %s'.\n",
            dest.string().c_str(), package.c_str());
        return 1;
    }

    std::printf("Fetching %s (%s)...\n", package.c_str(), ref.c_str());
    std::string clone_cmd = "git clone --branch " + ref + " --depth 1 " + git_url +
        " \"" + dest.string() + "\" 2>&1";
    int rc = std::system(clone_cmd.c_str());
    if (rc != 0) {
        std::fprintf(stderr, "ERROR: failed to clone %s at ref '%s'.\n", git_url.c_str(), ref.c_str());
        return 1;
    }

    auto commit = run_capture("git -C \"" + dest.string() + "\" rev-parse HEAD");
    if (!commit) {
        std::fprintf(stderr, "ERROR: cloned %s but could not resolve HEAD commit.\n", package.c_str());
        fs::remove_all(dest);
        return 1;
    }

    // Vendored source shouldn't drag its own git history into flux's repo.
    fs::remove_all(dest / ".git");

    fs::path fragment_path = dest / "flux-package.cmake";
    std::ofstream frag_out(fragment_path);
    if (!frag_out) {
        std::fprintf(stderr, "ERROR: could not write %s\n", fragment_path.string().c_str());
        fs::remove_all(dest);
        return 1;
    }
    frag_out << cmake_fragment;
    frag_out.close();

    manifest["packages"][package] = {
        {"version", ref},
        {"commit", *commit},
        {"source", git_url}
    };

    if (!save_manifest(*root, manifest)) {
        std::fprintf(stderr, "ERROR: fetched %s but failed to write flux.deps.json\n", package.c_str());
        return 1;
    }

    std::printf("\nAdded %s (%s @ %s) -> external/%s\n",
        package.c_str(), ref.c_str(), commit->c_str(), package.c_str());
    std::printf("Re-run cmake configure to pick it up.\n");
    return 0;
}

int cmd_remove(const std::string& package) {
    auto root = find_project_root();
    if (!root) {
        std::fprintf(stderr,
            "ERROR: could not locate project root "
            "(no CMakeLists.txt / config/AppConfig.cmake found above current directory).\n");
        return 1;
    }

    json manifest = load_manifest(*root);
    if (!manifest["packages"].contains(package)) {
        std::fprintf(stderr, "flux: '%s' is not currently added (see flux.deps.json).\n", package.c_str());
        return 1;
    }

    fs::path dest = *root / "external" / package;
    if (fs::exists(dest)) {
        std::error_code ec;
        fs::remove_all(dest, ec);
        if (ec) {
            std::fprintf(stderr, "ERROR: failed to remove %s: %s\n", dest.string().c_str(), ec.message().c_str());
            return 1;
        }
    }

    manifest["packages"].erase(package);
    if (!save_manifest(*root, manifest)) {
        std::fprintf(stderr, "ERROR: removed %s from disk but failed to update flux.deps.json\n", package.c_str());
        return 1;
    }

    std::printf("Removed %s.\n", package.c_str());
    std::printf("Re-run cmake configure to drop it from the build.\n");
    return 0;
}