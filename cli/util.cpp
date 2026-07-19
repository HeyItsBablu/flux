#include "util.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

bool command_exists(const std::string& cmd) {
#if defined(_WIN32)
    return std::system(("where " + cmd + " > NUL 2>&1").c_str()) == 0;
#else
    return std::system(("command -v " + cmd + " > /dev/null 2>&1").c_str()) == 0;
#endif
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