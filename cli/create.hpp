#pragma once
#include <string>

// Scaffolds a new FluxUI project into ./<project_name> by cloning a
// tagged release of the flux engine repo and stripping engine-dev-only
// content. `ref` is a git tag; empty resolves to the latest release tag.
int cmd_create(const std::string& project_name, const std::string& ref);