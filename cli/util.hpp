#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include "../include/flux/flux_json.hpp"

using json = JsonValue;
namespace fs = std::filesystem;

// Runs `cmd` in the OS shell, silently checks it exits 0 (stdout/stderr
// discarded). Used for cheap toolchain-presence checks.
bool command_exists(const std::string& cmd);

// Runs `command` and captures trimmed stdout. Returns nullopt on nonzero
// exit or launch failure. Caller is responsible for redirecting stderr
// into the command string (e.g. "... 2>&1") if it wants errors captured too.
std::optional<std::string> run_capture(const std::string& command);

std::optional<json> load_json_file(const fs::path& path);
bool write_json_file(const fs::path& path, const json& j);