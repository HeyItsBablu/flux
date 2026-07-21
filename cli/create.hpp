#pragma once
#include <string>

// Scaffolds a new FluxUI project into ./<project_name> by extracting the
// stripped engine template embedded in this CLI binary at build time.
// Fully offline — no network access, no git dependency.
int cmd_create(const std::string& project_name);