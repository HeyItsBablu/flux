#pragma once
#include <filesystem>
#include <string>
#include <utility>

std::pair<int, std::filesystem::path>
windows_configure_and_build(bool release, const std::string &build_dir);