// cli/platform/linux_build.hpp
#pragma once
#include <filesystem>
#include <string>
#include <utility>

std::pair<int, std::filesystem::path>
linux_configure_and_build(bool release, const std::string &build_dir);