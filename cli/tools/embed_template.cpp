#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

namespace fs = std::filesystem;

namespace {

bool has_excluded_component(const fs::path& rel) {
    for (const auto& part : rel) {
        const std::string p = part.string();
        if (p == ".git" || p == ".github" || p == "build") return true;
    }
    return false;
}

bool is_top_level_excluded(const fs::path& rel) {
    const std::string top = rel.begin()->string();
    static const char* kDirs[] = { "examples", "screenshots", "docs", "cli", "scripts" };
    for (auto* d : kDirs) if (top == d) return true;
    if (top == "external") {
        auto it = rel.begin();
        ++it;
        if (it != rel.end() && it->string() == "emsdk") return true;
    }
    static const char* kFiles[] = { "CHANGELOG.md", "INSTALL.md" };
    for (auto* f : kFiles) if (rel == f) return true;
    return false;
}

void write_u16(std::ofstream& out, uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); }
void write_u64(std::ofstream& out, uint64_t v) { out.write(reinterpret_cast<const char*>(&v), 8); }

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: embed_template <source_root> <build_dir> <output.bin>\n");
        return 1;
    }
    fs::path root = fs::canonical(argv[1]);
    fs::path build_dir = fs::exists(argv[2]) ? fs::canonical(argv[2]) : fs::path(argv[2]);
    fs::path out_path = argv[3];

    struct Entry { std::string rel; bool is_dir; std::vector<char> data; };
    std::vector<Entry> entries;

    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code ec;
        if (fs::equivalent(it->path(), build_dir, ec)) { it.disable_recursion_pending(); continue; }

        fs::path rel = fs::relative(it->path(), root);
        if (has_excluded_component(rel) || is_top_level_excluded(rel)) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }

        std::string rel_str = rel.generic_string();
        bool in_assets = rel_str.rfind("assets/", 0) == 0 || rel_str == "assets";

        if (it->is_directory()) {
            if (rel_str == "assets") entries.push_back({rel_str, true, {}});
            else if (!in_assets) continue; // only need explicit entries for empty dirs like assets/
            continue;
        }
        if (in_assets) continue; // demo media dropped, folder kept via the entry above

        std::ifstream in(it->path(), std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
        entries.push_back({rel_str, false, std::move(data)});
    }

    std::ofstream out(out_path, std::ios::binary);
    out.write("FTPL", 4);
    uint32_t count = static_cast<uint32_t>(entries.size());
    out.write(reinterpret_cast<const char*>(&count), 4);
    for (auto& e : entries) {
        write_u16(out, static_cast<uint16_t>(e.rel.size()));
        out.write(e.rel.data(), static_cast<std::streamsize>(e.rel.size()));
        char type = e.is_dir ? 1 : 0;
        out.write(&type, 1);
        write_u64(out, e.data.size());
        if (!e.data.empty()) out.write(e.data.data(), static_cast<std::streamsize>(e.data.size()));
    }
    std::fprintf(stdout, "embedded %u entries into %s\n", count, out_path.string().c_str());
    return 0;
}